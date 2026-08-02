#include <ntifs.h>
#include <wdmsec.h>

#include "../Shared/DragonBurnProtocol.h"

extern "C" NTSTATUS NTAPI ZwQuerySystemInformation(
    ULONG systemInformationClass,
    PVOID systemInformation,
    ULONG systemInformationLength,
    PULONG returnLength);

extern "C" PVOID NTAPI PsGetProcessPeb(PEPROCESS process);

#ifndef MM_COPY_MEMORY_VIRTUAL
#define MM_COPY_MEMORY_VIRTUAL 0x1
#endif

extern "C" NTSTATUS NTAPI IoCreateDriver(
    PUNICODE_STRING driverName,
    PDRIVER_INITIALIZE initializationFunction);

extern "C" DRIVER_INITIALIZE DriverEntry;

namespace
{
    using namespace DragonBurn::Protocol;

    constexpr ULONG SystemProcessInformationClass = 5;
    constexpr ULONG PoolTag = 'rBkD';
    constexpr ULONG MaxModuleEntries = 512;
    DRIVER_DISPATCH DispatchUnsupported;
    DRIVER_DISPATCH DispatchCreateClose;
    DRIVER_DISPATCH DispatchDeviceControl;
    DRIVER_UNLOAD DriverUnload;
    DRIVER_INITIALIZE InitializeDriver;


    const GUID DeviceClassGuid =
    { 0x45ca70d7, 0x27e5, 0x4bcc, { 0x93, 0x7f, 0xf4, 0x39, 0x57, 0x66, 0x6d, 0x62 } };

    DeviceNames DevicePaths{};
    bool DevicePathsReady = false;

    struct SystemProcessInformation
    {
        ULONG nextEntryOffset;
        ULONG numberOfThreads;
        UCHAR reserved1[48];
        UNICODE_STRING imageName;
        KPRIORITY basePriority;
        HANDLE uniqueProcessId;
    };

    struct Peb
    {
        UCHAR reserved[0x18];
        PVOID loaderData;
    };

    struct PebLoaderData
    {
        UCHAR reserved[0x10];
        LIST_ENTRY inLoadOrderModuleList;
    };

    struct LoaderDataTableEntry
    {
        LIST_ENTRY inLoadOrderLinks;
        LIST_ENTRY inMemoryOrderLinks;
        LIST_ENTRY inInitializationOrderLinks;
        PVOID dllBase;
        PVOID entryPoint;
        ULONG sizeOfImage;
        ULONG padding;
        UNICODE_STRING fullDllName;
        UNICODE_STRING baseDllName;
    };

    static_assert(FIELD_OFFSET(Peb, loaderData) == 0x18, "Unexpected PEB layout");
    static_assert(FIELD_OFFSET(PebLoaderData, inLoadOrderModuleList) == 0x10, "Unexpected loader layout");
    static_assert(FIELD_OFFSET(LoaderDataTableEntry, dllBase) == 0x30, "Unexpected loader entry layout");
    static_assert(FIELD_OFFSET(LoaderDataTableEntry, baseDllName) == 0x58, "Unexpected loader name layout");

    NTSTATUS PrepareDeviceNames()
    {
        if (DevicePathsReady)
            return STATUS_SUCCESS;

        UNICODE_STRING keyName{};
        RtlInitUnicodeString(
            &keyName,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName");

        OBJECT_ATTRIBUTES attributes{};
        InitializeObjectAttributes(
            &attributes,
            &keyName,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
            nullptr,
            nullptr);

        HANDLE keyHandle = nullptr;
        NTSTATUS status = ZwOpenKey(&keyHandle, KEY_QUERY_VALUE, &attributes);
        if (!NT_SUCCESS(status))
            return status;

        UNICODE_STRING valueName{};
        RtlInitUnicodeString(&valueName, L"ComputerName");

        alignas(KEY_VALUE_PARTIAL_INFORMATION)
            UCHAR valueBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
                HostNameCapacity * sizeof(WCHAR)]{};
        ULONG resultLength = 0;
        status = ZwQueryValueKey(
            keyHandle,
            &valueName,
            KeyValuePartialInformation,
            valueBuffer,
            sizeof(valueBuffer),
            &resultLength);
        ZwClose(keyHandle);
        if (!NT_SUCCESS(status))
            return status;

        const auto value = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(valueBuffer);
        if ((value->Type != REG_SZ && value->Type != REG_EXPAND_SZ) ||
            value->DataLength < sizeof(WCHAR) ||
            value->DataLength > HostNameCapacity * sizeof(WCHAR) ||
            value->DataLength % sizeof(WCHAR) != 0)
        {
            return STATUS_OBJECT_NAME_INVALID;
        }

        const auto hostName = reinterpret_cast<const WCHAR*>(value->Data);
        SIZE_T hostNameLength = value->DataLength / sizeof(WCHAR);
        while (hostNameLength != 0 && hostName[hostNameLength - 1] == L'\0')
            --hostNameLength;

        DeviceNames derivedNames{};
        if (!BuildDeviceNames(hostName, hostNameLength, &derivedNames))
            return STATUS_OBJECT_NAME_INVALID;

        RtlCopyMemory(&DevicePaths, &derivedNames, sizeof(DevicePaths));
        DevicePathsReady = true;
        return STATUS_SUCCESS;
    }

    NTSTATUS CompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information = 0)
    {
        irp->IoStatus.Status = status;
        irp->IoStatus.Information = information;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
    }

    bool TryGetTerminatedLength(const WCHAR* text, SIZE_T capacity, USHORT* lengthBytes)
    {
        if (text == nullptr || lengthBytes == nullptr || capacity == 0)
            return false;

        for (SIZE_T index = 0; index < capacity; ++index)
        {
            if (text[index] == L'\0')
            {
                if (index > MAXUSHORT / sizeof(WCHAR))
                    return false;

                *lengthBytes = static_cast<USHORT>(index * sizeof(WCHAR));
                return index != 0;
            }
        }

        return false;
    }

    bool IsValidUserRange(UINT64 address, SIZE_T size)
    {
        if (address == 0 || size == 0)
            return false;

        const UINT64 highestUserAddress = reinterpret_cast<UINT64>(MmHighestUserAddress);
        if (address > highestUserAddress)
            return false;

        const UINT64 lastByte = address + size - 1;
        return lastByte >= address && lastByte <= highestUserAddress;
    }


    NTSTATUS LookupProcess(HANDLE processId, PEPROCESS* process)
    {
        if (process == nullptr || processId == nullptr)
            return STATUS_INVALID_PARAMETER;

        *process = nullptr;
        return PsLookupProcessByProcessId(processId, process);
    }

    NTSTATUS CopyFromAttachedProcess(UINT64 sourceAddress, void* destination, SIZE_T size)
    {
        if (destination == nullptr || !IsValidUserRange(sourceAddress, size))
            return STATUS_INVALID_PARAMETER;

        MM_COPY_ADDRESS source{};
        source.VirtualAddress = reinterpret_cast<void*>(sourceAddress);

        SIZE_T bytesCopied = 0;
        const NTSTATUS status = MmCopyMemory(
            destination,
            source,
            size,
            MM_COPY_MEMORY_VIRTUAL,
            &bytesCopied);

        if (!NT_SUCCESS(status))
            return status;

        return bytesCopied == size ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
    }

    NTSTATUS CopyFromProcess(
        PEPROCESS process,
        UINT64 sourceAddress,
        void* destination,
        SIZE_T size)
    {
        if (process == nullptr)
            return STATUS_INVALID_PARAMETER;

        KAPC_STATE apcState{};
        KeStackAttachProcess(process, &apcState);
        const NTSTATUS status = CopyFromAttachedProcess(sourceAddress, destination, size);
        KeUnstackDetachProcess(&apcState);
        return status;
    }

    NTSTATUS HandleAttach(void* systemBuffer, ULONG inputLength, ULONG outputLength)
    {
        if (systemBuffer == nullptr || inputLength < sizeof(Request) || outputLength < sizeof(Request))
            return STATUS_BUFFER_TOO_SMALL;

        const auto request = static_cast<const Request*>(systemBuffer);
        PEPROCESS process = nullptr;
        const NTSTATUS status = LookupProcess(request->process_id, &process);
        if (NT_SUCCESS(status))
            ObDereferenceObject(process);

        return status;
    }

    NTSTATUS HandleRead(PIRP irp, void* systemBuffer, ULONG inputLength, ULONG outputLength)
    {
        if (systemBuffer == nullptr || inputLength < sizeof(Request) || outputLength < sizeof(Request))
            return STATUS_BUFFER_TOO_SMALL;

        const auto request = static_cast<const Request*>(systemBuffer);
        if (request->buffer == nullptr || request->size == 0 || request->size > MaxSingleReadSize ||
            !IsValidUserRange(reinterpret_cast<UINT64>(request->target), request->size))
        {
            return STATUS_INVALID_PARAMETER;
        }

        auto temporary = static_cast<UCHAR*>(ExAllocatePool2(POOL_FLAG_NON_PAGED, request->size, PoolTag));
        if (temporary == nullptr)
            return STATUS_INSUFFICIENT_RESOURCES;

        PEPROCESS process = nullptr;
        NTSTATUS status = LookupProcess(request->process_id, &process);
        if (NT_SUCCESS(status))
        {
            status = CopyFromProcess(process, reinterpret_cast<UINT64>(request->target), temporary, request->size);
            ObDereferenceObject(process);
        }

        if (NT_SUCCESS(status))
        {
            __try
            {
                if (irp->RequestorMode == UserMode)
                    ProbeForWrite(request->buffer, request->size, 1);

                RtlCopyMemory(request->buffer, temporary, request->size);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                status = GetExceptionCode();
            }
        }

        RtlSecureZeroMemory(temporary, request->size);
        ExFreePoolWithTag(temporary, PoolTag);
        return status;
    }

    NTSTATUS QueryProcessId(ProcessIdPacket* packet)
    {
        if (packet == nullptr)
            return STATUS_INVALID_PARAMETER;

        USHORT requestedLength = 0;
        if (!TryGetTerminatedLength(packet->name, NameCapacity, &requestedLength))
            return STATUS_INVALID_PARAMETER;

        UNICODE_STRING requestedName{};
        requestedName.Buffer = packet->name;
        requestedName.Length = requestedLength;
        requestedName.MaximumLength = static_cast<USHORT>(requestedLength + sizeof(WCHAR));

        ULONG requiredLength = 0;
        NTSTATUS status = ZwQuerySystemInformation(SystemProcessInformationClass, nullptr, 0, &requiredLength);
        if (status != STATUS_INFO_LENGTH_MISMATCH || requiredLength == 0)
            return status;

        PVOID processBuffer = nullptr;
        ULONG bufferLength = requiredLength;

        for (ULONG attempt = 0; attempt < 3; ++attempt)
        {
            if (bufferLength > MaxBatchOutputSize)
                return STATUS_INSUFFICIENT_RESOURCES;

            processBuffer = ExAllocatePool2(POOL_FLAG_PAGED, bufferLength, PoolTag);
            if (processBuffer == nullptr)
                return STATUS_INSUFFICIENT_RESOURCES;

            status = ZwQuerySystemInformation(
                SystemProcessInformationClass,
                processBuffer,
                bufferLength,
                &requiredLength);

            if (status != STATUS_INFO_LENGTH_MISMATCH)
                break;

            ExFreePoolWithTag(processBuffer, PoolTag);
            processBuffer = nullptr;
            bufferLength = requiredLength;
        }

        if (!NT_SUCCESS(status))
        {
            if (processBuffer != nullptr)
                ExFreePoolWithTag(processBuffer, PoolTag);
            return status;
        }

        status = STATUS_NOT_FOUND;
        auto entry = static_cast<SystemProcessInformation*>(processBuffer);

        for (;;)
        {
            if (entry->imageName.Buffer != nullptr &&
                RtlEqualUnicodeString(&entry->imageName, &requestedName, TRUE))
            {
                packet->pid = HandleToULong(entry->uniqueProcessId);
                status = packet->pid == 0 ? STATUS_NOT_FOUND : STATUS_SUCCESS;
                break;
            }

            if (entry->nextEntryOffset == 0)
                break;

            entry = reinterpret_cast<SystemProcessInformation*>(
                reinterpret_cast<UCHAR*>(entry) + entry->nextEntryOffset);
        }

        ExFreePoolWithTag(processBuffer, PoolTag);
        return status;
    }

    NTSTATUS QueryModule(ModulePacket* packet)
    {
        if (packet == nullptr || packet->pid == 0)
            return STATUS_INVALID_PARAMETER;

        USHORT requestedLength = 0;
        if (!TryGetTerminatedLength(packet->moduleName, NameCapacity, &requestedLength))
            return STATUS_INVALID_PARAMETER;

        UNICODE_STRING requestedName{};
        requestedName.Buffer = packet->moduleName;
        requestedName.Length = requestedLength;
        requestedName.MaximumLength = static_cast<USHORT>(requestedLength + sizeof(WCHAR));

        PEPROCESS process = nullptr;
        NTSTATUS status = LookupProcess(ULongToHandle(packet->pid), &process);
        if (!NT_SUCCESS(status))
            return status;

        status = STATUS_NOT_FOUND;
        KAPC_STATE apcState{};
        KeStackAttachProcess(process, &apcState);

        __try
        {
            const auto peb = static_cast<const Peb*>(PsGetProcessPeb(process));
            if (peb == nullptr)
            {
                status = STATUS_NOT_FOUND;
                __leave;
            }

            ProbeForRead(const_cast<Peb*>(peb), sizeof(Peb), alignof(PVOID));
            const auto loader = static_cast<const PebLoaderData*>(peb->loaderData);
            if (loader == nullptr)
            {
                status = STATUS_NOT_FOUND;
                __leave;
            }

            ProbeForRead(const_cast<PebLoaderData*>(loader), sizeof(PebLoaderData), alignof(PVOID));
            const LIST_ENTRY* const head = &loader->inLoadOrderModuleList;
            const LIST_ENTRY* link = head->Flink;

            for (ULONG index = 0; link != head && index < MaxModuleEntries; ++index)
            {
                ProbeForRead(const_cast<LIST_ENTRY*>(link), sizeof(LIST_ENTRY), alignof(PVOID));
                const auto module = CONTAINING_RECORD(link, LoaderDataTableEntry, inLoadOrderLinks);
                ProbeForRead(const_cast<LoaderDataTableEntry*>(module), sizeof(LoaderDataTableEntry), alignof(PVOID));

                const UNICODE_STRING moduleName = module->baseDllName;
                if (moduleName.Buffer != nullptr && moduleName.Length != 0 &&
                    moduleName.Length <= moduleName.MaximumLength &&
                    moduleName.Length <= (NameCapacity - 1) * sizeof(WCHAR))
                {
                    ProbeForRead(moduleName.Buffer, moduleName.Length, sizeof(WCHAR));
                    if (RtlEqualUnicodeString(&moduleName, &requestedName, TRUE))
                    {
                        packet->baseAddress = reinterpret_cast<UINT64>(module->dllBase);
                        packet->size = module->sizeOfImage;
                        status = packet->baseAddress == 0 ? STATUS_NOT_FOUND : STATUS_SUCCESS;
                        break;
                    }
                }

                link = module->inLoadOrderLinks.Flink;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = GetExceptionCode();
        }

        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
        return status;
    }

    NTSTATUS HandleBatchRead(
        void* systemBuffer,
        ULONG inputLength,
        ULONG outputLength,
        ULONG_PTR* information)
    {
        if (systemBuffer == nullptr || information == nullptr ||
            inputLength < sizeof(BatchReadHeader) || outputLength < sizeof(BatchReadHeader))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        const auto header = static_cast<const BatchReadHeader*>(systemBuffer);
        if (header->process_id == nullptr || header->num_requests == 0 ||
            header->num_requests > MaxBatchRequests ||
            header->total_buffer_size == 0 || header->total_buffer_size > MaxBatchOutputSize)
        {
            return STATUS_INVALID_PARAMETER;
        }

        const SIZE_T requestArraySize =
            static_cast<SIZE_T>(header->num_requests) * sizeof(BatchReadRequest);
        if (requestArraySize / sizeof(BatchReadRequest) != header->num_requests)
            return STATUS_INTEGER_OVERFLOW;

        const SIZE_T requestStructureSize = sizeof(BatchReadHeader) + requestArraySize;
        if (requestStructureSize < sizeof(BatchReadHeader))
            return STATUS_INTEGER_OVERFLOW;

        const SIZE_T totalSize = requestStructureSize + header->total_buffer_size;
        if (totalSize < requestStructureSize || totalSize > MAXULONG ||
            inputLength != totalSize || outputLength < totalSize)
        {
            return STATUS_INVALID_BUFFER_SIZE;
        }

        const auto requests = reinterpret_cast<const BatchReadRequest*>(
            static_cast<const UCHAR*>(systemBuffer) + sizeof(BatchReadHeader));
        auto output = static_cast<UCHAR*>(systemBuffer) + requestStructureSize;

        SIZE_T expectedOffset = 0;
        for (ULONG index = 0; index < header->num_requests; ++index)
        {
            const auto& request = requests[index];
            if (request.size == 0 || request.size > MaxSingleReadSize ||
                request.offset_in_buffer != expectedOffset ||
                !IsValidUserRange(request.address, request.size) ||
                request.size > header->total_buffer_size - expectedOffset)
            {
                return STATUS_INVALID_PARAMETER;
            }

            expectedOffset += request.size;
        }

        if (expectedOffset != header->total_buffer_size)
            return STATUS_INVALID_PARAMETER;

        PEPROCESS process = nullptr;
        NTSTATUS status = LookupProcess(header->process_id, &process);
        if (!NT_SUCCESS(status))
            return status;

        KAPC_STATE apcState{};
        KeStackAttachProcess(process, &apcState);
        for (ULONG index = 0; index < header->num_requests; ++index)
        {
            const auto& request = requests[index];
            status = CopyFromAttachedProcess(
                request.address,
                output + request.offset_in_buffer,
                request.size);
            if (!NT_SUCCESS(status))
                break;
        }
        KeUnstackDetachProcess(&apcState);

        ObDereferenceObject(process);

        if (NT_SUCCESS(status))
            *information = totalSize;
        else
            RtlSecureZeroMemory(output, header->total_buffer_size);

        return status;
    }

    NTSTATUS DispatchUnsupported(PDEVICE_OBJECT deviceObject, PIRP irp)
    {
        UNREFERENCED_PARAMETER(deviceObject);
        return CompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST);
    }

    NTSTATUS DispatchCreateClose(PDEVICE_OBJECT deviceObject, PIRP irp)
    {
        UNREFERENCED_PARAMETER(deviceObject);
        return CompleteIrp(irp, STATUS_SUCCESS);
    }

    NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp)
    {
        UNREFERENCED_PARAMETER(deviceObject);

        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
            return CompleteIrp(irp, STATUS_INVALID_DEVICE_STATE);

        const auto stack = IoGetCurrentIrpStackLocation(irp);
        const ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
        const ULONG inputLength = stack->Parameters.DeviceIoControl.InputBufferLength;
        const ULONG outputLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
        void* const systemBuffer = irp->AssociatedIrp.SystemBuffer;

        NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
        ULONG_PTR information = 0;

        switch (controlCode)
        {
        case IoctlAttach:
            status = HandleAttach(systemBuffer, inputLength, outputLength);
            break;

        case IoctlRead:
            status = HandleRead(irp, systemBuffer, inputLength, outputLength);
            break;

        case IoctlGetModuleBase:
            if (systemBuffer == nullptr || inputLength < sizeof(ModulePacket) || outputLength < sizeof(ModulePacket))
            {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                status = QueryModule(static_cast<ModulePacket*>(systemBuffer));
                if (NT_SUCCESS(status))
                    information = sizeof(ModulePacket);
            }
            break;

        case IoctlGetPid:
            if (systemBuffer == nullptr || inputLength < sizeof(ProcessIdPacket) || outputLength < sizeof(ProcessIdPacket))
            {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                status = QueryProcessId(static_cast<ProcessIdPacket*>(systemBuffer));
                if (NT_SUCCESS(status))
                    information = sizeof(ProcessIdPacket);
            }
            break;

        case IoctlBatchRead:
            status = HandleBatchRead(systemBuffer, inputLength, outputLength, &information);
            break;

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        return CompleteIrp(irp, status, information);
    }

    void DriverUnload(PDRIVER_OBJECT driverObject)
    {
        if (DevicePathsReady)
        {
            UNICODE_STRING dosDeviceName{};
            RtlInitUnicodeString(&dosDeviceName, DevicePaths.dosPath);
            IoDeleteSymbolicLink(&dosDeviceName);
        }

        if (driverObject->DeviceObject != nullptr)
            IoDeleteDevice(driverObject->DeviceObject);
    }
}

namespace
{
    NTSTATUS InitializeDriver(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
    {
        UNREFERENCED_PARAMETER(registryPath);

        if (driverObject == nullptr)
            return STATUS_INVALID_PARAMETER;

        if (!DevicePathsReady)
            return STATUS_INVALID_DEVICE_STATE;

        for (ULONG index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index)
            driverObject->MajorFunction[index] = DispatchUnsupported;

        driverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
        driverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
        driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
        driverObject->DriverUnload = DriverUnload;

        UNICODE_STRING kernelDeviceName{};
        RtlInitUnicodeString(&kernelDeviceName, DevicePaths.kernelPath);

        PDEVICE_OBJECT deviceObject = nullptr;
        NTSTATUS status = IoCreateDeviceSecure(
            driverObject,
            0,
            &kernelDeviceName,
            DragonBurn::Protocol::DeviceType,
            FILE_DEVICE_SECURE_OPEN,
            FALSE,
            &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
            &DeviceClassGuid,
            &deviceObject);

        if (!NT_SUCCESS(status))
            return status;

        deviceObject->Flags |= DO_BUFFERED_IO;

        UNICODE_STRING dosDeviceName{};
        RtlInitUnicodeString(&dosDeviceName, DevicePaths.dosPath);
        status = IoCreateSymbolicLink(&dosDeviceName, &kernelDeviceName);
        if (!NT_SUCCESS(status))
        {
            IoDeleteDevice(deviceObject);
            return status;
        }

        deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
        return STATUS_SUCCESS;
    }
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    const NTSTATUS status = PrepareDeviceNames();
    if (!NT_SUCCESS(status))
        return status;

    if (driverObject != nullptr)
        return InitializeDriver(driverObject, registryPath);

    UNICODE_STRING driverName{};
    RtlInitUnicodeString(&driverName, DevicePaths.driverPath);
    return IoCreateDriver(&driverName, InitializeDriver);
}
