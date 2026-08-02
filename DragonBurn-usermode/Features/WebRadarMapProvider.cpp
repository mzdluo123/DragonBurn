#include "WebRadarMapProvider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <winhttp.h>

#include <json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace
{
    using json = nlohmann::json;
    constexpr std::size_t ManifestLimit = 4U * 1024U * 1024U;
    constexpr std::size_t ImageLimit = 16U * 1024U * 1024U;
    constexpr std::size_t MaximumLayers = 8;
    constexpr auto ManifestLifetime = std::chrono::hours(12);
    constexpr std::wstring_view ManifestUrl = L"https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/data/available.json";
    constexpr std::wstring_view AllowedOrigin = L"https://raw.githubusercontent.com";
    constexpr std::wstring_view AllowedPathPrefix = L"/MurkyYT/cs2-map-icons/";
    constexpr double LowestAltitude = -std::numeric_limits<float>::max();
    constexpr double HighestAltitude = std::numeric_limits<float>::max();

    struct InternetHandle
    {
        HINTERNET value = nullptr;
        ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
        explicit operator bool() const { return value != nullptr; }
    };

    bool IsSafeSegment(const std::string_view value)
    {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
        });
    }

    std::optional<std::wstring> ValidateRemoteUrl(const std::wstring_view url)
    {
        if (!url.starts_with(AllowedOrigin))
            return std::nullopt;
        const auto path = url.substr(AllowedOrigin.size());
        if (!path.starts_with(AllowedPathPrefix) || path.find(L"..") != std::wstring_view::npos
            || path.find(L'\\') != std::wstring_view::npos || path.find(L'%') != std::wstring_view::npos
            || path.find(L'?') != std::wstring_view::npos || path.find(L'#') != std::wstring_view::npos)
            return std::nullopt;
        if (!std::all_of(path.begin(), path.end(), [](const wchar_t character) {
            return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z')
                || (character >= L'0' && character <= L'9') || character == L'/' || character == L'_'
                || character == L'-' || character == L'.';
        }))
            return std::nullopt;
        return std::wstring(url);
    }

    std::optional<std::wstring> Utf8ToWide(const std::string& value)
    {
        if (value.empty())
            return std::nullopt;
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (length <= 0)
            return std::nullopt;
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length) != length)
            return std::nullopt;
        return result;
    }

    WebRadarMaps::HttpResponse FetchWithWinHttp(const std::wstring_view requestedUrl, const std::size_t maxBytes)
    {
        WebRadarMaps::HttpResponse response;
        const auto validated = ValidateRemoteUrl(requestedUrl);
        if (!validated)
            return response;

        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(validated->c_str(), static_cast<DWORD>(validated->size()), 0, &components)
            || components.nScheme != INTERNET_SCHEME_HTTPS)
            return response;

        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        if (host != L"raw.githubusercontent.com")
            return response;
        std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.dwExtraInfoLength)
            path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

        InternetHandle session{ WinHttpOpen(L"VoidSpectre-WebRadar/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session)
            return response;
        WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 10000);

        InternetHandle connection{ WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0) };
        if (!connection)
            return response;
        InternetHandle request{ WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
        if (!request)
            return response;
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
        if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
            return response;

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
            return response;
        response.status = status;

        DWORD typeSize = 0;
        WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
            WINHTTP_NO_OUTPUT_BUFFER, &typeSize, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && typeSize >= sizeof(wchar_t))
        {
            std::wstring type(typeSize / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                type.data(), &typeSize, WINHTTP_NO_HEADER_INDEX))
            {
                type.resize(wcsnlen_s(type.c_str(), type.size()));
                const int utf8Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, type.data(),
                    static_cast<int>(type.size()), nullptr, 0, nullptr, nullptr);
                if (utf8Size > 0)
                {
                    response.contentType.resize(static_cast<std::size_t>(utf8Size));
                    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, type.data(), static_cast<int>(type.size()),
                        response.contentType.data(), utf8Size, nullptr, nullptr);
                }
            }
        }

        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.value, &available))
                return {};
            if (available == 0)
                break;
            if (response.body.size() + available > maxBytes)
                return {};
            const auto offset = response.body.size();
            response.body.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(request.value, response.body.data() + offset, available, &read))
                return {};
            response.body.resize(offset + read);
        }
        return response;
    }

    bool HasPngSignature(const std::vector<std::byte>& body)
    {
        constexpr std::array<unsigned char, 8> signature{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        return body.size() >= signature.size() && std::equal(signature.begin(), signature.end(), body.begin(),
            [](const unsigned char left, const std::byte right) { return left == std::to_integer<unsigned char>(right); });
    }

    bool HasWebpSignature(const std::vector<std::byte>& body)
    {
        if (body.size() < 12)
            return false;
        const auto character = [&body](const std::size_t index) { return std::to_integer<unsigned char>(body[index]); };
        return character(0) == 'R' && character(1) == 'I' && character(2) == 'F' && character(3) == 'F'
            && character(8) == 'W' && character(9) == 'E' && character(10) == 'B' && character(11) == 'P';
    }

    bool IsImage(const std::vector<std::byte>& body)
    {
        return HasPngSignature(body) || HasWebpSignature(body);
    }

    std::string ImageMime(const std::vector<std::byte>& body)
    {
        return HasPngSignature(body) ? "image/png" : "image/webp";
    }

    bool AtomicWrite(const std::filesystem::path& destination, const std::vector<std::byte>& body)
    {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            return false;
        auto temporary = destination;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                return false;
            output.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
            if (!output)
                return false;
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    std::optional<std::vector<std::byte>> ReadFileLimited(const std::filesystem::path& path, const std::size_t limit)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > limit)
            return std::nullopt;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return std::nullopt;
        std::vector<std::byte> result(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
        if (!input && !result.empty())
            return std::nullopt;
        return result;
    }

    std::string LayerIdFromUrl(const std::string& mapName, const std::string& url)
    {
        const auto slash = url.find_last_of('/');
        std::string fileName = slash == std::string::npos ? url : url.substr(slash + 1);
        const std::string prefix = mapName + "_";
        if (!fileName.starts_with(prefix))
            return {};
        fileName.erase(0, prefix.size());
        constexpr std::array<std::string_view, 4> suffixes{ "_radar_psd.png", "_radar_psd.webp", "radar_psd.png", "radar_psd.webp" };
        for (const auto suffix : suffixes)
        {
            if (!fileName.ends_with(suffix))
                continue;
            fileName.resize(fileName.size() - suffix.size());
            while (!fileName.empty() && fileName.back() == '_')
                fileName.pop_back();
            return fileName.empty() ? "main" : fileName;
        }
        return {};
    }

    std::string ExtensionFromUrl(const std::wstring& url)
    {
        return url.ends_with(L".webp") ? ".webp" : ".png";
    }

    std::string MakeEtag(const std::vector<std::byte>& body)
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto value : body)
        {
            hash ^= std::to_integer<unsigned char>(value);
            hash *= 1099511628211ULL;
        }
        std::ostringstream stream;
        stream << '"' << std::hex << std::setfill('0') << std::setw(16) << hash << '"';
        return stream.str();
    }
}

struct WebRadarMaps::Provider::ParsedMap
{
    std::string name;
    std::wstring iconUrl;
    std::filesystem::path iconPath;
    double positionX = 0.0;
    double positionY = 0.0;
    double scale = 0.0;
    double rotate = 0.0;
    double zoom = 0.0;

    struct Layer
    {
        std::string id;
        double minZ = LowestAltitude;
        double maxZ = HighestAltitude;
        std::wstring url;
        std::filesystem::path path;
    };
    std::vector<Layer> layers;
};

WebRadarMaps::Provider::Provider(std::filesystem::path cacheRoot)
    : Provider(std::move(cacheRoot), FetchWithWinHttp)
{
}

WebRadarMaps::Provider::Provider(std::filesystem::path cacheRoot, FetchFunction fetch)
    : cacheRoot_(std::move(cacheRoot)), fetch_(std::move(fetch))
{
}

WebRadarMaps::Provider::~Provider() = default;

bool WebRadarMaps::Provider::ParseManifest(const std::vector<std::byte>& body)
{
    if (body.empty() || body.size() > ManifestLimit)
        return false;
    const auto* first = reinterpret_cast<const char*>(body.data());
    const auto parsed = json::parse(first, first + body.size(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("maps") || !parsed["maps"].is_object())
        return false;

    std::vector<ParsedMap> parsedMaps;
    for (const auto& [mapName, value] : parsed["maps"].items())
    {
        if (!IsSafeSegment(mapName) || !value.is_object() || !value.contains("path") || !value["path"].is_string()
            || !value.contains("radar_paths") || !value["radar_paths"].is_array()
            || !value.contains("radar_info") || !value["radar_info"].is_object())
            continue;
        const auto& info = value["radar_info"];
        if (!info.contains("pos_x") || !info["pos_x"].is_number() || !info.contains("pos_y") || !info["pos_y"].is_number()
            || !info.contains("scale") || !info["scale"].is_number())
            continue;

        if ((info.contains("rotate") && !info["rotate"].is_number())
            || (info.contains("zoom") && !info["zoom"].is_number()))
            continue;
        ParsedMap map;
        map.name = mapName;
        map.positionX = info["pos_x"].get<double>();
        map.positionY = info["pos_y"].get<double>();
        map.scale = info["scale"].get<double>();
        map.rotate = info.value("rotate", 0.0);
        map.zoom = info.value("zoom", 0.0);
        if (!std::isfinite(map.positionX) || !std::isfinite(map.positionY) || !std::isfinite(map.scale)
            || map.scale <= 0.0 || !std::isfinite(map.rotate) || !std::isfinite(map.zoom))
            continue;

        const auto iconWide = Utf8ToWide(value["path"].get<std::string>());
        if (!iconWide)
            continue;
        const auto iconUrl = ValidateRemoteUrl(*iconWide);
        if (!iconUrl)
            continue;
        map.iconUrl = *iconUrl;
        map.iconPath = cacheRoot_ / "assets" / mapName / ("icon" + ExtensionFromUrl(map.iconUrl));

        std::vector<std::pair<std::string, std::wstring>> radarUrls;
        bool invalid = value["radar_paths"].empty() || value["radar_paths"].size() > MaximumLayers;
        for (const auto& pathValue : value["radar_paths"])
        {
            if (!pathValue.is_string())
            {
                invalid = true;
                break;
            }
            const std::string narrowUrl = pathValue.get<std::string>();
            const std::string id = LayerIdFromUrl(mapName, narrowUrl);
            const auto wideUrl = Utf8ToWide(narrowUrl);
            if (!IsSafeSegment(id) || !wideUrl)
            {
                invalid = true;
                break;
            }
            const auto url = ValidateRemoteUrl(*wideUrl);
            if (!url)
            {
                invalid = true;
                break;
            }
            radarUrls.emplace_back(id, *url);
        }
        if (invalid)
            continue;

        if (info.contains("verticalsections"))
        {
            const auto& sections = info["verticalsections"];
            if (!sections.is_object() || sections.empty() || sections.size() > MaximumLayers)
                continue;
            for (const auto& [sectionName, section] : sections.items())
            {
                const std::string id = sectionName == "default" ? "main" : sectionName;
                if (!IsSafeSegment(id) || !section.is_object() || !section.contains("AltitudeMin")
                    || !section.contains("AltitudeMax") || !section["AltitudeMin"].is_number()
                    || !section["AltitudeMax"].is_number())
                {
                    invalid = true;
                    break;
                }
                const double minimum = section["AltitudeMin"].get<double>();
                const double maximum = section["AltitudeMax"].get<double>();
                const auto source = std::find_if(radarUrls.begin(), radarUrls.end(), [&id](const auto& candidate) {
                    return candidate.first == id;
                });
                if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || source == radarUrls.end())
                {
                    invalid = true;
                    break;
                }
                map.layers.push_back({ id, minimum, maximum, source->second,
                    cacheRoot_ / "assets" / mapName / (id + ExtensionFromUrl(source->second)) });
            }
            std::sort(map.layers.begin(), map.layers.end(), [](const auto& left, const auto& right) { return left.minZ < right.minZ; });
            for (std::size_t index = 1; index < map.layers.size(); ++index)
                invalid = invalid || map.layers[index - 1].maxZ > map.layers[index].minZ;
        }
        else
        {
            std::unordered_set<std::string> identifiers;
            for (const auto& [id, url] : radarUrls)
            {
                if (!identifiers.insert(id).second)
                {
                    invalid = true;
                    break;
                }
                map.layers.push_back({ id, LowestAltitude, HighestAltitude, url,
                    cacheRoot_ / "assets" / mapName / (id + ExtensionFromUrl(url)) });
            }
        }
        if (!invalid && !map.layers.empty() && map.layers.size() <= MaximumLayers)
            parsedMaps.push_back(std::move(map));
    }

    if (parsedMaps.empty())
        return false;
    maps_ = std::move(parsedMaps);
    return true;
}

bool WebRadarMaps::Provider::LoadManifestLocked(bool& stale)
{
    stale = false;
    const auto manifestPath = cacheRoot_ / "available.json";
    std::error_code error;
    bool cacheFresh = false;
    if (std::filesystem::exists(manifestPath, error) && !error)
    {
        const auto modified = std::filesystem::last_write_time(manifestPath, error);
        if (!error)
            cacheFresh = std::filesystem::file_time_type::clock::now() - modified < ManifestLifetime;
    }

    if (!manifestLoaded_)
    {
        if (const auto cached = ReadFileLimited(manifestPath, ManifestLimit); cached && ParseManifest(*cached))
            manifestLoaded_ = true;
    }
    if (manifestLoaded_ && cacheFresh)
    {
        stale = manifestStale_;
        return true;
    }

    const auto response = fetch_(ManifestUrl, ManifestLimit);
    if (response.status == 200 && response.body.size() <= ManifestLimit)
    {
        const auto previousMaps = maps_;
        if (ParseManifest(response.body) && AtomicWrite(manifestPath, response.body))
        {
            manifestLoaded_ = true;
            manifestStale_ = false;
            return true;
        }
        maps_ = previousMaps;
    }

    if (manifestLoaded_)
    {
        manifestStale_ = true;
        stale = true;
        return true;
    }
    return false;
}

bool WebRadarMaps::Provider::DownloadImageLocked(const std::wstring& url, const std::filesystem::path& path)
{
    if (const auto cached = ReadFileLimited(path, ImageLimit); cached && IsImage(*cached))
        return true;
    const auto response = fetch_(url, ImageLimit);
    return response.status == 200 && response.body.size() <= ImageLimit && IsImage(response.body)
        && AtomicWrite(path, response.body);
}

WebRadarMaps::MapResult WebRadarMaps::Provider::GetMapLocked(const std::string_view mapName)
{
    if (!IsSafeSegment(mapName))
        return { Status::MapUnavailable, {} };
    bool stale = false;
    if (!LoadManifestLocked(stale))
        return { Status::Error, {} };
    const auto map = std::find_if(maps_.begin(), maps_.end(), [mapName](const ParsedMap& candidate) {
        return candidate.name == mapName;
    });
    if (map == maps_.end())
        return { Status::MapUnavailable, {} };
    if (!DownloadImageLocked(map->iconUrl, map->iconPath))
        return { Status::Error, {} };
    for (const auto& layer : map->layers)
        if (!DownloadImageLocked(layer.url, layer.path))
            return { Status::Error, {} };

    MapMetadata metadata;
    metadata.map = map->name;
    metadata.icon = "/api/maps/" + map->name + "/icon";
    metadata.positionX = map->positionX;
    metadata.positionY = map->positionY;
    metadata.scale = map->scale;
    metadata.rotate = map->rotate;
    metadata.zoom = map->zoom;
    metadata.stale = stale;
    metadata.layers.reserve(map->layers.size());
    for (const auto& layer : map->layers)
        metadata.layers.push_back({ layer.id, layer.minZ, layer.maxZ,
            "/api/maps/" + map->name + "/layers/" + layer.id });
    return { Status::Available, std::move(metadata) };
}

WebRadarMaps::MapResult WebRadarMaps::Provider::GetMap(const std::string_view mapName)
{
    std::lock_guard lock(mutex_);
    return GetMapLocked(mapName);
}

WebRadarMaps::BinaryResult WebRadarMaps::Provider::ReadImageLocked(const std::filesystem::path& path) const
{
    const auto body = ReadFileLimited(path, ImageLimit);
    if (!body || !IsImage(*body))
        return { Status::MapUnavailable, {}, {}, {} };
    return { Status::Available, ImageMime(*body), MakeEtag(*body), *body };
}

WebRadarMaps::BinaryResult WebRadarMaps::Provider::GetIcon(const std::string_view mapName)
{
    std::lock_guard lock(mutex_);
    const auto result = GetMapLocked(mapName);
    if (result.status != Status::Available)
        return { result.status, {}, {}, {} };
    const auto map = std::find_if(maps_.begin(), maps_.end(), [mapName](const ParsedMap& candidate) { return candidate.name == mapName; });
    return map == maps_.end() ? BinaryResult{ Status::MapUnavailable, {}, {}, {} } : ReadImageLocked(map->iconPath);
}

WebRadarMaps::BinaryResult WebRadarMaps::Provider::GetLayer(const std::string_view mapName, const std::string_view layerId)
{
    if (!IsSafeSegment(layerId))
        return { Status::MapUnavailable, {}, {}, {} };
    std::lock_guard lock(mutex_);
    const auto result = GetMapLocked(mapName);
    if (result.status != Status::Available)
        return { result.status, {}, {}, {} };
    const auto map = std::find_if(maps_.begin(), maps_.end(), [mapName](const ParsedMap& candidate) { return candidate.name == mapName; });
    if (map == maps_.end())
        return { Status::MapUnavailable, {}, {}, {} };
    const auto layer = std::find_if(map->layers.begin(), map->layers.end(), [layerId](const ParsedMap::Layer& candidate) {
        return candidate.id == layerId;
    });
    return layer == map->layers.end() ? BinaryResult{ Status::MapUnavailable, {}, {}, {} } : ReadImageLocked(layer->path);
}
