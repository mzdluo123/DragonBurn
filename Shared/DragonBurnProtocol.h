#pragma once

#if defined(_KERNEL_MODE)
#include <ntddk.h>
#else
#include <Windows.h>
#endif

namespace DragonBurn
{
namespace Protocol
{
    constexpr ULONG DeviceType = 0x8000;

    constexpr ULONG IoctlAttach = CTL_CODE(DeviceType, 0x4452, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    constexpr ULONG IoctlRead = CTL_CODE(DeviceType, 0x4453, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    constexpr ULONG IoctlGetModuleBase = CTL_CODE(DeviceType, 0x4454, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    constexpr ULONG IoctlGetPid = CTL_CODE(DeviceType, 0x4455, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    constexpr ULONG IoctlBatchRead = CTL_CODE(DeviceType, 0x4457, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

    constexpr SIZE_T HostNameCapacity = 256;
    constexpr SIZE_T DevicePathCapacity = 64;

    struct DeviceNames
    {
        WCHAR userPath[DevicePathCapacity];
        WCHAR kernelPath[DevicePathCapacity];
        WCHAR dosPath[DevicePathCapacity];
        WCHAR driverPath[DevicePathCapacity];
    };

    inline UINT64 HashHostName(const WCHAR* hostName, SIZE_T length, UINT64 seed)
    {
        if (hostName == nullptr || length == 0)
            return 0;

        UINT64 hash = seed;
        for (SIZE_T index = 0; index < length; ++index)
        {
            UINT16 character = static_cast<UINT16>(hostName[index]);
            if (character >= L'A' && character <= L'Z')
                character = static_cast<UINT16>(character + (L'a' - L'A'));

            hash ^= static_cast<UINT8>(character & 0xFF);
            hash *= 0x100000001B3ULL;
            hash ^= static_cast<UINT8>(character >> 8);
            hash *= 0x100000001B3ULL;
        }

        hash ^= hash >> 33;
        hash *= 0xFF51AFD7ED558CCDULL;
        hash ^= hash >> 33;
        hash *= 0xC4CEB9FE1A85EC53ULL;
        hash ^= hash >> 33;
        return hash;
    }

    inline bool FormatDevicePath(
        const WCHAR* prefix,
        SIZE_T prefixLength,
        UINT64 firstHalf,
        UINT64 secondHalf,
        WCHAR* output,
        SIZE_T capacity)
    {
        constexpr WCHAR HexDigits[] = L"0123456789abcdef";
        constexpr SIZE_T TokenLength = 32;
        if (prefix == nullptr || output == nullptr ||
            capacity < prefixLength + TokenLength + 1)
        {
            return false;
        }

        for (SIZE_T index = 0; index < prefixLength; ++index)
            output[index] = prefix[index];

        SIZE_T offset = prefixLength;
        for (int shift = 60; shift >= 0; shift -= 4)
            output[offset++] = HexDigits[(firstHalf >> shift) & 0xF];
        for (int shift = 60; shift >= 0; shift -= 4)
            output[offset++] = HexDigits[(secondHalf >> shift) & 0xF];
        output[offset] = L'\0';
        return true;
    }

    inline bool BuildDeviceNames(const WCHAR* hostName, SIZE_T length, DeviceNames* names)
    {
        if (names == nullptr)
            return false;

        const UINT64 firstHalf = HashHostName(hostName, length, 0x9E3779B185EBCA87ULL);
        const UINT64 secondHalf = HashHostName(hostName, length, 0xD6E8FEB86659FD93ULL);
        if (firstHalf == 0 || secondHalf == 0)
            return false;

        constexpr WCHAR UserPrefix[] = L"\\\\.\\";
        constexpr WCHAR KernelPrefix[] = L"\\Device\\";
        constexpr WCHAR DosPrefix[] = L"\\DosDevices\\";
        constexpr WCHAR DriverPrefix[] = L"\\Driver\\";

        return FormatDevicePath(UserPrefix, RTL_NUMBER_OF(UserPrefix) - 1,
                   firstHalf, secondHalf, names->userPath, DevicePathCapacity) &&
            FormatDevicePath(KernelPrefix, RTL_NUMBER_OF(KernelPrefix) - 1,
                   firstHalf, secondHalf, names->kernelPath, DevicePathCapacity) &&
            FormatDevicePath(DosPrefix, RTL_NUMBER_OF(DosPrefix) - 1,
                   firstHalf, secondHalf, names->dosPath, DevicePathCapacity) &&
            FormatDevicePath(DriverPrefix, RTL_NUMBER_OF(DriverPrefix) - 1,
                   firstHalf, secondHalf, names->driverPath, DevicePathCapacity);
    }

#if !defined(_KERNEL_MODE)
    inline bool BuildLocalDeviceNames(DeviceNames* names)
    {
        WCHAR hostName[HostNameCapacity]{};
        DWORD length = static_cast<DWORD>(HostNameCapacity);
        return GetComputerNameW(hostName, &length) != FALSE &&
            BuildDeviceNames(hostName, static_cast<SIZE_T>(length), names);
    }
#endif

    constexpr SIZE_T MaxSingleReadSize = 0x1000;
    constexpr ULONG MaxBatchRequests = 4096;
    constexpr SIZE_T MaxBatchOutputSize = 4 * 1024 * 1024;
    constexpr SIZE_T NameCapacity = 1024;

    struct Request
    {
        HANDLE process_id;
        PVOID target;
        PVOID buffer;
        SIZE_T size;
    };

    struct ProcessIdPacket
    {
        UINT32 pid;
        WCHAR name[NameCapacity];
    };

    struct ModulePacket
    {
        UINT32 pid;
        UINT64 baseAddress;
        SIZE_T size;
        WCHAR moduleName[NameCapacity];
    };

    struct BatchReadRequest
    {
        UINT64 address;
        SIZE_T size;
        SIZE_T offset_in_buffer;
        UINT32 succeeded;
    };

    struct BatchReadHeader
    {
        HANDLE process_id;
        UINT32 num_requests;
        UINT32 successful_requests;
        SIZE_T total_buffer_size;
    };

    static_assert(sizeof(Request) == 32, "Request ABI changed");
    static_assert(sizeof(ProcessIdPacket) == 2052, "ProcessIdPacket ABI changed");
    static_assert(sizeof(ModulePacket) == 2072, "ModulePacket ABI changed");
    static_assert(FIELD_OFFSET(BatchReadRequest, succeeded) == 24, "BatchReadRequest success offset changed");
    static_assert(sizeof(BatchReadRequest) == 32, "BatchReadRequest ABI changed");
    static_assert(FIELD_OFFSET(BatchReadHeader, successful_requests) == 12, "BatchReadHeader success offset changed");
    static_assert(FIELD_OFFSET(BatchReadHeader, total_buffer_size) == 16, "BatchReadHeader size offset changed");
    static_assert(sizeof(BatchReadHeader) == 24, "BatchReadHeader ABI changed");
}
}
