#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "MemoryBackend.h"

class MemProcFsMemoryBackend final : public IMemoryBackend
{
public:
    MemProcFsMemoryBackend();
    ~MemProcFsMemoryBackend() override;

    MemProcFsMemoryBackend(const MemProcFsMemoryBackend&) = delete;
    MemProcFsMemoryBackend& operator=(const MemProcFsMemoryBackend&) = delete;

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
    bool ReadBatch(
        std::span<const MemoryReadRequest> requests,
        std::span<std::byte> output,
        MemoryReadPolicy policy) override;

private:
    struct Api;

    bool ResetScatter(MemoryReadPolicy policy) noexcept;

    std::unique_ptr<Api> api_;
    HMODULE vmmModule_ = nullptr;
    HMODULE leechCoreModule_ = nullptr;
    DLL_DIRECTORY_COOKIE dllDirectoryCookie_ = nullptr;
    void* vmmHandle_ = nullptr;
    void* scatterHandle_ = nullptr;
    DWORD processId_ = 0;
    std::array<std::byte, MaxSingleMemoryReadSize> scalarScratch_{};
    std::vector<std::byte> batchScratch_;
    std::vector<DWORD> batchBytesRead_;
    std::vector<SIZE_T> batchOffsets_;
};
