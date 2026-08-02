#include "MemoryMgr.h"

namespace Protocol = DragonBurn::Protocol;

MemoryMgr::MemoryMgr()
{
    ProcessID = 0;
    kernelDriver = nullptr;
}

MemoryMgr::~MemoryMgr()
{
    DisconnectDriver();
    ProcessID = 0;
    kernelDriver = nullptr;
}

bool MemoryMgr::ConnectDriver()
{
    Protocol::DeviceNames deviceNames{};
    if (!Protocol::BuildLocalDeviceNames(&deviceNames))
        return false;

    kernelDriver = CreateFileW(deviceNames.userPath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (kernelDriver == INVALID_HANDLE_VALUE)
    {
        kernelDriver = nullptr;
        return false;
    }
    return true;
}

bool MemoryMgr::DisconnectDriver()
{
    if (kernelDriver != nullptr)
    {
        BOOL result = CloseHandle(kernelDriver);
        kernelDriver = nullptr;
        return result == TRUE;
    }
    return false;
}

bool MemoryMgr::Attach(const DWORD pid)
{
    if (pid == 0 || kernelDriver == nullptr)
        return false;


    Protocol::Request attachRequest;
    attachRequest.process_id = ULongToHandle(pid);
    attachRequest.target = nullptr;
    attachRequest.buffer = nullptr;
    attachRequest.size = 0;

    BOOL result = DeviceIoControl(kernelDriver,
        Protocol::IoctlAttach,
        &attachRequest,
        sizeof(attachRequest),
        &attachRequest,
        sizeof(attachRequest),
        nullptr,
        nullptr);

    if (result == TRUE)
    {
        ProcessID = pid;
        return true;
    }
    return false;
}

DWORD MemoryMgr::GetProcessID(const wchar_t* processName)
{
    if (kernelDriver == nullptr || processName == nullptr)
        return 0;

    Protocol::ProcessIdPacket packet{};
    if (wcsncpy_s(packet.name, processName, _TRUNCATE) != 0)
        return 0;

    const BOOL result = DeviceIoControl(
        kernelDriver,
        Protocol::IoctlGetPid,
        &packet,
        sizeof(packet),
        &packet,
        sizeof(packet),
        nullptr,
        nullptr);

    return result == TRUE ? packet.pid : 0;
}
DWORD64 MemoryMgr::GetModuleBase(const wchar_t* moduleName)
{
    if (kernelDriver == nullptr || ProcessID == 0 || moduleName == nullptr)
        return 0;

    Protocol::ModulePacket packet{};
    packet.pid = ProcessID;
    if (wcsncpy_s(packet.moduleName, moduleName, _TRUNCATE) != 0)
        return 0;

    const BOOL result = DeviceIoControl(
        kernelDriver,
        Protocol::IoctlGetModuleBase,
        &packet,
        sizeof(packet),
        &packet,
        sizeof(packet),
        nullptr,
        nullptr);

    return result == TRUE ? packet.baseAddress : 0;
}

/*
DWORD64 MemoryMgr::TraceAddress(DWORD64 baseAddress, std::vector<DWORD> offsets)
{
    if (kernelDriver == nullptr || ProcessID == 0)
        return 0;

    if (baseAddress == 0 || baseAddress >= 0x7FFFFFFFFFFF)
        return 0;

    uint64_t address = baseAddress;
    if (offsets.empty())
        return baseAddress;

    uint64_t buffer = 0;
    if (!ReadMemory(address, buffer))
        return 0;

    for (size_t i = 0; i < offsets.size() - 1; i++)
    {
        if (buffer == 0 || buffer >= 0x7FFFFFFFFFFF)
            return 0;

        address = buffer + offsets[i];

        if (address < buffer)
            return 0;

        if (!ReadMemory(address, buffer))
            return 0;
    }

    if (buffer == 0 || buffer >= 0x7FFFFFFFFFFF)
        return 0;

    uint64_t finalAddress = buffer + offsets.back();
    return (finalAddress < buffer) ? 0 : finalAddress; // Check overflow
}
*/

bool MemoryMgr::BatchReadMemory(const std::vector<std::pair<DWORD64, SIZE_T>>& requests, void* output_buffer)
{
    if (kernelDriver == nullptr || ProcessID == 0 || output_buffer == nullptr || requests.empty() ||
        requests.size() > Protocol::MaxBatchRequests)
    {
        return false;
    }

    SIZE_T outputDataSize = 0;
    for (const auto& request : requests)
    {
        if (request.first == 0 || request.second == 0 || request.second > Protocol::MaxSingleReadSize ||
            request.first + request.second < request.first ||
            request.second > Protocol::MaxBatchOutputSize - outputDataSize)
        {
            return false;
        }

        outputDataSize += request.second;
    }

    const SIZE_T requestStructureSize = sizeof(Protocol::BatchReadHeader) +
        requests.size() * sizeof(Protocol::BatchReadRequest);
    const SIZE_T totalBufferSize = requestStructureSize + outputDataSize;
    if (totalBufferSize < requestStructureSize || totalBufferSize > MAXDWORD)
        return false;

    std::vector<BYTE> operationBuffer(totalBufferSize);
    auto header = reinterpret_cast<Protocol::BatchReadHeader*>(operationBuffer.data());
    auto batchRequests = reinterpret_cast<Protocol::BatchReadRequest*>(header + 1);

    header->process_id = ULongToHandle(ProcessID);
    header->num_requests = static_cast<UINT32>(requests.size());
    header->total_buffer_size = outputDataSize;

    SIZE_T bufferOffset = 0;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        batchRequests[index].address = requests[index].first;
        batchRequests[index].size = requests[index].second;
        batchRequests[index].offset_in_buffer = bufferOffset;
        bufferOffset += requests[index].second;
    }

    const BOOL result = DeviceIoControl(
        kernelDriver,
        Protocol::IoctlBatchRead,
        operationBuffer.data(),
        static_cast<DWORD>(totalBufferSize),
        operationBuffer.data(),
        static_cast<DWORD>(totalBufferSize),
        nullptr,
        nullptr);

    if (result == TRUE)
    {
        const BYTE* outputStart = operationBuffer.data() + requestStructureSize;
        memcpy(output_buffer, outputStart, outputDataSize);
    }

    return result == TRUE;
}