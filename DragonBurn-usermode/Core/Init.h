#pragma once
#include <fstream>
#include <tchar.h>
#include <shellapi.h>
#include <cstdlib>
#include <thread>
#include <psapi.h>
#include <stdexcept>
#include "../Offsets/Offsets.h"

namespace Init
{
    using namespace std;

	class Verify
	{
	public:
		// Check if the Windows version is higher than 7
		static bool CheckWindowVersion() {
            OSVERSIONINFOEX osvi;
            ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
            osvi.dwMajorVersion = 6;
            osvi.dwMinorVersion = 1;

            ULONGLONG conditionMask = VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL);
            conditionMask = VerSetConditionMask(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

            if (VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, conditionMask))
            {
                return true;
            }
            return false;
		}

        //static void RandTitle()
        //{
        //    srand(time(0));
        //    constexpr int length = 25;
        //    const auto characters = TEXT("0123456789qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM");
        //    TCHAR title[length + 1]{};

        //    for (int j = 0; j < length; j++)
        //    {
        //        title[j] += characters[rand() % 63];
        //    }

        //    SetConsoleTitle(title);
        //}



	};

    class Client
    {
    public:
        //static std::string GetCs2Version(int pid)
        //{
        //    std::wstring processPath;
        //    WCHAR filename[MAX_PATH];

        //    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        //    if (processHandle != NULL) {
        //        if (GetModuleFileNameEx(processHandle, NULL, filename, MAX_PATH) == 0)
        //            throw std::runtime_error("failed to get process path");
        //        else
        //            processPath = filename;
        //        CloseHandle(processHandle);
        //    }
        //    else
        //        throw std::runtime_error("failed to open process");

        //    int pos = processPath.rfind(L"bin");
        //    if (pos != std::wstring::npos) 
        //        processPath = processPath.substr(0, pos + 3) + L"\\built_from_cl.txt";
        //    else
        //        throw std::runtime_error("failed to find version file");

        //    std::string gameVersion;
        //    std::ifstream file(processPath);
        //    if (file.is_open()) 
        //    {
        //        std::getline(file, gameVersion);
        //        file.close();
        //    }
        //    else
        //        throw std::runtime_error("failed to get game version");

        //    return gameVersion;
        //}

        static HWND FindGameWindow(DWORD processId)
        {
            struct SearchContext
            {
                DWORD processId;
                HWND window;
            } context{ processId, nullptr };

            EnumWindows([](HWND window, LPARAM parameter) -> BOOL
            {
                auto* context = reinterpret_cast<SearchContext*>(parameter);
                DWORD windowProcessId = 0;
                GetWindowThreadProcessId(window, &windowProcessId);
                if (windowProcessId != context->processId || !IsWindowVisible(window) ||
                    GetWindow(window, GW_OWNER) != nullptr)
                {
                    return TRUE;
                }

                char className[64]{};
                if (GetClassNameA(window, className, static_cast<int>(sizeof(className))) == 0 ||
                    lstrcmpiA(className, "SDL_app") != 0)
                {
                    return TRUE;
                }

                context->window = window;
                return FALSE;
            }, reinterpret_cast<LPARAM>(&context));

            gameProcessId = processId;
            gameWindow = context.window;
            return gameWindow;
        }

        static HWND GetGameWindow()
        {
            if (!IsWindow(gameWindow) && gameProcessId != 0)
                FindGameWindow(gameProcessId);
            return IsWindow(gameWindow) ? gameWindow : nullptr;
        }

        inline static DWORD gameProcessId = 0;
        inline static HWND gameWindow = nullptr;

        static bool isGameWindowActive()
        {
            const HWND activeGameWindow = GetGameWindow();
            return activeGameWindow != nullptr && GetForegroundWindow() == activeGameWindow;
        }

        static void Exit()
        {
            exit(0);
        }
    };

}

