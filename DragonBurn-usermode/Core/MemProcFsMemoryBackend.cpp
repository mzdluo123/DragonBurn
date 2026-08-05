#include "MemProcFsMemoryBackend.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "../../memprocfs/vmmdll.h"

namespace
{
    constexpr DWORD MemProcFsLoadFlags =
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
        LOAD_LIBRARY_SEARCH_USER_DIRS |
        LOAD_LIBRARY_SEARCH_SYSTEM32;

    std::string Win32ErrorMessage(const char* operation, const DWORD error)
    {
        return std::string(operation) + " failed with Win32 error " + std::to_string(error);
    }

    bool GetExecutableDirectory(std::filesystem::path& directory, std::string& error)
    {
        try
        {
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;)
            {
                SetLastError(ERROR_SUCCESS);
                const DWORD length = GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    error = Win32ErrorMessage("Resolving the executable path", GetLastError());
                    return false;
                }

                if (length < buffer.size())
                {
                    directory = std::filesystem::path(
                        std::wstring(buffer.data(), static_cast<size_t>(length))).parent_path();
                    if (directory.empty())
                    {
                        error = "The executable path has no parent directory";
                        return false;
                    }
                    return true;
                }

                if (buffer.size() >= 32768)
                {
                    error = "The executable path exceeds the supported Windows path length";
                    return false;
                }
                buffer.resize(std::min<size_t>(buffer.size() * 2, 32768));
            }
        }
        catch (const std::exception& exception)
        {
            error = std::string("Constructing the executable path failed: ") + exception.what();
            return false;
        }
    }

    bool WideRangeToUtf8(const wchar_t* text, const size_t length, std::string& output)
    {
        output.clear();
        if (text == nullptr || length == 0 || length > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return false;

        const int required = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            static_cast<int>(length),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0)
            return false;

        try
        {
            output.resize(static_cast<size_t>(required));
        }
        catch (...)
        {
            return false;
        }

        return WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            static_cast<int>(length),
            output.data(),
            required,
            nullptr,
            nullptr) == required;
    }

    bool WideStringToUtf8(const wchar_t* text, std::string& output)
    {
        if (text == nullptr)
            return false;
        return WideRangeToUtf8(text, std::wcslen(text), output);
    }

    std::string ExtractInitializationError(const PLC_CONFIG_ERRORINFO errorInfo)
    {
        if (errorInfo == nullptr || errorInfo->dwVersion != LC_CONFIG_ERRORINFO_VERSION)
            return {};

        constexpr size_t TextOffset = offsetof(LC_CONFIG_ERRORINFO, wszUserText);
        if (errorInfo->cbStruct <= TextOffset || errorInfo->cwszUserText == 0)
            return {};

        const size_t availableCharacters = (errorInfo->cbStruct - TextOffset) / sizeof(wchar_t);
        const size_t claimedCharacters = std::min<size_t>(
            errorInfo->cwszUserText,
            availableCharacters);
        size_t textLength = 0;
        while (textLength < claimedCharacters && errorInfo->wszUserText[textLength] != L'\0')
            ++textLength;

        std::string message;
        return WideRangeToUtf8(errorInfo->wszUserText, textLength, message)
            ? message
            : std::string{};
    }

    template <typename Function>
    bool ResolveFunction(
        const HMODULE module,
        const char* name,
        Function& function,
        std::string& error)
    {
        function = reinterpret_cast<Function>(GetProcAddress(module, name));
        if (function != nullptr)
            return true;

        error = std::string("MemProcFS runtime/API mismatch: missing symbol ") + name;
        return false;
    }

    ULONG64 ScalarFlags(const MemoryReadPolicy policy)
    {
        return policy == MemoryReadPolicy::BypassDataCache ? VMMDLL_FLAG_NOCACHE : 0;
    }

    DWORD ScatterFlags(const MemoryReadPolicy policy)
    {
        return policy == MemoryReadPolicy::BypassDataCache
            ? VMMDLL_FLAG_NOCACHE
            : VMMDLL_FLAG_SCATTER_FORCE_PAGEREAD;
    }
}

struct MemProcFsMemoryBackend::Api
{
    decltype(&VMMDLL_InitializeEx) initializeEx = nullptr;
    decltype(&VMMDLL_Close) close = nullptr;
    decltype(&VMMDLL_PidGetFromName) pidGetFromName = nullptr;
    decltype(&VMMDLL_ProcessGetInformation) processGetInformation = nullptr;
    decltype(&VMMDLL_ProcessGetModuleBaseW) processGetModuleBase = nullptr;
    decltype(&VMMDLL_MemReadEx) memReadEx = nullptr;
    decltype(&VMMDLL_Scatter_Initialize) scatterInitialize = nullptr;
    decltype(&VMMDLL_Scatter_PrepareEx) scatterPrepareEx = nullptr;
    decltype(&VMMDLL_Scatter_ExecuteRead) scatterExecuteRead = nullptr;
    decltype(&VMMDLL_Scatter_Clear) scatterClear = nullptr;
    decltype(&VMMDLL_Scatter_CloseHandle) scatterClose = nullptr;
    decltype(&LcMemFree) lcMemFree = nullptr;
};

MemProcFsMemoryBackend::MemProcFsMemoryBackend() = default;

MemProcFsMemoryBackend::~MemProcFsMemoryBackend()
{
    Shutdown();
}

MemoryBackendKind MemProcFsMemoryBackend::Kind() const noexcept
{
    return MemoryBackendKind::MemProcFs;
}

BackendInitializationResult MemProcFsMemoryBackend::Initialize()
{
    Shutdown();

    std::filesystem::path executableDirectory;
    std::string error;
    if (!GetExecutableDirectory(executableDirectory, error))
        return { BackendInitializationStatus::Failed, std::move(error) };

    std::filesystem::path runtimeDirectory;
    std::filesystem::path leechCorePath;
    std::filesystem::path vmmPath;
    try
    {
        runtimeDirectory = executableDirectory / L"memprocfs";
        leechCorePath = runtimeDirectory / L"leechcore.dll";
        vmmPath = runtimeDirectory / L"vmm.dll";
    }
    catch (const std::exception& exception)
    {
        return {
            BackendInitializationStatus::Failed,
            std::string("Constructing the MemProcFS runtime path failed: ") + exception.what()
        };
    }

    dllDirectoryCookie_ = AddDllDirectory(runtimeDirectory.c_str());
    if (dllDirectoryCookie_ == nullptr)
    {
        return {
            BackendInitializationStatus::Failed,
            Win32ErrorMessage("Adding the MemProcFS DLL directory", GetLastError())
        };
    }

    leechCoreModule_ = LoadLibraryExW(leechCorePath.c_str(), nullptr, MemProcFsLoadFlags);
    if (leechCoreModule_ == nullptr)
    {
        error = Win32ErrorMessage("Loading memprocfs\\leechcore.dll", GetLastError());
        Shutdown();
        return { BackendInitializationStatus::Failed, std::move(error) };
    }

    vmmModule_ = LoadLibraryExW(vmmPath.c_str(), nullptr, MemProcFsLoadFlags);
    if (vmmModule_ == nullptr)
    {
        error = Win32ErrorMessage("Loading memprocfs\\vmm.dll", GetLastError());
        Shutdown();
        return { BackendInitializationStatus::Failed, std::move(error) };
    }

    try
    {
        api_ = std::make_unique<Api>();
    }
    catch (...)
    {
        Shutdown();
        return {
            BackendInitializationStatus::Failed,
            "Allocating the MemProcFS API table failed"
        };
    }

    if (!ResolveFunction(vmmModule_, "VMMDLL_InitializeEx", api_->initializeEx, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_Close", api_->close, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_PidGetFromName", api_->pidGetFromName, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_ProcessGetInformation", api_->processGetInformation, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_ProcessGetModuleBaseW", api_->processGetModuleBase, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_MemReadEx", api_->memReadEx, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_Scatter_Initialize", api_->scatterInitialize, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_Scatter_PrepareEx", api_->scatterPrepareEx, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_Scatter_ExecuteRead", api_->scatterExecuteRead, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_Scatter_Clear", api_->scatterClear, error) ||
        !ResolveFunction(vmmModule_, "VMMDLL_Scatter_CloseHandle", api_->scatterClose, error) ||
        !ResolveFunction(leechCoreModule_, "LcMemFree", api_->lcMemFree, error))
    {
        Shutdown();
        return { BackendInitializationStatus::Failed, std::move(error) };
    }

    LPCSTR arguments[] = {
        "",
        "-device",
        "fpga",
        "-memmap",
        "auto",
        "-waitinitialize",
        "-disable-python",
        "-disable-symbolserver"
    };
    static_assert(std::size(arguments) == 8);

    PLC_CONFIG_ERRORINFO errorInfo = nullptr;
    vmmHandle_ = api_->initializeEx(
        static_cast<DWORD>(std::size(arguments)),
        arguments,
        &errorInfo);
    if (vmmHandle_ == nullptr)
    {
        error = ExtractInitializationError(errorInfo);
        if (errorInfo != nullptr)
            api_->lcMemFree(errorInfo);
        if (error.empty())
        {
            error = "FPGA/FTDI device, driver, or automatic memory map initialization failed";
        }
        Shutdown();
        return { BackendInitializationStatus::Failed, std::move(error) };
    }
    if (errorInfo != nullptr)
        api_->lcMemFree(errorInfo);

    try
    {
        batchScratch_.resize(MaxBatchMemoryOutputSize);
        batchBytesRead_.resize(MaxBatchMemoryRequests);
        batchOffsets_.resize(MaxBatchMemoryRequests + 1);
    }
    catch (...)
    {
        Shutdown();
        return {
            BackendInitializationStatus::Failed,
            "Allocating fixed MemProcFS scatter buffers failed"
        };
    }

    return { BackendInitializationStatus::Ready, {} };
}

void MemProcFsMemoryBackend::Shutdown() noexcept
{
    if (scatterHandle_ != nullptr && api_ && api_->scatterClose)
        api_->scatterClose(scatterHandle_);
    scatterHandle_ = nullptr;

    if (vmmHandle_ != nullptr && api_ && api_->close)
        api_->close(reinterpret_cast<VMM_HANDLE>(vmmHandle_));
    vmmHandle_ = nullptr;

    if (vmmModule_ != nullptr)
        FreeLibrary(vmmModule_);
    vmmModule_ = nullptr;

    if (leechCoreModule_ != nullptr)
        FreeLibrary(leechCoreModule_);
    leechCoreModule_ = nullptr;

    if (dllDirectoryCookie_ != nullptr)
        RemoveDllDirectory(dllDirectoryCookie_);
    dllDirectoryCookie_ = nullptr;

    processId_ = 0;
    api_.reset();
    std::vector<std::byte>().swap(batchScratch_);
    std::vector<DWORD>().swap(batchBytesRead_);
    std::vector<SIZE_T>().swap(batchOffsets_);
}
DWORD MemProcFsMemoryBackend::GetProcessId(const wchar_t* processName)
{
    if (vmmHandle_ == nullptr || !api_ || processName == nullptr)
        return 0;

    std::string utf8Name;
    if (!WideStringToUtf8(processName, utf8Name))
        return 0;

    DWORD processId = 0;
    return api_->pidGetFromName(
        reinterpret_cast<VMM_HANDLE>(vmmHandle_),
        utf8Name.c_str(),
        &processId)
        ? processId
        : 0;
}

bool MemProcFsMemoryBackend::Attach(const DWORD processId)
{
    if (vmmHandle_ == nullptr || !api_ || processId == 0)
        return false;

    VMMDLL_PROCESS_INFORMATION information{};
    information.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
    information.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
    SIZE_T informationSize = sizeof(information);
    if (!api_->processGetInformation(
        reinterpret_cast<VMM_HANDLE>(vmmHandle_),
        processId,
        &information,
        &informationSize))
    {
        return false;
    }

    VMMDLL_SCATTER_HANDLE candidate = api_->scatterInitialize(
        reinterpret_cast<VMM_HANDLE>(vmmHandle_),
        processId,
        0);
    if (candidate == nullptr)
        return false;

    if (scatterHandle_ != nullptr)
        api_->scatterClose(scatterHandle_);
    scatterHandle_ = candidate;
    processId_ = processId;
    return true;
}

DWORD64 MemProcFsMemoryBackend::GetModuleBase(const wchar_t* moduleName)
{
    if (vmmHandle_ == nullptr || !api_ || processId_ == 0 || moduleName == nullptr)
        return 0;

    return api_->processGetModuleBase(
        reinterpret_cast<VMM_HANDLE>(vmmHandle_),
        processId_,
        moduleName);
}

bool MemProcFsMemoryBackend::Read(
    const DWORD64 address,
    const std::span<std::byte> output,
    const MemoryReadPolicy policy)
{
    if (vmmHandle_ == nullptr || !api_ || processId_ == 0 || address == 0 ||
        output.data() == nullptr || output.empty() || output.size() > scalarScratch_.size())
    {
        return false;
    }

    DWORD bytesRead = 0;
    if (!api_->memReadEx(
        reinterpret_cast<VMM_HANDLE>(vmmHandle_),
        processId_,
        address,
        reinterpret_cast<PBYTE>(scalarScratch_.data()),
        static_cast<DWORD>(output.size()),
        &bytesRead,
        ScalarFlags(policy)) ||
        bytesRead != output.size())
    {
        return false;
    }

    std::memcpy(output.data(), scalarScratch_.data(), output.size());
    return true;
}

bool MemProcFsMemoryBackend::ResetScatter(const MemoryReadPolicy policy) noexcept
{
    if (vmmHandle_ == nullptr || !api_ || processId_ == 0)
        return false;

    const DWORD flags = ScatterFlags(policy);
    if (scatterHandle_ != nullptr && api_->scatterClear(scatterHandle_, processId_, flags))
        return true;

    if (scatterHandle_ != nullptr)
        api_->scatterClose(scatterHandle_);
    scatterHandle_ = api_->scatterInitialize(
        reinterpret_cast<VMM_HANDLE>(vmmHandle_),
        processId_,
        flags);
    return scatterHandle_ != nullptr;
}

MemoryBatchReadResult MemProcFsMemoryBackend::ReadBatch(
    const std::span<const MemoryReadRequest> requests,
    const std::span<std::byte> output,
    const MemoryReadPolicy policy,
    const std::span<std::uint8_t> requestSucceeded)
{
    if (output.data() != nullptr && !output.empty())
        SecureZeroMemory(output.data(), output.size());
    if (requestSucceeded.data() != nullptr && !requestSucceeded.empty())
        SecureZeroMemory(requestSucceeded.data(), requestSucceeded.size());

    if (vmmHandle_ == nullptr || !api_ || processId_ == 0 || requests.empty() ||
        output.data() == nullptr || output.empty() || requests.size() > batchBytesRead_.size() ||
        output.size() > batchScratch_.size() ||
        (!requestSucceeded.empty() && requestSucceeded.size() != requests.size()) ||
        !ResetScatter(policy))
    {
        return {};
    }

    SecureZeroMemory(batchScratch_.data(), output.size());
    batchOffsets_[0] = 0;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        const MemoryReadRequest& request = requests[index];
        if (request.address == 0 || request.size == 0 || request.size > MAXDWORD ||
            request.size > output.size() - batchOffsets_[index])
        {
            return {};
        }

        batchOffsets_[index + 1] = batchOffsets_[index] + request.size;
        batchBytesRead_[index] = 0;
        api_->scatterPrepareEx(
            scatterHandle_,
            request.address,
            static_cast<DWORD>(request.size),
            reinterpret_cast<PBYTE>(batchScratch_.data() + batchOffsets_[index]),
            &batchBytesRead_[index]);
    }

    if (batchOffsets_[requests.size()] != output.size() ||
        !api_->scatterExecuteRead(scatterHandle_))
    {
        return {};
    }

    SIZE_T successfulRequests = 0;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        if (batchBytesRead_[index] == requests[index].size)
        {
            ++successfulRequests;
            if (!requestSucceeded.empty())
                requestSucceeded[index] = 1;
        }
        else
        {
            SecureZeroMemory(
                batchScratch_.data() + batchOffsets_[index],
                requests[index].size);
        }
    }

    std::memcpy(output.data(), batchScratch_.data(), output.size());
    return { true, successfulRequests };
}
