#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "../../Shared/DragonBurnProtocol.h"

enum class MemoryBackendKind
{
    None,
    Driver,
    MemProcFs
};

enum class BackendInitializationStatus
{
    Ready,
    NotPresent,
    Failed
};

enum class MemoryReadPolicy
{
    BypassDataCache,
    AllowDataCache
};

struct BackendInitializationResult
{
    BackendInitializationStatus status;
    std::string error;
};

struct MemoryReadRequest
{
    DWORD64 address;
    SIZE_T size;
};

struct MemoryBatchReadResult
{
    bool completed = false;
    SIZE_T successfulRequests = 0;
};

inline constexpr SIZE_T MaxSingleMemoryReadSize = 0x1000;
inline constexpr SIZE_T MaxBatchMemoryRequests = 4096;
inline constexpr SIZE_T MaxBatchMemoryOutputSize = 4 * 1024 * 1024;

static_assert(MaxSingleMemoryReadSize == DragonBurn::Protocol::MaxSingleReadSize);
static_assert(MaxBatchMemoryRequests == DragonBurn::Protocol::MaxBatchRequests);
static_assert(MaxBatchMemoryOutputSize == DragonBurn::Protocol::MaxBatchOutputSize);

class IMemoryBackend
{
public:
    virtual ~IMemoryBackend() = default;

    virtual MemoryBackendKind Kind() const noexcept = 0;
    virtual BackendInitializationResult Initialize() = 0;
    virtual void Shutdown() noexcept = 0;
    virtual DWORD GetProcessId(const wchar_t* processName) = 0;
    virtual bool Attach(DWORD processId) = 0;
    virtual DWORD64 GetModuleBase(const wchar_t* moduleName) = 0;
    virtual bool Read(
        DWORD64 address,
        std::span<std::byte> output,
        MemoryReadPolicy policy) = 0;
    virtual MemoryBatchReadResult ReadBatch(
        std::span<const MemoryReadRequest> requests,
        std::span<std::byte> output,
        MemoryReadPolicy policy,
        std::span<std::uint8_t> requestSucceeded) = 0;
};
