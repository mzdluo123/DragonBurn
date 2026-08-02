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

if(NOT MSVC)
    message(FATAL_ERROR "DragonBurn requires the MSVC toolchain")
endif()

if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
    message(FATAL_ERROR "Use a Visual Studio generator so the WDK driver project can be built")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "DragonBurn supports x64 builds only")
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
)

list(TRANSFORM DRAGONBURN_SOURCES PREPEND "${USERMODE_DIR}/")
list(TRANSFORM DRAGONBURN_HEADERS PREPEND "${USERMODE_DIR}/")
list(TRANSFORM DRAGONBURN_RESOURCES PREPEND "${USERMODE_DIR}/")
set(DRAGONBURN_SHARED_HEADERS "${CMAKE_SOURCE_DIR}/Shared/DragonBurnProtocol.h")

# Create the main executable
add_executable(DragonBurn-usermode ${DRAGONBURN_SOURCES} ${DRAGONBURN_HEADERS} ${DRAGONBURN_RESOURCES} ${DRAGONBURN_SHARED_HEADERS})

# Set target name
set_target_properties(DragonBurn-usermode PROPERTIES OUTPUT_NAME "VoidSpectre")

# Include directories for main app
target_include_directories(DragonBurn-usermode PRIVATE ${DRAGONBURN_INCLUDES})

# Compiler-specific options for main app
if(MSVC)
    target_compile_options(DragonBurn-usermode PRIVATE /W3 /sdl-)
    
    target_compile_definitions(DragonBurn-usermode PRIVATE
        $<$<CONFIG:Debug>:DBDEBUG>
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        _CONSOLE
        UNICODE
        _UNICODE
    )
    
    set_property(TARGET DragonBurn-usermode PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    
    set_target_properties(DragonBurn-usermode PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE /DEBUG"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF"
    )
    
    set_target_properties(DragonBurn-usermode PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

# Platform-specific libraries for main app
target_link_libraries(DragonBurn-usermode PRIVATE
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
# DragonBurn Mapper
#############################################

set(MAPPER_DIR "${CMAKE_SOURCE_DIR}/DragonBurn-kernel")
set(MAPPER_SOURCES
    "${MAPPER_DIR}/intel_driver.cpp"
    "${MAPPER_DIR}/main.cpp"
    "${MAPPER_DIR}/kdmapper.cpp"
    "${MAPPER_DIR}/portable_executable.cpp"
    "${MAPPER_DIR}/service.cpp"
    "${MAPPER_DIR}/utils.cpp"
)
set(MAPPER_HEADERS
    "${MAPPER_DIR}/include/cfg.h"
    "${MAPPER_DIR}/include/crypto.h"
    "${MAPPER_DIR}/include/intel_driver.h"
    "${MAPPER_DIR}/include/intel_driver_resource.h"
    "${MAPPER_DIR}/include/json.hpp"
    "${MAPPER_DIR}/include/kdmapper.h"
    "${MAPPER_DIR}/include/logger.h"
    "${MAPPER_DIR}/include/nt.h"
    "${MAPPER_DIR}/include/portable_executable.h"
    "${MAPPER_DIR}/include/service.h"
    "${MAPPER_DIR}/include/utils.h"
    "${MAPPER_DIR}/include/web_api.h"
    "${MAPPER_DIR}/resource.h"
)
set(MAPPER_RESOURCES
    "${MAPPER_DIR}/Resource.rc"
)

add_executable(DragonBurn-kernel ${MAPPER_SOURCES} ${MAPPER_HEADERS} ${MAPPER_RESOURCES} ${DRAGONBURN_SHARED_HEADERS})
set_target_properties(DragonBurn-kernel PROPERTIES
    OUTPUT_NAME "VoidSpectre-Mapper"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "Mapper"
    LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    LINK_FLAGS_DEBUG "/DEBUG"
    LINK_FLAGS_RELEASE "/OPT:REF /OPT:ICF"
)
target_include_directories(DragonBurn-kernel PRIVATE "${MAPPER_DIR}/include")
target_compile_options(DragonBurn-kernel PRIVATE
    /W4
    /sdl
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Oi>
    $<$<CONFIG:Release>:/MP>
)
target_compile_definitions(DragonBurn-kernel PRIVATE
    $<$<CONFIG:Debug>:DBDEBUG>
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    UNICODE
    _UNICODE
)
target_link_libraries(DragonBurn-kernel PRIVATE version)

source_group(TREE "${MAPPER_DIR}" PREFIX "DragonBurn-kernel" FILES
    ${MAPPER_SOURCES}
    ${MAPPER_HEADERS}
    ${MAPPER_RESOURCES}
)

#############################################
# DragonBurn WDK Driver
#############################################

set(DRIVER_PROJECT "${CMAKE_SOURCE_DIR}/DragonBurn-driver/DragonBurn-driver.vcxproj")
add_custom_target(DragonBurn-driver ALL
    COMMAND "${CMAKE_VS_MSBUILD_COMMAND}" "${DRIVER_PROJECT}"
        /nologo
        /t:Build
        "/p:Configuration=$<CONFIG>"
        /p:Platform=x64
        "/p:SolutionDir=${CMAKE_SOURCE_DIR}/"
        /verbosity:minimal
    SOURCES
        "${DRIVER_PROJECT}"
        "${CMAKE_SOURCE_DIR}/DragonBurn-driver/Driver.cpp"
        "${CMAKE_SOURCE_DIR}/Shared/DragonBurnProtocol.h"
    USES_TERMINAL
    VERBATIM
)
set_target_properties(DragonBurn-driver PROPERTIES FOLDER "Driver")
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
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DragonBurn-usermode)

# Set folder for main project in IDE
set_target_properties(DragonBurn-usermode PROPERTIES FOLDER "Main")