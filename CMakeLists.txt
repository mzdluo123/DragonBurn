cmake_minimum_required(VERSION 3.20)
project(DragonBurnSolution VERSION 3.3.8.0 LANGUAGES C CXX)

set(USERMODE_DIR "${CMAKE_SOURCE_DIR}/DragonBurn-usermode")
set(WEB_RADAR_DIR "${CMAKE_SOURCE_DIR}/DragonBurn-web")
set(WEB_RADAR_ASSETS_HEADER "${USERMODE_DIR}/Resources/WebRadarAssets.generated.h")
find_program(NPM_EXECUTABLE npm REQUIRED)
find_program(NODE_EXECUTABLE node REQUIRED)
file(GLOB_RECURSE WEB_RADAR_SOURCE_INPUTS CONFIGURE_DEPENDS "${WEB_RADAR_DIR}/src/*")
add_custom_command(
    OUTPUT "${WEB_RADAR_ASSETS_HEADER}"
    COMMAND "${NPM_EXECUTABLE}" ci
    COMMAND "${NPM_EXECUTABLE}" run build
    COMMAND "${NODE_EXECUTABLE}" "${WEB_RADAR_DIR}/scripts/embed-assets.mjs"
        "${WEB_RADAR_DIR}/dist" "${WEB_RADAR_ASSETS_HEADER}"
    DEPENDS
        "${WEB_RADAR_DIR}/package.json"
        "${WEB_RADAR_DIR}/package-lock.json"
        "${WEB_RADAR_DIR}/tsconfig.json"
        "${WEB_RADAR_DIR}/tsconfig.app.json"
        "${WEB_RADAR_DIR}/tsconfig.node.json"
        "${WEB_RADAR_DIR}/vite.config.ts"
        "${WEB_RADAR_DIR}/index.html"
        "${WEB_RADAR_DIR}/scripts/embed-assets.mjs"
        ${WEB_RADAR_SOURCE_INPUTS}
    WORKING_DIRECTORY "${WEB_RADAR_DIR}"
    COMMENT "Building and embedding DragonBurn web radar"
    VERBATIM
)
add_custom_target(DragonBurn-web-assets DEPENDS "${WEB_RADAR_ASSETS_HEADER}")

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
    "${CMAKE_SOURCE_DIR}/memprocfs"
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
    Core/DriverMemoryBackend.cpp
    Core/MemProcFsMemoryBackend.cpp
    
    # Features
    Features/Aimbot.cpp
    Features/Misc.cpp
    Features/Radar.cpp
    Features/RCS.cpp
    Features/WebRadar.cpp
    Features/WebRadarMapProvider.cpp
    Libs/civetweb/src/civetweb.c
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
    Core/MemoryBackend.h
    Core/DriverMemoryBackend.h
    Core/MemProcFsMemoryBackend.h
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
    Features/WebRadar.h
    Features/WebRadarMapProvider.h
    Libs/civetweb/src/md5.inl
    Libs/civetweb/src/sort.inl
    Libs/civetweb/src/match.inl
    Libs/civetweb/src/response.inl
    Libs/civetweb/src/sha1.inl
    Libs/civetweb/src/handle_form.inl
    Libs/civetweb/include/civetweb.h
    
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
    Resources/WebRadarAssets.generated.h
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
add_dependencies(DragonBurn-usermode DragonBurn-web-assets)

# Set target name
set_target_properties(DragonBurn-usermode PROPERTIES OUTPUT_NAME "VoidSpectre")

# Include directories for main app
target_include_directories(DragonBurn-usermode PRIVATE ${DRAGONBURN_INCLUDES} "${USERMODE_DIR}/Libs/civetweb/include")

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
        USE_WEBSOCKET
        NO_SSL
        NO_CGI
        NO_CACHING
        NO_FILES
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
    ws2_32
    winhttp
)

set(MEMPROCFS_RUNTIME_FILES
    "${CMAKE_SOURCE_DIR}/memprocfs/vmm.dll"
    "${CMAKE_SOURCE_DIR}/memprocfs/leechcore.dll"
    "${CMAKE_SOURCE_DIR}/memprocfs/leechcore_driver.dll"
    "${CMAKE_SOURCE_DIR}/memprocfs/FTD3XX.dll"
    "${CMAKE_SOURCE_DIR}/memprocfs/FTD3XXWU.dll"
    "${CMAKE_SOURCE_DIR}/memprocfs/vcruntime140.dll"
    "${CMAKE_SOURCE_DIR}/memprocfs/info.db"
    "${CMAKE_SOURCE_DIR}/memprocfs/LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/memprocfs/license_info_all.txt"
    "${CMAKE_SOURCE_DIR}/memprocfs/redist_vcruntime.txt"
)

foreach(MEMPROCFS_RUNTIME_FILE IN LISTS MEMPROCFS_RUNTIME_FILES)
    if(NOT EXISTS "${MEMPROCFS_RUNTIME_FILE}")
        message(FATAL_ERROR "Missing required MemProcFS runtime file: ${MEMPROCFS_RUNTIME_FILE}")
    endif()
endforeach()

add_custom_command(TARGET DragonBurn-usermode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DragonBurn-usermode>/memprocfs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MEMPROCFS_RUNTIME_FILES}
        "$<TARGET_FILE_DIR:DragonBurn-usermode>/memprocfs"
    COMMENT "Staging MemProcFS FPGA runtime"
    VERBATIM
)

enable_testing()
add_executable(DragonBurnWebRadarContracts
    "${USERMODE_DIR}/Tests/WebRadarContracts.cpp"
    "${USERMODE_DIR}/Features/WebRadarMapProvider.cpp"
    "${USERMODE_DIR}/Features/WebRadarMapProvider.h"
)
target_include_directories(DragonBurnWebRadarContracts PRIVATE
    "${USERMODE_DIR}/Libs/json"
    "${USERMODE_DIR}"
)
target_compile_definitions(DragonBurnWebRadarContracts PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(DragonBurnWebRadarContracts PRIVATE winhttp)
add_test(NAME DragonBurnWebRadarContracts COMMAND DragonBurnWebRadarContracts)
set_target_properties(DragonBurnWebRadarContracts PROPERTIES FOLDER "Tests")

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