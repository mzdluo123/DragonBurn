#include "WebRadar.h"
#include "WebRadarMapProvider.h"

#include "../Resources/WebRadarAssets.generated.h"
#include "../Libs/civetweb/include/civetweb.h"

#include <json.hpp>

#include "../Game/Entity.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <cstdlib>
#include <memory>
#include <set>
#include <thread>

namespace
{
    constexpr std::size_t MaxRadarPlayers = 64;
    constexpr auto PublishInterval = std::chrono::milliseconds(50);
    constexpr auto InvalidPublishInterval = std::chrono::milliseconds(1000);

    struct RadarPlayer
    {
        int slot = -1;
        std::array<char, 64> name{};
        int team = 0;
        bool alive = false;
        int health = 0;
        std::array<char, 64> weapon{};
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
    };

    struct RadarSnapshot
    {
        std::uint64_t sequence = 0;
        DWORD gameTick = 0;
        std::uint64_t serverTimeMs = 0;
        bool inGame = false;
        std::array<char, 64> map{};
        RadarPlayer local{};
        std::array<RadarPlayer, MaxRadarPlayers> players{};
        std::size_t playerCount = 0;
    };

    std::mutex snapshotMutex;
    RadarSnapshot latestSnapshot;
    std::atomic<std::uint64_t> nextSequence{ 1 };
    std::atomic<std::int64_t> nextPublishAtMs{ 0 };
    std::atomic<std::int64_t> nextInvalidPublishAtMs{ 0 };

    std::int64_t SteadyTimeMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::uint64_t SystemTimeMs()
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    bool IsContinuationByte(const unsigned char value)
    {
        return (value & 0xC0U) == 0x80U;
    }

    void CopyUtf8(std::array<char, 64>& destination, std::string_view source)
    {
        destination.fill('\0');
        const auto limit = source.size() < destination.size() - 1 ? source.size() : destination.size() - 1;
        std::size_t sourceOffset = 0;
        std::size_t destinationOffset = 0;

        while (sourceOffset < limit)
        {
            const auto lead = static_cast<unsigned char>(source[sourceOffset]);
            std::size_t length = 0;
            if (lead <= 0x7FU)
                length = 1;
            else if (lead >= 0xC2U && lead <= 0xDFU)
                length = 2;
            else if (lead >= 0xE0U && lead <= 0xEFU)
                length = 3;
            else if (lead >= 0xF0U && lead <= 0xF4U)
                length = 4;
            else
            {
                ++sourceOffset;
                continue;
            }

            if (sourceOffset + length > source.size() || destinationOffset + length >= destination.size())
                break;

            bool valid = true;
            for (std::size_t index = 1; index < length; ++index)
                valid = valid && IsContinuationByte(static_cast<unsigned char>(source[sourceOffset + index]));
            if (valid && length >= 3)
            {
                const auto second = static_cast<unsigned char>(source[sourceOffset + 1]);
                valid = !(lead == 0xE0U && second < 0xA0U) && !(lead == 0xEDU && second >= 0xA0U)
                    && !(lead == 0xF0U && second < 0x90U) && !(lead == 0xF4U && second > 0x8FU);
            }
            if (!valid)
            {
                ++sourceOffset;
                continue;
            }

            std::memcpy(destination.data() + destinationOffset, source.data() + sourceOffset, length);
            destinationOffset += length;
            sourceOffset += length;
        }
    }

    bool IsValidMapName(const std::string_view mapName)
    {
        return !mapName.empty() && mapName.size() < latestSnapshot.map.size()
            && std::all_of(mapName.begin(), mapName.end(), [](const unsigned char value) {
                return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
            });
    }

    bool TryCopyPlayer(const CEntity& entity, const int slot, RadarPlayer& result)
    {
        const auto& position = entity.Pawn.Pos;
        const float yaw = entity.Pawn.ViewAngle.y;
        if (!std::isfinite(position.x) || !std::isfinite(position.y)
            || !std::isfinite(position.z) || !std::isfinite(yaw))
            return false;

        result = {};
        result.slot = slot;
        CopyUtf8(result.name, entity.Controller.PlayerName);
        result.team = entity.Controller.TeamID;
        result.alive = entity.Controller.AliveStatus == 1 && entity.Pawn.Health > 0;
        result.health = std::clamp(entity.Pawn.Health, 0, 100);
        CopyUtf8(result.weapon, entity.Pawn.WeaponName);
        result.x = position.x;
        result.y = position.y;
        result.z = position.z;
        result.yaw = yaw;
        return true;
    }

    bool CopyLatestSnapshot(RadarSnapshot& destination)
    {
        std::lock_guard lock(snapshotMutex);
        destination = latestSnapshot;
        return destination.sequence != 0;
    }
}

void WebRadar::Publish(const CEntity& localEntity,
    const std::vector<std::pair<int, CEntity>>& entities,
    const DWORD gameTick,
    const std::string_view mapName)
{
    const auto now = SteadyTimeMs();
    auto deadline = nextPublishAtMs.load(std::memory_order_relaxed);
    if (now < deadline || !nextPublishAtMs.compare_exchange_strong(
        deadline, now + PublishInterval.count(), std::memory_order_relaxed))
        return;

    if (!IsValidMapName(mapName))
    {
        Invalidate();
        return;
    }

    RadarSnapshot snapshot;
    snapshot.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
    snapshot.gameTick = gameTick;
    snapshot.serverTimeMs = SystemTimeMs();
    snapshot.inGame = true;
    CopyUtf8(snapshot.map, mapName);
    if (!TryCopyPlayer(localEntity, -1, snapshot.local))
    {
        Invalidate();
        return;
    }

    for (const auto& [slot, entity] : entities)
    {
        if (snapshot.playerCount == snapshot.players.size())
            break;
        RadarPlayer player;
        if (TryCopyPlayer(entity, slot, player))
            snapshot.players[snapshot.playerCount++] = player;
    }

    std::unique_lock lock(snapshotMutex, std::try_to_lock);
    if (lock.owns_lock())
        latestSnapshot = snapshot;
}

void WebRadar::Invalidate()
{
    const auto now = SteadyTimeMs();
    std::unique_lock lock(snapshotMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;

    if (latestSnapshot.sequence != 0 && !latestSnapshot.inGame)
    {
        auto deadline = nextInvalidPublishAtMs.load(std::memory_order_relaxed);
        if (now < deadline || !nextInvalidPublishAtMs.compare_exchange_strong(
            deadline, now + InvalidPublishInterval.count(), std::memory_order_relaxed))
            return;
    }
    else
    {
        nextInvalidPublishAtMs.store(now + InvalidPublishInterval.count(), std::memory_order_relaxed);
    }

    RadarSnapshot snapshot;
    snapshot.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
    snapshot.serverTimeMs = SystemTimeMs();
    latestSnapshot = snapshot;
}

namespace
{
    using json = nlohmann::json;

    struct WebRadarServiceState
    {
        std::mutex lifecycleMutex;
        std::mutex connectionsMutex;
        mg_context* context = nullptr;
        std::set<mg_connection*> connections;
        std::unique_ptr<WebRadarMaps::Provider> mapProvider;
        std::thread publisher;
        std::atomic<bool> running{ false };
    };

    WebRadarServiceState serviceState;
    std::once_flag exitRegistration;

    json PlayerJson(const RadarPlayer& player)
    {
        return {
            { "slot", player.slot }, { "name", player.name.data() }, { "team", player.team },
            { "alive", player.alive }, { "health", player.health }, { "weapon", player.weapon.data() },
            { "x", player.x }, { "y", player.y }, { "z", player.z }, { "yaw", player.yaw }
        };
    }

    std::string SerializeSnapshot(const RadarSnapshot& snapshot)
    {
        json message{
            { "v", 1 }, { "type", "snapshot" }, { "seq", snapshot.sequence },
            { "gameTick", snapshot.gameTick }, { "serverTimeMs", snapshot.serverTimeMs },
            { "inGame", snapshot.inGame }, { "map", snapshot.inGame ? snapshot.map.data() : "" },
            { "local", snapshot.inGame ? PlayerJson(snapshot.local) : json(nullptr) },
            { "players", json::array() }
        };
        if (snapshot.inGame)
            for (std::size_t index = 0; index < snapshot.playerCount; ++index)
                message["players"].push_back(PlayerJson(snapshot.players[index]));
        return message.dump();
    }

    void SendResponse(mg_connection* connection, const int status, const std::string_view mime,
        const void* body, const std::size_t bodySize, const std::string_view cacheControl,
        const std::string_view etag = {}, const bool stale = false)
    {
        const std::string length = std::to_string(bodySize);
        mg_response_header_start(connection, status);
        mg_response_header_add(connection, "Content-Type", mime.data(), static_cast<int>(mime.size()));
        mg_response_header_add(connection, "Content-Length", length.c_str(), -1);
        mg_response_header_add(connection, "Cache-Control", cacheControl.data(), static_cast<int>(cacheControl.size()));
        if (!etag.empty())
            mg_response_header_add(connection, "ETag", etag.data(), static_cast<int>(etag.size()));
        if (stale)
            mg_response_header_add(connection, "X-WebRadar-Stale", "1", 1);
        mg_response_header_send(connection);
        if (bodySize != 0)
            mg_write(connection, body, bodySize);
    }

    int SendText(mg_connection* connection, const int status, const std::string_view body)
    {
        SendResponse(connection, status, "application/json; charset=utf-8", body.data(), body.size(), "no-store");
        return status;
    }

    int SendBinary(mg_connection* connection, const WebRadarMaps::BinaryResult& resource)
    {
        if (resource.status == WebRadarMaps::Status::MapUnavailable)
            return SendText(connection, 404, R"({"error":"map_unavailable"})");
        if (resource.status != WebRadarMaps::Status::Available)
            return SendText(connection, 503, R"({"error":"map_resource_error"})");
        const char* requestEtag = mg_get_header(connection, "If-None-Match");
        if (requestEtag && resource.etag == requestEtag)
        {
            SendResponse(connection, 304, resource.contentType, nullptr, 0, "public,max-age=86400", resource.etag);
            return 304;
        }
        SendResponse(connection, 200, resource.contentType, resource.body.data(), resource.body.size(),
            "public,max-age=86400", resource.etag);
        return 200;
    }

    int HandleMapRequest(mg_connection* connection, const std::string_view uri)
    {
        constexpr std::string_view prefix = "/api/maps/";
        const auto remainder = uri.substr(prefix.size());
        if (remainder.empty())
            return SendText(connection, 404, R"({"error":"map_unavailable"})");

        const auto slash = remainder.find('/');
        const auto mapName = remainder.substr(0, slash);
        if (!IsValidMapName(mapName))
            return SendText(connection, 404, R"({"error":"map_unavailable"})");
        if (!serviceState.mapProvider)
            return SendText(connection, 503, R"({"error":"map_resource_error"})");

        if (slash == std::string_view::npos)
        {
            const auto result = serviceState.mapProvider->GetMap(mapName);
            if (result.status == WebRadarMaps::Status::MapUnavailable)
                return SendText(connection, 404, R"({"error":"map_unavailable"})");
            if (result.status != WebRadarMaps::Status::Available)
                return SendText(connection, 503, R"({"error":"map_resource_error"})");

            const auto& metadata = result.metadata;
            json layers = json::array();
            for (const auto& layer : metadata.layers)
                layers.push_back({ { "id", layer.id }, { "minZ", layer.minZ }, { "maxZ", layer.maxZ }, { "image", layer.image } });
            const std::string body = json{
                { "v", 1 }, { "map", metadata.map }, { "icon", metadata.icon },
                { "position", { { "x", metadata.positionX }, { "y", metadata.positionY } } },
                { "scale", metadata.scale }, { "rotate", metadata.rotate }, { "zoom", metadata.zoom },
                { "layers", std::move(layers) }
            }.dump();
            SendResponse(connection, 200, "application/json; charset=utf-8", body.data(), body.size(),
                "no-store", {}, metadata.stale);
            return 200;
        }

        const auto resource = remainder.substr(slash + 1);
        if (resource == "icon")
            return SendBinary(connection, serviceState.mapProvider->GetIcon(mapName));
        constexpr std::string_view layersPrefix = "layers/";
        if (resource.starts_with(layersPrefix))
        {
            const auto layerId = resource.substr(layersPrefix.size());
            if (layerId.find('/') == std::string_view::npos && IsValidMapName(layerId))
                return SendBinary(connection, serviceState.mapProvider->GetLayer(mapName, layerId));
        }
        return SendText(connection, 404, R"({"error":"map_unavailable"})");
    }

    int HandleHttp(mg_connection* connection, void*)
    {
        const auto* request = mg_get_request_info(connection);
        if (!request || !request->request_method || std::strcmp(request->request_method, "GET") != 0)
        {
            SendResponse(connection, 405, "application/json; charset=utf-8", R"({"error":"method_not_allowed"})",
                sizeof(R"({"error":"method_not_allowed"})") - 1, "no-store");
            return 405;
        }
        const std::string_view uri = request->local_uri ? request->local_uri : "/";
        if (uri == "/health")
            return SendText(connection, 200, R"({"v":1,"status":"ok"})");
        if (uri.starts_with("/api/maps/"))
            return HandleMapRequest(connection, uri);
        if (const auto* asset = WebRadarAssets::Find(uri))
        {
            const bool html = asset->path == "/index.html";
            SendResponse(connection, 200, asset->mime, asset->data, asset->size,
                html ? "no-store" : "public,max-age=31536000,immutable");
            return 200;
        }
        return SendText(connection, 404, R"({"error":"not_found"})");
    }

    int WebSocketConnect(const mg_connection*, void*)
    {
        return serviceState.running.load(std::memory_order_acquire) ? 0 : 1;
    }

    void WebSocketReady(mg_connection* connection, void*)
    {
        {
            std::lock_guard lock(serviceState.connectionsMutex);
            serviceState.connections.insert(connection);
        }
        RadarSnapshot snapshot;
        if (CopyLatestSnapshot(snapshot))
        {
            const std::string message = SerializeSnapshot(snapshot);
            mg_lock_connection(connection);
            mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, message.data(), message.size());
            mg_unlock_connection(connection);
        }
    }

    int WebSocketData(mg_connection*, int, char*, std::size_t, void*)
    {
        return 1;
    }

    void WebSocketClose(const mg_connection* connection, void*)
    {
        std::lock_guard lock(serviceState.connectionsMutex);
        serviceState.connections.erase(const_cast<mg_connection*>(connection));
    }

    void BroadcastLoop()
    {
        std::uint64_t lastSequence = 0;
        while (serviceState.running.load(std::memory_order_acquire))
        {
            RadarSnapshot snapshot;
            if (CopyLatestSnapshot(snapshot) && snapshot.sequence != lastSequence)
            {
                lastSequence = snapshot.sequence;
                const std::string message = SerializeSnapshot(snapshot);
                std::lock_guard lock(serviceState.connectionsMutex);
                for (auto connection = serviceState.connections.begin(); connection != serviceState.connections.end();)
                {
                    mg_lock_connection(*connection);
                    const int written = mg_websocket_write(*connection, MG_WEBSOCKET_OPCODE_TEXT, message.data(), message.size());
                    mg_unlock_connection(*connection);
                    if (written <= 0)
                        connection = serviceState.connections.erase(connection);
                    else
                        ++connection;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool WebRadar::Start(const WebRadarConfig& config)
{
    std::lock_guard lock(serviceState.lifecycleMutex);
    if (serviceState.context)
        return true;
    if (config.listenAddress != "0.0.0.0" || config.port == 0 || config.mapCacheRoot.empty())
        return false;
    const unsigned features = mg_init_library(MG_FEATURES_WEBSOCKET);
    if ((features & MG_FEATURES_WEBSOCKET) == 0)
        return false;

    serviceState.mapProvider = std::make_unique<WebRadarMaps::Provider>(config.mapCacheRoot);
    const std::string listeningPort = config.listenAddress + ":" + std::to_string(config.port);
    const char* options[] = {
        "listening_ports", listeningPort.c_str(),
        "num_threads", "4",
        "request_timeout_ms", "10000",
        "enable_keep_alive", "yes",
        nullptr
    };
    mg_callbacks callbacks{};
    serviceState.context = mg_start(&callbacks, nullptr, options);
    if (!serviceState.context)
    {
        serviceState.mapProvider.reset();
        mg_exit_library();
        return false;
    }

    mg_set_request_handler(serviceState.context, "/", HandleHttp, nullptr);
    mg_set_websocket_handler(serviceState.context, "/ws", WebSocketConnect, WebSocketReady,
        WebSocketData, WebSocketClose, nullptr);
    serviceState.running.store(true, std::memory_order_release);
    try
    {
        serviceState.publisher = std::thread(BroadcastLoop);
    }
    catch (...)
    {
        serviceState.running.store(false, std::memory_order_release);
        mg_stop(serviceState.context);
        serviceState.context = nullptr;
        serviceState.mapProvider.reset();
        mg_exit_library();
        return false;
    }
    std::call_once(exitRegistration, [] { std::atexit(WebRadar::Stop); });
    return true;
}

void WebRadar::Stop()
{
    std::lock_guard lock(serviceState.lifecycleMutex);
    serviceState.running.store(false, std::memory_order_release);
    if (serviceState.publisher.joinable())
        serviceState.publisher.join();
    {
        std::lock_guard connectionsLock(serviceState.connectionsMutex);
        serviceState.connections.clear();
    }
    if (serviceState.context)
    {
        mg_stop(serviceState.context);
        serviceState.context = nullptr;
        serviceState.mapProvider.reset();
        mg_exit_library();
    }
}
