#include "DriverMemoryBackend.h"

#include <cstring>
#include <string>

namespace Protocol = DragonBurn::Protocol;

namespace
{
    std::string Win32ErrorMessage(const char* operation, const DWORD error)
    {
        return std::string(operation) + " failed with Win32 error " + std::to_string(error);
    }

    constexpr SIZE_T MaxDriverBatchBufferSize =
        sizeof(Protocol::BatchReadHeader) +
        MaxBatchMemoryRequests * sizeof(Protocol::BatchReadRequest) +
        MaxBatchMemoryOutputSize;
}

DriverMemoryBackend::~DriverMemoryBackend()
{
    Shutdown();
}

MemoryBackendKind DriverMemoryBackend::Kind() const noexcept
{
    return MemoryBackendKind::Driver;
}

BackendInitializationResult DriverMemoryBackend::Initialize()
{
    Shutdown();

    Protocol::DeviceNames deviceNames{};
    if (!Protocol::BuildLocalDeviceNames(&deviceNames))
    {
        return {
            BackendInitializationStatus::Failed,
            "Failed to derive the DragonBurn driver device name"
        };
    }

    driver_ = CreateFileW(
        deviceNames.userPath,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (driver_ == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        driver_ = nullptr;
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return { BackendInitializationStatus::NotPresent, {} };

        return {
            BackendInitializationStatus::Failed,
            Win32ErrorMessage("Opening the DragonBurn driver device", error)
        };
    }

    try
    {
        batchScratch_.resize(MaxDriverBatchBufferSize);
    }
    catch (...)
    {
        Shutdown();
        return {
            BackendInitializationStatus::Failed,
            "Allocating the DragonBurn driver batch buffer failed"
        };
    }

    return { BackendInitializationStatus::Ready, {} };
}

void DriverMemoryBackend::Shutdown() noexcept
{
    if (driver_ != nullptr)
        CloseHandle(driver_);

    driver_ = nullptr;
    processId_ = 0;
    std::vector<std::byte>().swap(batchScratch_);
}

DWORD DriverMemoryBackend::GetProcessId(const wchar_t* processName)
{
    if (driver_ == nullptr || processName == nullptr)
        return 0;

    Protocol::ProcessIdPacket packet{};
    if (wcsncpy_s(packet.name, processName, _TRUNCATE) != 0)
        return 0;

    const BOOL result = DeviceIoControl(
        driver_,
        Protocol::IoctlGetPid,
        &packet,
        sizeof(packet),
        &packet,
        sizeof(packet),
        nullptr,
        nullptr);
    return result == TRUE ? packet.pid : 0;
}

bool DriverMemoryBackend::Attach(const DWORD processId)
{
    if (driver_ == nullptr || processId == 0)
        return false;

    Protocol::Request request{};
    request.process_id = ULongToHandle(processId);

    const BOOL result = DeviceIoControl(
        driver_,
        Protocol::IoctlAttach,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        nullptr,
        nullptr);
    if (result != TRUE)
        return false;

    processId_ = processId;
    return true;
}

DWORD64 DriverMemoryBackend::GetModuleBase(const wchar_t* moduleName)
{
    if (driver_ == nullptr || processId_ == 0 || moduleName == nullptr)
        return 0;

    Protocol::ModulePacket packet{};
    packet.pid = processId_;
    if (wcsncpy_s(packet.moduleName, moduleName, _TRUNCATE) != 0)
        return 0;

    const BOOL result = DeviceIoControl(
        driver_,
        Protocol::IoctlGetModuleBase,
        &packet,
        sizeof(packet),
        &packet,
        sizeof(packet),
        nullptr,
        nullptr);
    return result == TRUE ? packet.baseAddress : 0;
}

bool DriverMemoryBackend::Read(
    const DWORD64 address,
    const std::span<std::byte> output,
    const MemoryReadPolicy)
{
    if (driver_ == nullptr || processId_ == 0 || address == 0 || output.empty() ||
        output.data() == nullptr || output.size() > scalarScratch_.size())
    {
        return false;
    }

    Protocol::Request request{};
    request.process_id = ULongToHandle(processId_);
    request.target = reinterpret_cast<PVOID>(address);
    request.buffer = scalarScratch_.data();
    request.size = output.size();

    const BOOL result = DeviceIoControl(
        driver_,
        Protocol::IoctlRead,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        nullptr,
        nullptr);
    if (result != TRUE)
        return false;

    std::memcpy(output.data(), scalarScratch_.data(), output.size());
    return true;
}

MemoryBatchReadResult DriverMemoryBackend::ReadBatch(
    const std::span<const MemoryReadRequest> requests,
    const std::span<std::byte> output,
    const MemoryReadPolicy,
    const std::span<std::uint8_t> requestSucceeded)
{
    if (output.data() != nullptr && !output.empty())
        SecureZeroMemory(output.data(), output.size());
    if (requestSucceeded.data() != nullptr && !requestSucceeded.empty())
        SecureZeroMemory(requestSucceeded.data(), requestSucceeded.size());

    if (driver_ == nullptr || processId_ == 0 || requests.empty() || output.empty() ||
        output.data() == nullptr || requests.size() > MaxBatchMemoryRequests ||
        output.size() > MaxBatchMemoryOutputSize ||
        (!requestSucceeded.empty() && requestSucceeded.size() != requests.size()))
    {
        return {};
    }

    const SIZE_T requestStructureSize = sizeof(Protocol::BatchReadHeader) +
        requests.size() * sizeof(Protocol::BatchReadRequest);
    const SIZE_T totalBufferSize = requestStructureSize + output.size();
    if (totalBufferSize < requestStructureSize || totalBufferSize > batchScratch_.size() ||
        totalBufferSize > MAXDWORD)
    {
        return {};
    }

    SecureZeroMemory(batchScratch_.data(), totalBufferSize);
    auto* header = reinterpret_cast<Protocol::BatchReadHeader*>(batchScratch_.data());
    auto* batchRequests = reinterpret_cast<Protocol::BatchReadRequest*>(header + 1);
    header->process_id = ULongToHandle(processId_);
    header->num_requests = static_cast<UINT32>(requests.size());
    header->successful_requests = 0;
    header->total_buffer_size = output.size();

    SIZE_T outputOffset = 0;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        batchRequests[index].address = requests[index].address;
        batchRequests[index].size = requests[index].size;
        batchRequests[index].offset_in_buffer = outputOffset;
        batchRequests[index].succeeded = 0;
        outputOffset += requests[index].size;
    }

    if (outputOffset != output.size())
        return {};

    DWORD bytesReturned = 0;
    const BOOL result = DeviceIoControl(
        driver_,
        Protocol::IoctlBatchRead,
        batchScratch_.data(),
        static_cast<DWORD>(totalBufferSize),
        batchScratch_.data(),
        static_cast<DWORD>(totalBufferSize),
        &bytesReturned,
        nullptr);
    if (result != TRUE || bytesReturned != totalBufferSize)
        return {};

    if (header->process_id != ULongToHandle(processId_) ||
        header->num_requests != requests.size() ||
        header->total_buffer_size != output.size() ||
        header->successful_requests > requests.size())
    {
        return {};
    }

    SIZE_T successfulRequests = 0;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        if (batchRequests[index].succeeded > 1)
            return {};

        successfulRequests += batchRequests[index].succeeded;
    }

    if (successfulRequests != header->successful_requests)
        return {};

    std::memcpy(output.data(), batchScratch_.data() + requestStructureSize, output.size());
    if (!requestSucceeded.empty())
    {
        for (size_t index = 0; index < requests.size(); ++index)
            requestSucceeded[index] = static_cast<std::uint8_t>(batchRequests[index].succeeded);
    }

    return { true, successfulRequests };
}
