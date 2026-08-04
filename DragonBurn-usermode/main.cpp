//______                            ______                  
//|  _  \                           | ___ \                 
//| | | |_ __ __ _  __ _  ___  _ __ | |_/ /_   _ _ __ _ __  
//| | | | '__/ _` |/ _` |/ _ \| '_ \| ___ \ | | | '__| '_ \ 
//| |/ /| | | (_| | (_| | (_) | | | | |_/ / |_| | |  | | | |
//|___/ |_|  \__,_|\__, |\___/|_| |_\____/ \__,_|_|  |_| |_|
//                  __/ |                                   
//                 |___/                                    
//
//https://discord.gg/5WcvdzFybD
//https://github.com/ByteCorum/DragonBurn

#include "Core/Cheats.h"
#include "Offsets/Offsets.h"
#include "Resources/Language.h"
#include "Core/Init.h"
#include "Config/ConfigSaver.h"
#include "Helpers/Logger.h"
#include "Helpers/UIAccess.h"
#include "Features/WebRadar.h"
#include <filesystem>
#include <KnownFolders.h>
#include <ShlObj.h>

using namespace std;

namespace fs = filesystem;
void Cheat();

int main()
{
	if (!memoryManager.Initialize())
	{
		Log::Error(memoryManager.GetLastError(), false, false);
		return -1;
	}

	const MemoryBackendKind backendKind = memoryManager.GetBackendKind();
	if (backendKind == MemoryBackendKind::Driver)
		Log::Info("Using DragonBurn driver memory backend");
	else
		Log::Info("Using MemProcFS FPGA memory backend");

// Do not use UIAccess for debugging/profiling (UIAccess restarts the client).
#ifndef DBDEBUG
	if (backendKind == MemoryBackendKind::Driver)
	{
		const DWORD error = PrepareForUIAccess();
		if (error != ERROR_SUCCESS)
		{
			MessageBoxA(nullptr, "Failed to elevate to UIAccess.", "Error", MB_OK);
			memoryManager.Shutdown();
			return -1;
		}
	}
#endif

	Cheat();
	memoryManager.Shutdown();
	return 0;
}



void Cheat()
{
	ShowWindow(GetConsoleWindow(), SW_SHOWNORMAL);
	SetConsoleTitle(L"DragonBurn");
	int tryCount = 0;
	//Init::Verify::RandTitle();

	Log::Custom(R"LOGO(______                            ______                  
|  _  \                           | ___ \                 
| | | |_ __ __ _  __ _  ___  _ __ | |_/ /_   _ _ __ _ __  
| | | | '__/ _` |/ _` |/ _ \| '_ \| ___ \ | | | '__| '_ \ 
| |/ /| | | (_| | (_| | (_) | | | | |_/ / |_| | |  | | | |
|___/ |_|  \__,_|\__, |\___/|_| |_\____/ \__,_|_|  |_| |_|
                  __/ |                                   
                 |___/                                    
)LOGO", 13);
	Log::Info(MenuConfig::name + " v" + MenuConfig::version + " by " + MenuConfig::author);
	Log::Info("https://github.com/ByteCorum/DragonBurn");
	Log::Info("https://discord.gg/5WcvdzFybD\n");

	if (!Init::Verify::CheckWindowVersion())
		Log::Warning("Your os is unsupported, bugs may occurred", true);

	char documentsPath[MAX_PATH];
	if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath) != S_OK)
		Log::Error("Failed to get the Documents folder path");

	MenuConfig::docPath = documentsPath;
	MenuConfig::path = MenuConfig::docPath + "\\DragonBurn";
	try
	{
		if (fs::exists(MenuConfig::docPath + "\\Adobe Software Data"))
			fs::rename(MenuConfig::docPath + "\\Adobe Software Data", MenuConfig::path);

		try
		{
			fs::create_directories(MenuConfig::path + "\\Data");
			Log::Fine("Config folder connected: " + MenuConfig::path);
		}
		catch (std::exception error)
		{
			Log::Error(error.what());
		}

		if (fs::exists(MenuConfig::path + "\\default.cfg"))
			MenuConfig::defaultConfig = true;

		Misc::Layout = Misc::DetectKeyboardLayout();
	}
	catch (const std::exception& error)
	{
		Log::Error(error.what());
	}


	Log::Info("Waiting for CS2...");
	DWORD pid = 0;
	bool attached = false;
	do
	{
		attached = false;
		pid = memoryManager.GetProcessID(L"cs2.exe");
		if (pid != 0)
		{
			Log::PreviousLine();
			Log::Info("Attaching to CS2...");
			attached = memoryManager.Attach(pid);
		}
		Sleep(1000);
	} while (!attached);

	Log::PreviousLine();
	Log::Fine("Connected to CS2");

	tryCount = 0;
UPDATE_OFFSETS://UPDATE_OFFSETS
	Log::Info("Updating offsets...");
	try
	{
		Offset.UpdateOffsets();
		Log::PreviousLine();
		Log::Fine("Offsets updated");
	}
	catch (const std::exception& error)
	{
		Log::PreviousLine();
		std::string errorMsg = error.what();
		if (errorMsg.find("bad internet connection") != std::string::npos && tryCount < 3)
		{
			Log::Error(errorMsg, false, false);
			Log::Info("Reconnecting...");
			tryCount++;
			goto UPDATE_OFFSETS;//UPDATE_OFFSETS
		}
		else
			Log::Error(errorMsg);
	}

	bool inited = false;
	tryCount = 0;
	Log::Info("Initialing adresses..", '.');
	do
	{
		std::cout << '.';
		tryCount++;
		inited = gGame.InitAddress();
		Sleep(1000);
	} while (!inited && tryCount < 30);
	std::cout << '\n';

	if (!inited)
	{
		Log::PreviousLine();
		Log::Error("Failed to Init Addresses");
	}

	g_globalVars = std::make_unique<globalvars>();
	if (!g_globalVars->UpdateGlobalvars())
	{
		Log::PreviousLine();
		Log::Error("Offsets are outdated, wait a few hours for offsets to update");
	}

	Log::PreviousLine();
	Log::Fine("Linked to CS2");
	Log::Fine("DragonBurn loaded");

	const WebRadarConfig webRadarConfig{
		"0.0.0.0",
		16668,
		fs::path(MenuConfig::path) / "Data" / "WebRadarMaps"
	};
	if (WebRadar::Start(webRadarConfig))
		Log::Fine("Web radar listening on 0.0.0.0:16668");
	else
		Log::Warning("Web radar could not start on 0.0.0.0:16668; overlay will continue");


#ifndef DBDEBUG
	Sleep(3000);
	ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif

	try
	{
		if (memoryManager.GetBackendKind() == MemoryBackendKind::Driver)
		{
			const HWND gameWindow = Init::Client::FindGameWindow(pid);
			if (gameWindow == nullptr)
				Log::Error("Failed to locate the CS2 game window", false, false);
			else
				Gui.AttachAnotherWindow(gameWindow, Cheats::Run);
		}
		else
		{
			const HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
			MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
			if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo))
				throw std::runtime_error("Failed to resolve the primary monitor bounds");

			const RECT& bounds = monitorInfo.rcMonitor;
			Gui.NewWindow(
				"VoidSpectre",
				Vec2(static_cast<float>(bounds.left), static_cast<float>(bounds.top)),
				Vec2(
					static_cast<float>(bounds.right - bounds.left),
					static_cast<float>(bounds.bottom - bounds.top)),
				Cheats::Run);
		}
	}
	catch (const std::exception& error)
	{
		Log::Error(error.what(), false, false);
	}
	WebRadar::Stop();
}
