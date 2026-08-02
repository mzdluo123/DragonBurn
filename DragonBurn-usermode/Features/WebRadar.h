#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CEntity;

struct WebRadarConfig
{
    std::string listenAddress;
    std::uint16_t port = 0;
    std::filesystem::path mapCacheRoot;
};

namespace WebRadar
{
    bool Start(const WebRadarConfig& config);
    void Publish(const CEntity& localEntity,
        const std::vector<std::pair<int, CEntity>>& entities,
        DWORD gameTick,
        std::string_view mapName);
    void Invalidate();
    void Stop();
}
