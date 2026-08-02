#include "../Features/WebRadarMapProvider.h"

#include <Windows.h>
#include <json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    using json = nlohmann::json;
    using WebRadarMaps::HttpResponse;
    constexpr std::wstring_view ManifestUrl = L"https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/data/available.json";
    constexpr std::wstring_view IconUrl = L"https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/de_test.png";
    constexpr std::wstring_view MainRadarUrl = L"https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/radars/de_test_radar_psd.png";
    constexpr std::string_view IconUrlUtf8 = "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/de_test.png";
    constexpr std::string_view MainRadarUrlUtf8 = "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/radars/de_test_radar_psd.png";

    void Check(const bool condition, const std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string(message));
    }

    std::vector<std::byte> Bytes(const std::string& text)
    {
        const auto* first = reinterpret_cast<const std::byte*>(text.data());
        return { first, first + text.size() };
    }

    std::vector<std::byte> Png()
    {
        return { std::byte{ 0x89 }, std::byte{ 'P' }, std::byte{ 'N' }, std::byte{ 'G' },
            std::byte{ 0x0D }, std::byte{ 0x0A }, std::byte{ 0x1A }, std::byte{ 0x0A } };
    }

    std::string SingleManifest(const std::string& iconUrl = std::string(IconUrlUtf8),
        const std::string& radarUrl = std::string(MainRadarUrlUtf8), const json& scale = 4.4)
    {
        return json{
            { "count", 1 },
            { "maps", {
                { "de_test", {
                    { "path", iconUrl },
                    { "radar_paths", { radarUrl } },
                    { "radar_info", {
                        { "pos_x", -2476 }, { "pos_y", 3239 }, { "scale", scale }, { "rotate", 1 }, { "zoom", 1.2 }
                    } }
                } }
            } }
        }.dump();
    }

    std::string NukeManifest()
    {
        return json{
            { "maps", {
                { "de_nuke", {
                    { "path", "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/de_nuke.png" },
                    { "radar_paths", {
                        "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/radars/de_nuke_lower_radar_psd.png",
                        "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/radars/de_nuke_radar_psd.png"
                    } },
                    { "radar_info", {
                        { "pos_x", -3453 }, { "pos_y", 2887 }, { "scale", 7.0 },
                        { "verticalsections", {
                            { "lower", { { "AltitudeMin", -10000 }, { "AltitudeMax", -495 } } },
                            { "default", { { "AltitudeMin", -495 }, { "AltitudeMax", 10000 } } }
                        } }
                    } }
                } }
            } }
        }.dump();
    }

    struct MockServer
    {
        std::mutex mutex;
        std::unordered_map<std::wstring, int> calls;
        std::string manifest = SingleManifest();
        unsigned manifestStatus = 200;
        std::vector<std::byte> image = Png();

        HttpResponse Fetch(const std::wstring_view url, const std::size_t)
        {
            std::lock_guard lock(mutex);
            ++calls[std::wstring(url)];
            if (url == ManifestUrl)
                return { manifestStatus, "application/json", Bytes(manifest) };
            return { 200, "image/png", image };
        }

        int Calls(const std::wstring_view url)
        {
            std::lock_guard lock(mutex);
            return calls[std::wstring(url)];
        }
    };

    std::filesystem::path TestRoot(const std::string_view name)
    {
        return std::filesystem::temp_directory_path()
            / ("DragonBurnWebRadarContracts-" + std::to_string(GetCurrentProcessId()) + "-" + std::string(name));
    }

    void ResetRoot(const std::filesystem::path& root)
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root, error);
        Check(!error, "could not create temporary cache root");
    }

    void TestSingleLayer()
    {
        const auto root = TestRoot("single");
        ResetRoot(root);
        MockServer mock;
        WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
        const auto result = provider.GetMap("de_test");
        Check(result.status == WebRadarMaps::Status::Available, "single-layer map unavailable");
        Check(result.metadata.layers.size() == 1 && result.metadata.layers[0].id == "main", "single layer was not normalized to main");
        std::filesystem::remove_all(root);
    }

    void TestVerticalSections()
    {
        const auto root = TestRoot("vertical");
        ResetRoot(root);
        MockServer mock;
        mock.manifest = NukeManifest();
        WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
        const auto result = provider.GetMap("de_nuke");
        Check(result.status == WebRadarMaps::Status::Available && result.metadata.layers.size() == 2, "Nuke layers unavailable");
        Check(result.metadata.layers[0].id == "lower" && result.metadata.layers[0].maxZ == -495, "lower range mismatch");
        Check(result.metadata.layers[1].id == "main" && result.metadata.layers[1].minZ == -495, "AltitudeMax boundary did not enter next layer");
        Check(result.metadata.layers[0].maxZ <= result.metadata.layers[1].minZ, "vertical ranges overlap");
        std::filesystem::remove_all(root);
    }

    void TestManifestFailures()
    {
        const std::vector<std::pair<std::string, std::function<void(MockServer&)>>> cases{
            { "status", [](MockServer& mock) { mock.manifestStatus = 503; } },
            { "oversize", [](MockServer& mock) { mock.manifest.assign(4U * 1024U * 1024U + 1U, 'x'); } },
            { "nonfinite", [](MockServer& mock) { mock.manifest = SingleManifest(std::string(IconUrlUtf8), std::string(MainRadarUrlUtf8), nullptr); } },
            { "badhost", [](MockServer& mock) { mock.manifest = SingleManifest("https://example.com/icon.png"); } },
            { "badpath", [](MockServer& mock) { mock.manifest = SingleManifest("https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/../secret.png"); } },
        };
        for (const auto& [name, configure] : cases)
        {
            const auto root = TestRoot(name);
            ResetRoot(root);
            MockServer mock;
            configure(mock);
            WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
            const auto result = provider.GetMap("de_test");
            Check(result.status != WebRadarMaps::Status::Available, name + " was accepted");
            if (name == "status" || name == "oversize")
                Check(!std::filesystem::exists(root / "available.json"), name + " wrote an invalid manifest");
            std::filesystem::remove_all(root);
        }

        const auto root = TestRoot("layers");
        ResetRoot(root);
        MockServer mock;
        json manifest = json::parse(SingleManifest());
        auto& paths = manifest["maps"]["de_test"]["radar_paths"];
        manifest["maps"]["de_valid"] = manifest["maps"]["de_test"];
        manifest["maps"]["de_valid"]["path"] = "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/de_valid.png";
        manifest["maps"]["de_valid"]["radar_paths"] = {
            "https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/radars/de_valid_radar_psd.png"
        };
        paths = json::array();
        for (int index = 0; index < 9; ++index)
            paths.push_back("https://raw.githubusercontent.com/MurkyYT/cs2-map-icons/main/images/radars/de_test_" + std::to_string(index) + "_radar_psd.png");
        mock.manifest = manifest.dump();
        WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
        Check(provider.GetMap("de_test").status == WebRadarMaps::Status::MapUnavailable, "more than eight layers accepted");
        std::filesystem::remove_all(root);
    }

    void TestImageFailures()
    {
        const std::vector<std::pair<std::string, std::vector<std::byte>>> cases{
            { "signature", Bytes("not an image") },
            { "image-limit", std::vector<std::byte>(16U * 1024U * 1024U + 1U, std::byte{ 0 }) },
        };
        for (const auto& [name, image] : cases)
        {
            const auto root = TestRoot(name);
            ResetRoot(root);
            MockServer mock;
            mock.image = image;
            WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
            Check(provider.GetMap("de_test").status == WebRadarMaps::Status::Error, name + " was accepted");
            Check(!std::filesystem::exists(root / "assets" / "de_test" / "icon.png"), name + " wrote an invalid image");
            std::filesystem::remove_all(root);
        }
    }

    void TestStaleFallback()
    {
        const auto root = TestRoot("stale");
        ResetRoot(root);
        MockServer initial;
        {
            WebRadarMaps::Provider provider(root, [&initial](const auto url, const auto limit) { return initial.Fetch(url, limit); });
            Check(provider.GetMap("de_test").status == WebRadarMaps::Status::Available, "initial manifest unavailable");
        }
        std::filesystem::last_write_time(root / "available.json",
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(13));
        MockServer failing;
        failing.manifestStatus = 503;
        WebRadarMaps::Provider fallback(root, [&failing](const auto url, const auto limit) { return failing.Fetch(url, limit); });
        const auto result = fallback.GetMap("de_test");
        Check(result.status == WebRadarMaps::Status::Available && result.metadata.stale, "stale manifest fallback not marked");
        std::filesystem::remove_all(root);
    }

    void TestConcurrentCoalescing()
    {
        const auto root = TestRoot("concurrent");
        ResetRoot(root);
        MockServer mock;
        WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
        WebRadarMaps::BinaryResult first;
        WebRadarMaps::BinaryResult second;
        std::thread one([&] { first = provider.GetLayer("de_test", "main"); });
        std::thread two([&] { second = provider.GetLayer("de_test", "main"); });
        one.join();
        two.join();
        Check(first.status == WebRadarMaps::Status::Available && second.status == WebRadarMaps::Status::Available, "concurrent layer request failed");
        Check(mock.Calls(MainRadarUrl) == 1, "concurrent layer request fetched more than once");
        std::filesystem::remove_all(root);
    }

    void TestPathBoundaries()
    {
        const auto root = TestRoot("paths");
        ResetRoot(root);
        const auto outside = root.parent_path() / "DragonBurnWebRadarContracts-sentinel.txt";
        {
            std::ofstream output(outside, std::ios::binary | std::ios::trunc);
            output << "unchanged";
        }
        MockServer mock;
        WebRadarMaps::Provider provider(root, [&mock](const auto url, const auto limit) { return mock.Fetch(url, limit); });
        Check(provider.GetMap("../").status == WebRadarMaps::Status::MapUnavailable, "parent traversal accepted");
        Check(provider.GetMap("de_test%2fsecret").status == WebRadarMaps::Status::MapUnavailable, "encoded slash accepted");
        Check(provider.GetLayer("de_test", "missing").status == WebRadarMaps::Status::MapUnavailable, "missing layer accepted");
        {
            std::ifstream input(outside, std::ios::binary);
            std::string value;
            input >> value;
            Check(value == "unchanged", "path validation modified a file outside cache root");
        }
        std::filesystem::remove(outside);
        std::filesystem::remove_all(root);
    }
}

int main()
{
    try
    {
        TestSingleLayer();
        TestVerticalSections();
        TestManifestFailures();
        TestImageFailures();
        TestStaleFallback();
        TestConcurrentCoalescing();
        TestPathBoundaries();
        std::cout << "Web radar contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Web radar contract failure: " << error.what() << '\n';
        return 1;
    }
}
