# Repository Guidelines

## Project Overview

DragonBurn is a Windows-only C++ solution for a read-only external CS2 client. It combines a user-mode overlay (`VoidSpectre.exe`), a kernel mapper (`VoidSpectre-Mapper.exe`), and a WDK driver (`VoidSpectre-Core.sys`). The client reads game state through the driver, renders ImGui features, and updates game offsets at startup.

## Architecture & Data Flow

- `DragonBurn-usermode/main.cpp` parses `/` or `--` flags, prepares UI access, creates the config directory, connects or auto-maps the driver, attaches to `cs2.exe`, updates offsets, initializes addresses, and starts the overlay.
- `DragonBurn-usermode/Core/Cheats.cpp::Cheats::Run` is the per-frame coordinator: read view/local-player state, batch entity reads, update entity caches, then run visual, radar, misc, aim, trigger, and spectator features.
- `MemoryMgr` sends `DeviceIoControl` requests defined in `Shared/DragonBurnProtocol.h`. `DragonBurn-driver/Driver.cpp` validates the ABI and addresses, attaches to the target process, and returns read data.
- `EntityBatchProcessor` in `DragonBurn-usermode/Game/Entity.*` performs staged bulk reads. Keep high-frequency work batched and avoid allocations or blocking work in the render loop.
- Runtime state is intentionally global: `memoryManager`, `gGame`, `g_globalVars`, and inline configuration namespaces in `Core/Config.h`. There is no dependency-injection container or application state store. `Config/ConfigSaver.cpp` persists that state as JSON.
- User-mode failures normally propagate as `bool` and fail fast with early returns. Driver code reports `NTSTATUS` and completes IRPs. Exceptions are mainly caught at startup, filesystem, offset-update, and GUI boundaries.
- There is no coroutine/task framework. Timing uses `Sleep` or `std::this_thread::sleep_for`; isolated runtime work may use detached `std::thread`. Do not introduce a second concurrency model without a concrete need.

Protocol changes are cross-layer changes: update both user-mode and driver consumers of `Shared/DragonBurnProtocol.h`, preserve packet layout and size limits, and keep its `static_assert` ABI checks valid.

## Key Directories

| Path | Purpose |
| --- | --- |
| `DragonBurn-usermode/Core/` | Startup helpers, memory/driver client, frame orchestration, GUI and global state. |
| `DragonBurn-usermode/Game/` | Game addresses, entities, bones, and batched entity hydration. |
| `DragonBurn-usermode/Features/` | ESP, aimbot, radar, RCS, trigger, misc, and related feature logic. |
| `DragonBurn-usermode/Config/` | Menu and JSON configuration persistence. |
| `DragonBurn-usermode/Offsets/` | Offset definitions and network update logic. |
| `DragonBurn-usermode/OS-ImGui/`, `Libs/` | Vendored UI and third-party code; avoid unrelated edits. |
| `DragonBurn-driver/` | WDK kernel driver and IOCTL dispatch/read implementation. |
| `DragonBurn-kernel/` | Vulnerable-driver loader and kdmapper-based mapper. |
| `Shared/` | User/kernel wire protocol and host-derived device naming. |
| `Crypto/` | Standalone image-encryption utility included by the checked-in solution. |
| `.github/` | CI build, ownership, issue templates, and PR template. |
| `docs/` | Commit-message guidance and roadmap; no helper scripts or runbook exist. |

Generated/local directories such as `build/`, `built/`, `built_dbg/`, `.vs/`, and `out/` are not source of truth.

## Development Commands

Run these from the repository root in a Visual Studio/WDK-capable shell:

```powershell
# Configure the Visual Studio 2022 x64 build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T v143

# Build all CMake targets
cmake --build build --config Release --parallel
cmake --build build --config Debug --parallel

# Match the CI build of the checked-in solution
msbuild DragonBurn.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
```

Release outputs go to `built/`; Debug outputs go to `built_dbg/`. The main artifacts are `VoidSpectre.exe`, `VoidSpectre-Mapper.exe`, and `VoidSpectre-Core.sys`; solution builds also include `VoidSpectre-Crypto.exe`.

Run `VoidSpectre.exe` as administrator from a working directory containing the mapper and driver image. Supported client flags are `--securemode`, `--legacyimg`, and `--forceprefs` (the `/flag` forms also work). The mapper loads `VoidSpectre-Core.sys` from the adjacent/current runtime directory when no embedded image is configured.

## Code Conventions & Common Patterns

- C++ standards vary by target: user-mode and driver use C++20; mapper and Crypto use C++17.
- Source style is not fully uniform and no formatter is configured. Match the edited file. Project code commonly uses `#pragma once`, PascalCase types/functions, feature/config namespaces, and braces on the next line; local variable casing is mixed.
- Keep Windows strings/API variants consistent with the call site (`WCHAR`/wide APIs for process, module, and device names).
- Prefer existing `bool`/`NTSTATUS` error propagation and early returns. Log actionable initialization errors; do not throw through the frame loop, IOCTL dispatch, or kernel boundaries.
- Validate pointers, ranges, sizes, arithmetic overflow, request counts, and IRQL before memory access. Failed read buffers should not expose stale data.
- Preserve the existing batch-read pattern. `MemoryMgr::ReadMemory` is limited by `Protocol::MaxSingleReadSize`; bulk entity data belongs in batch requests rather than repeated one-off reads.
- Feature configuration belongs in the existing namespaces in `Core/Config.h` and must be wired through `ConfigSaver.cpp` when it is persistent. Do not add a parallel settings system.
- Keep feature orchestration in `Cheats::Run` and feature-specific behavior under `Features/`; game-memory hydration belongs under `Game/`.
- Commit subjects follow `type(optional scope): description`, at most 50 characters, using `feat`, `fix`, `docs`, `style`, `refactor`, `test`, or `chore` (`docs/CommitMessagesGuidance.md`).

## Important Files

- `CMakeLists.txt`: authoritative CMake requirements, targets, sources, libraries, output names, and directories.
- `DragonBurn.sln`: checked-in Visual Studio solution used by CI; includes user-mode, mapper, driver, and Crypto projects.
- `DragonBurn-usermode/main.cpp`: user-mode entry and complete startup sequence.
- `DragonBurn-usermode/Core/Cheats.cpp`: frame-level control and feature ordering.
- `DragonBurn-usermode/Core/MemoryMgr.{h,cpp}`: validated user-mode IOCTL and batch-read client.
- `DragonBurn-usermode/Core/Config.h`: inline global configuration/state.
- `DragonBurn-usermode/Game/Entity.{h,cpp}`: entity model and staged batch processing.
- `Shared/DragonBurnProtocol.h`: shared ABI, IOCTLs, limits, and device-name derivation.
- `DragonBurn-driver/Driver.cpp`: driver entry, device setup, dispatch, and memory-read implementation.
- `DragonBurn-kernel/main.cpp`: mapper flags and mapping flow.
- `.github/workflows/build-check.yml`: actual CI command and archived artifact.
- `README.md`: usage and runtime troubleshooting; verify names against build files because some release/version text is stale.

## Runtime/Tooling Preferences

- Required platform: Windows x64. CMake explicitly rejects non-Windows, non-MSVC, non-Visual-Studio, and 32-bit configurations.
- Required tools: Visual Studio 2022, MSVC v143, CMake 3.20+, and the Windows Driver Kit. The driver project pins Windows SDK/WDK `10.0.26100.0`.
- No package manager, dependency lockfile, `.env`, helper-script layer, formatter, linter, or separate type-check command is configured.
- Prefer CMake for local all-target builds and the exact MSBuild command above when reproducing CI.
- Do not treat `.vs/launch.vs.json` or generated `build/` files as canonical; the launch config contains a stale `DragonBurn.exe` path while current outputs are named `VoidSpectre*`.

## Testing & QA

- No project test directory, test framework, fixtures/mocks, CTest registration, coverage tool, or coverage threshold is present. The README test badge is not executable evidence.
- CI currently performs only a Release x64 rebuild and archives `built/VoidSpectre.exe`; it does not run tests, lint, formatting, static analysis, or coverage.
- Minimum validation for any code change is the narrowest affected Debug or Release x64 build. Match CI with the Release MSBuild command before integration-sensitive changes.
- Behavioral changes require a manual smoke check of the affected path: startup/driver connection, mapper invocation, protocol request, entity read, feature render/input, or config save/load as applicable. State the exact scenario exercised.
- For shared protocol or driver changes, build both user-mode and driver targets and verify connection plus the affected IOCTL end to end. For render-loop changes, check active and inactive-window paths and ensure failures reset cached/runtime state rather than using stale data.
- If adding the first automated tests, create an explicit test target and document its command; do not infer conventions from vendored ImGui or nlohmann-json assertions.
