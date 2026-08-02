cmake_minimum_required(VERSION 3.20)
project(DragonBurnSolution VERSION 3.3.8.0 LANGUAGES CXX)

set(USERMODE_DIR "${CMAKE_SOURCE_DIR}/DragonBurn-usermode")

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform check
if(NOT WIN32)
    message(FATAL_ERROR "This project is designed for Windows only")
endif()

# Configuration types
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/built_dbg")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/built")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/built_dbg/cache")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/built/cache")

# Enable solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#############################################
# DragonBurn Main Application
#############################################

# Include directories for main app
set(DRAGONBURN_INCLUDES
    "${USERMODE_DIR}/Libs/json"
    "${USERMODE_DIR}/OS-ImGui/imgui"
    "${USERMODE_DIR}/OS-ImGui/stb"
    "${USERMODE_DIR}"
    "${CMAKE_SOURCE_DIR}"
)

# Source files for main app
set(DRAGONBURN_SOURCES
    # Main
    main.cpp
    
    # Config
    Config/ConfigMenu.cpp
    Config/ConfigSaver.cpp
    
    # Core
    Core/Cheats.cpp
    Core/GlobalVars.cpp
    Core/MemoryMgr.cpp
    
    # Features
    Features/Aimbot.cpp
    Features/Misc.cpp
    Features/Radar.cpp
    Features/RCS.cpp
    Features/TriggerBot.cpp
    
    # Game
    Game/Bone.cpp
    Game/Entity.cpp
    Game/Game.cpp
    
    # Helpers
    Helpers/Mouse.cpp
    Helpers/UIAccess.cpp
    Helpers/StorageMgr.cpp
    
    # Offsets
    Offsets/Offsets.cpp
    
    # ImGui
    OS-ImGui/imgui/imgui.cpp
    OS-ImGui/imgui/imgui_demo.cpp
    OS-ImGui/imgui/imgui_draw.cpp
    OS-ImGui/imgui/imgui_impl_dx11.cpp
    OS-ImGui/imgui/imgui_impl_win32.cpp
    OS-ImGui/imgui/imgui_tables.cpp
    OS-ImGui/imgui/imgui_widgets.cpp
    
    # OS-ImGui
    OS-ImGui/OS-ImGui.cpp

    OS-ImGui/OS-ImGui_Base.cpp
    OS-ImGui/OS-ImGui_External.cpp
)

# Header files for main app
set(DRAGONBURN_HEADERS
    # Config
    Config/ConfigMenu.h
    Config/ConfigSaver.h
    
    # Core
    Core/Config.h
    Core/Cheats.h
    Core/Globals.h
    Core/GlobalVars.h
    Core/GUI.h
    Core/Init.h
    Core/MemoryMgr.h
    Core/Render.h
    
    # Features
    Features/Aimbot.h
    Features/BombTimer.h
    Features/ESP.h
    Features/Misc.h
    Features/Radar.h
    Features/RCS.h
    Features/SoundESP.h
    Features/SpectatorList.h
    Features/TriggerBot.h
    
    # Game
    Game/Bone.h
    Game/Entity.h
    Game/Game.h
    Game/View.h
    
    # Helpers
    Helpers/GetWeaponIcon.h
    Helpers/KeyManager.h
    Helpers/Logger.h
    Helpers/Mouse.h
    Helpers/UIAccess.h
    Helpers/WebApi.h
    Helpers/StorageMgr.h
    
    # Offsets
    Offsets/Offsets.h
    
    # ImGui
    OS-ImGui/imgui/imconfig.h
    OS-ImGui/imgui/imgui.h
    OS-ImGui/imgui/imgui_impl_dx11.h
    OS-ImGui/imgui/imgui_impl_win32.h
    OS-ImGui/imgui/imgui_internal.h
    OS-ImGui/imgui/imstb_rectpack.h
    OS-ImGui/imgui/imstb_textedit.h
    OS-ImGui/imgui/imstb_truetype.h
    
    # OS-ImGui
    OS-ImGui/OS-ImGui.h
    OS-ImGui/OS-ImGui_Base.h
    OS-ImGui/OS-ImGui_Exception.h
    OS-ImGui/OS-ImGui_External.h
    OS-ImGui/OS-ImGui_Struct.h
    
    # STB
    OS-ImGui/stb/stb_image.h
    
    # Resources
    Resources/resource.h
    Resources/Font.h
    Resources/Images.h
    Resources/Language.h
    Resources/Sounds.h
    Resources/WeaponIcon.h
)

# Resource files for main app
set(DRAGONBURN_RESOURCES
    Resources/Resource.rc
    Resources/icon.ico
)

list(TRANSFORM DRAGONBURN_SOURCES PREPEND "${USERMODE_DIR}/")
list(TRANSFORM DRAGONBURN_HEADERS PREPEND "${USERMODE_DIR}/")
list(TRANSFORM DRAGONBURN_RESOURCES PREPEND "${USERMODE_DIR}/")
set(DRAGONBURN_SHARED_HEADERS "${CMAKE_SOURCE_DIR}/Shared/DragonBurnProtocol.h")

# Create the main executable
add_executable(DragonBurn WIN32 ${DRAGONBURN_SOURCES} ${DRAGONBURN_HEADERS} ${DRAGONBURN_RESOURCES} ${DRAGONBURN_SHARED_HEADERS})

# Set target name
set_target_properties(DragonBurn PROPERTIES OUTPUT_NAME "DragonBurn")

# Include directories for main app
target_include_directories(DragonBurn PRIVATE ${DRAGONBURN_INCLUDES})

# Compiler-specific options for main app
if(MSVC)
    target_compile_options(DragonBurn PRIVATE /W3 /sdl-)
    
    target_compile_definitions(DragonBurn PRIVATE
        $<$<CONFIG:Debug>:DBDEBUG>
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        _CONSOLE
        UNICODE
        _UNICODE
    )
    
    set_property(TARGET DragonBurn PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    
    set_target_properties(DragonBurn PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE /DEBUG"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF"
    )
    
    set_target_properties(DragonBurn PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

# Platform-specific libraries for main app
target_link_libraries(DragonBurn PRIVATE
    d3d11
    dxgi
    user32
    kernel32
    gdi32
    winspool
    comdlg32
    advapi32
    shell32
    ole32
    oleaut32
    uuid
    odbc32
    odbccp32
)

#############################################
# Kernel Driver
#############################################

# DragonBurn-driver is a WDK kernel-mode project. Build DragonBurn.sln with
# Visual Studio 2022/MSBuild so the WDK targets and signing policy are applied.

#############################################
# IDE Organization
#############################################

source_group(TREE "${USERMODE_DIR}" PREFIX "DragonBurn" FILES
    ${DRAGONBURN_SOURCES}
    ${DRAGONBURN_HEADERS}
    ${DRAGONBURN_RESOURCES}
)
source_group("Shared" FILES ${DRAGONBURN_SHARED_HEADERS})

# Set main app as startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DragonBurn)

# Set folder for main project in IDE
set_target_properties(DragonBurn PROPERTIES FOLDER "Main")