#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "MemoryBackend.h"

class DriverMemoryBackend final : public IMemoryBackend
{
public:
    DriverMemoryBackend() = default;
    ~DriverMemoryBackend() override;

    DriverMemoryBackend(const DriverMemoryBackend&) = delete;
    DriverMemoryBackend& operator=(const DriverMemoryBackend&) = delete;

    MemoryBackendKind Kind() const noexcept override;
    BackendInitializationResult Initialize() override;
    void Shutdown() noexcept override;
    DWORD GetProcessId(const wchar_t* processName) override;
    bool Attach(DWORD processId) override;
    DWORD64 GetModuleBase(const wchar_t* moduleName) override;
    bool Read(
        DWORD64 address,
        std::span<std::byte> output,
        MemoryReadPolicy policy) override;
    MemoryBatchReadResult ReadBatch(
        std::span<const MemoryReadRequest> requests,
        std::span<std::byte> output,
        MemoryReadPolicy policy,
        std::span<std::uint8_t> requestSucceeded) override;

private:
    HANDLE driver_ = nullptr;
    DWORD processId_ = 0;
    std::array<std::byte, MaxSingleMemoryReadSize> scalarScratch_{};
    std::vector<std::byte> batchScratch_;
};
