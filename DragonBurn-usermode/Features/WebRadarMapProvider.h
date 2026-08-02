#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace WebRadarMaps
{
    struct HttpResponse
    {
        unsigned status = 0;
        std::string contentType;
        std::vector<std::byte> body;
    };

    using FetchFunction = std::function<HttpResponse(std::wstring_view url, std::size_t maxBytes)>;

    struct MapLayer
    {
        std::string id;
        double minZ = 0.0;
        double maxZ = 0.0;
        std::string image;
    };

    struct MapMetadata
    {
        std::string map;
        std::string icon;
        double positionX = 0.0;
        double positionY = 0.0;
        double scale = 0.0;
        double rotate = 0.0;
        double zoom = 0.0;
        std::vector<MapLayer> layers;
        bool stale = false;
    };

    enum class Status
    {
        Available,
        MapUnavailable,
        Error
    };

    struct MapResult
    {
        Status status = Status::Error;
        MapMetadata metadata;
    };

    struct BinaryResult
    {
        Status status = Status::Error;
        std::string contentType;
        std::string etag;
        std::vector<std::byte> body;
    };

    class Provider
    {
    public:
        explicit Provider(std::filesystem::path cacheRoot);
        Provider(std::filesystem::path cacheRoot, FetchFunction fetch);
        ~Provider();

        MapResult GetMap(std::string_view mapName);
        BinaryResult GetIcon(std::string_view mapName);
        BinaryResult GetLayer(std::string_view mapName, std::string_view layerId);

    private:
        struct ParsedMap;

        MapResult GetMapLocked(std::string_view mapName);
        bool LoadManifestLocked(bool& stale);
        bool ParseManifest(const std::vector<std::byte>& body);
        bool DownloadImageLocked(const std::wstring& url, const std::filesystem::path& path);
        BinaryResult ReadImageLocked(const std::filesystem::path& path) const;

        std::filesystem::path cacheRoot_;
        FetchFunction fetch_;
        std::mutex mutex_;
        std::vector<ParsedMap> maps_;
        bool manifestLoaded_ = false;
        bool manifestStale_ = false;
    };
}
