#pragma once
#include <Windows.h>
#include <utility>
#include <vector>

#include "../../Shared/DragonBurnProtocol.h"

class MemoryMgr
{
public:
    MemoryMgr();
    ~MemoryMgr();
    bool ConnectDriver();
    bool DisconnectDriver();
    bool Attach(const DWORD pid);
    DWORD64 GetModuleBase(const wchar_t* moduleName);
    DWORD GetProcessID(const wchar_t* processName);
    //DWORD64 TraceAddress(DWORD64 baseAddress, std::vector<DWORD> offsets);
    bool BatchReadMemory(const std::vector<std::pair<DWORD64, SIZE_T>>& requests, void* output_buffer);
    // Reads valid ranges even when another request in the batch is stale.
    // Failed ranges are zeroed; returns true when at least one range succeeds.
    bool BatchReadMemoryBestEffort(const std::vector<std::pair<DWORD64, SIZE_T>>& requests, void* output_buffer);

    template <typename ReadType>
    bool ReadMemory(DWORD64 address, ReadType& value, SIZE_T size = sizeof(ReadType))
    {
        if (kernelDriver != nullptr && ProcessID != 0)
        {
            if (address == 0 || address >= 0x7FFFFFFFFFFF || size == 0 ||
                size > DragonBurn::Protocol::MaxSingleReadSize) {
                return false;
            }

            if (address + size < address) {
                return false;
            }

            DragonBurn::Protocol::Request readRequest;
            readRequest.process_id = ULongToHandle(ProcessID);
            readRequest.target = reinterpret_cast<PVOID>(address);
            readRequest.buffer = &value;
            readRequest.size = size;

            BOOL result = DeviceIoControl(kernelDriver,
                DragonBurn::Protocol::IoctlRead,
                &readRequest,
                sizeof(readRequest),
                &readRequest,
                sizeof(readRequest),
                nullptr,
                nullptr);
            return result == TRUE;
        }
        return false;
    }

    template<typename T>
    bool BatchReadStructured(const std::vector<DWORD64>& addresses, std::vector<T>& results) {
        if (addresses.empty()) return false;

        std::vector<std::pair<DWORD64, SIZE_T>> requests;
        requests.reserve(addresses.size());

        for (DWORD64 addr : addresses) {
            requests.emplace_back(addr, sizeof(T));
        }

        results.resize(addresses.size());
        return BatchReadMemory(requests, results.data());
    }

private:
    DWORD ProcessID = 0;
    HANDLE kernelDriver = nullptr;

};

inline MemoryMgr memoryManager;