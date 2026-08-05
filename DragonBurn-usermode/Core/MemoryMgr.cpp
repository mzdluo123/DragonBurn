#include "MemoryMgr.h"

#include <limits>
#include <utility>

#include "DriverMemoryBackend.h"
#include "MemProcFsMemoryBackend.h"
#include "../Helpers/Logger.h"

MemoryMgr::~MemoryMgr()
{
    Shutdown();
}

bool MemoryMgr::Initialize()
{
    Shutdown();
    lastError_.clear();

    auto driver = std::make_unique<DriverMemoryBackend>();
    BackendInitializationResult result = driver->Initialize();
    if (result.status == BackendInitializationStatus::Ready)
    {
        backendKind_ = driver->Kind();
        backend_ = std::move(driver);
        return true;
    }

    driver->Shutdown();
    driver.reset();

    if (result.status == BackendInitializationStatus::Failed)
    {
        lastError_ = result.error.empty()
            ? "DragonBurn driver initialization failed"
            : std::move(result.error);
        return false;
    }

    Log::Info("DragonBurn driver is not loaded; initializing MemProcFS FPGA backend");

    auto memProcFs = std::make_unique<MemProcFsMemoryBackend>();
    result = memProcFs->Initialize();
    if (result.status == BackendInitializationStatus::Ready)
    {
        backendKind_ = memProcFs->Kind();
        backend_ = std::move(memProcFs);
        return true;
    }

    memProcFs->Shutdown();
    lastError_ = result.error.empty()
        ? "MemProcFS FPGA backend initialization failed"
        : std::move(result.error);
    return false;
}

void MemoryMgr::Shutdown() noexcept
{
    if (backend_)
        backend_->Shutdown();

    backend_.reset();
    processId_ = 0;
    backendKind_ = MemoryBackendKind::None;
}

MemoryBackendKind MemoryMgr::GetBackendKind() const noexcept
{
    return backendKind_;
}

const std::string& MemoryMgr::GetLastError() const noexcept
{
    return lastError_;
}

bool MemoryMgr::Attach(const DWORD processId)
{
    if (!backend_ || processId == 0 || !backend_->Attach(processId))
        return false;

    processId_ = processId;
    return true;
}

DWORD64 MemoryMgr::GetModuleBase(const wchar_t* moduleName)
{
    if (!backend_ || processId_ == 0 || moduleName == nullptr)
        return 0;

    return backend_->GetModuleBase(moduleName);
}

DWORD MemoryMgr::GetProcessID(const wchar_t* processName)
{
    if (!backend_ || processName == nullptr)
        return 0;

    return backend_->GetProcessId(processName);
}

bool MemoryMgr::ReadMemoryBytes(
    const DWORD64 address,
    const std::span<std::byte> output,
    const MemoryReadPolicy policy)
{
    constexpr DWORD64 HighestUserAddress = 0x7FFFFFFFFFFFULL;
    if (!backend_ || processId_ == 0 || address == 0 || address >= HighestUserAddress ||
        output.data() == nullptr || output.empty() || output.size() > MaxSingleMemoryReadSize ||
        address > (std::numeric_limits<DWORD64>::max)() - output.size())
    {
        return false;
    }

    return backend_->Read(address, output, policy);
}

bool MemoryMgr::ValidateBatch(
    const std::span<const MemoryReadRequest> requests,
    const std::span<std::byte> output) const noexcept
{
    if (!backend_ || processId_ == 0 || requests.empty() ||
        requests.size() > MaxBatchMemoryRequests || output.data() == nullptr || output.empty())
    {
        return false;
    }

    SIZE_T outputSize = 0;
    for (const MemoryReadRequest& request : requests)
    {
        if (request.address == 0 || request.size == 0 || request.size > MaxSingleMemoryReadSize ||
            request.address > (std::numeric_limits<DWORD64>::max)() - request.size ||
            request.size > MaxBatchMemoryOutputSize - outputSize)
        {
            return false;
        }

        outputSize += request.size;
    }

    return outputSize == output.size();
}

bool MemoryMgr::BatchReadMemory(
    const std::span<const MemoryReadRequest> requests,
    const std::span<std::byte> output,
    const MemoryReadPolicy policy)
{
    if (!ValidateBatch(requests, output))
        return false;

    const MemoryBatchReadResult result = backend_->ReadBatch(requests, output, policy, {});
    return result.completed && result.successfulRequests == requests.size();
}

MemoryBatchReadResult MemoryMgr::BatchReadMemoryBestEffort(
    const std::span<const MemoryReadRequest> requests,
    const std::span<std::byte> output,
    const MemoryReadPolicy policy,
    const std::span<std::uint8_t> requestSucceeded)
{
    if (requestSucceeded.data() != nullptr && !requestSucceeded.empty())
        SecureZeroMemory(requestSucceeded.data(), requestSucceeded.size());

    if ((!requestSucceeded.empty() && requestSucceeded.size() != requests.size()) ||
        !ValidateBatch(requests, output))
    {
        if (output.data() != nullptr && !output.empty())
            SecureZeroMemory(output.data(), output.size());
        return {};
    }

    const MemoryBatchReadResult result =
        backend_->ReadBatch(requests, output, policy, requestSucceeded);
    if (!result.completed)
    {
        SecureZeroMemory(output.data(), output.size());
        if (!requestSucceeded.empty())
            SecureZeroMemory(requestSucceeded.data(), requestSucceeded.size());
    }

    return result;
}