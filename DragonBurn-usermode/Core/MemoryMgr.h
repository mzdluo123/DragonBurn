#pragma once

#include <Windows.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include "MemoryBackend.h"

class MemoryMgr
{
public:
    MemoryMgr() = default;
    ~MemoryMgr();

    MemoryMgr(const MemoryMgr&) = delete;
    MemoryMgr& operator=(const MemoryMgr&) = delete;

    bool Initialize();
    void Shutdown() noexcept;
    MemoryBackendKind GetBackendKind() const noexcept;
    const std::string& GetLastError() const noexcept;

    bool Attach(DWORD processId);
    DWORD64 GetModuleBase(const wchar_t* moduleName);
    DWORD GetProcessID(const wchar_t* processName);

    bool BatchReadMemory(
        std::span<const MemoryReadRequest> requests,
        std::span<std::byte> output,
        MemoryReadPolicy policy = MemoryReadPolicy::BypassDataCache);

    // Reads every valid range once and reports per-request success when requested.
    MemoryBatchReadResult BatchReadMemoryBestEffort(
        std::span<const MemoryReadRequest> requests,
        std::span<std::byte> output,
        MemoryReadPolicy policy = MemoryReadPolicy::BypassDataCache,
        std::span<std::uint8_t> requestSucceeded = {});

    template <typename ReadType>
    bool ReadMemory(
        DWORD64 address,
        ReadType& value,
        SIZE_T size = sizeof(ReadType),
        MemoryReadPolicy policy = MemoryReadPolicy::BypassDataCache)
    {
        return ReadMemoryBytes(
            address,
            std::span<std::byte>(reinterpret_cast<std::byte*>(std::addressof(value)), size),
            policy);
    }

private:
    bool ReadMemoryBytes(
        DWORD64 address,
        std::span<std::byte> output,
        MemoryReadPolicy policy);
    bool ValidateBatch(
        std::span<const MemoryReadRequest> requests,
        std::span<std::byte> output) const noexcept;

    std::unique_ptr<IMemoryBackend> backend_;
    DWORD processId_ = 0;
    MemoryBackendKind backendKind_ = MemoryBackendKind::None;
    std::string lastError_;
};

inline MemoryMgr memoryManager;