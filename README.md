<p align="center"><img src="Assets/banner.png"></p><p align="center">  <img src="https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white">
  <img src="https://img.shields.io/badge/Visual_Studio-5C2D91?style=for-the-badge&logo=visual%20studio&logoColor=white">
  <img src="https://img.shields.io/badge/Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white">
  <img src="https://img.shields.io/badge/build-passing-76B900?style=for-the-badge&logo=&logoColor=whit">
  <img src="https://img.shields.io/badge/tests-100/100-76B900?style=for-the-badge&logo=&logoColor=whit">
  <img src="https://img.shields.io/badge/code quality-A+-76B900?style=for-the-badge&logo=&logoColor=whit">
  <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge&logo=&logoColor=whit">
  <img src="https://img.shields.io/badge/DragonBurn-v3.7.10.4-blue?style=for-the-badge&logo=&logoColor=whit">
  <img src="https://img.shields.io/badge/CS2-000000?style=for-the-badge&logo=counter-strike&logoColor=white">
  <img src="https://img.shields.io/badge/Kernel mode-28004D?style=for-the-badge">
  <img src="https://img.shields.io/badge/offsets auto update-D06B57?style=for-the-badge">
  <img src="https://img.shields.io/badge/undetected-03C75A?style=for-the-badge">
  <img src="https://img.shields.io/badge/fullscreen-supported-76B900?style=for-the-badge">
</p>

---

<h3>
<p align="center">
DragonBurn is one of the best CS2 kernel mode read only external cheats. It has a wide range of features, full customization, and automatic offset updates. Undetected by all anti-cheats except faceit.
</p></h3>

<p align="center">
<a href="https://github.com/ByteCorum/DragonBurn/releases/latest/download/DragonBurn.exe">Download latest release</a><br>
⭐Please, star this repo if it was helpful⭐
</p>

---

### 🌐Join our community

<a href="https://discord.gg/5WcvdzFybD"><img src="https://invidget.switchblade.xyz/5WcvdzFybD"></a>

<a href="https://ko-fi.com/bytecorum"><img src="https://img.shields.io/badge/Support Author-F16061?style=for-the-badge&logo=ko-fi&logoColor=white"></a>

---

### 📋Features

Press END key to open/close menu.

<details>
<summary>Visual</summary>

-   Box ESP
-   Box Type
-   Box Rounding
-   Filled Box ESP
-   Gradient Filled Box ESP
-   Skeleton
-   Snap Line
-   Sound esp
-   Bomb esp
-   Bomb carrier esp
-   Visual Color
-   Eye Ray
-   Health Bar
-   Armor Bar
-   Weapon
-   Ammo
-   Distance
-   Name
-   Scoped
-   Blind
-   Blind Hide
-   AWP Crosshair
-   Visual Preview
-   etc
</details>

<details>
<summary>Radar Hack</summary>

-   Point Size
-   Proportion
-   Range
-   Alpha

-   Web radar (`http://127.0.0.1:16668`)
</details>

<details>
<summary>Aimbot</summary>

-   Start Bullet
-   Aim Lock
-   Draw Fov
-   Visible Check
-   Auto Only
-   Flash Check
-   Scope Check
-   Humanization
-   FOV
-   Smooth
-   Multi Hitboxes Selection
</details>

<details>
<summary>RCS</summary>

-   Yaw
-   Pitch
-   Preview
</details>

<details>
<summary>Trigger Bot</summary>

-   Scope Check
-   Flash Check
-   Stop Check
-   Shot Delay
-   Shot Duration
-   TTD
</details>

<details>
<summary>Misc</summary>

-   Bomb Timer
-   Bunny Hop
-   Head Line
-   Hit Sound
-   Hit Markers
-   Auto knife
-   Auto zeus
-   Auto accept
-   Spectator list
-   Watermark
-   Anti Record
</details>

---

### 🛠️How to use

Configure and build with CMake, the Visual Studio 2022 generator, MSVC v143, the Windows Driver Kit, and Node.js `^20.19.0 || >=22.12.0`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T v143
cmake --build build --config Release --parallel
```

Use `--config Debug` for a debug build. CMake builds the user-mode client and mapper directly, and invokes the WDK `.vcxproj` for the kernel driver. Release outputs go to `built/` (`built_dbg/` for Debug):

- `VoidSpectre.exe`
- `VoidSpectre-Mapper.exe`
- `VoidSpectre-Core.sys`
- `memprocfs/`, containing the required FPGA runtime DLLs, `info.db`, and license files

Run `VoidSpectre.exe` as administrator. Startup is driver-first: if the host-derived DragonBurn device is already loaded and accessible, the client uses it and attaches the overlay to the local CS2 window. If and only if the device does not exist, the client loads the adjacent `memprocfs/` runtime and connects to the target through PCILeech FPGA. Driver access errors fail startup instead of switching to a different memory view.

The FPGA fallback requires the FT601 kernel driver on the analysis computer, a supported FPGA connected to the target computer, and the complete `built/memprocfs/` directory beside `VoidSpectre.exe`. In this mode process/module discovery and all reads come from MemProcFS. Rendering uses an opaque borderless full-screen window on the analysis computer; ESP, radar, menu, spectator/bomb/sound displays, and WebRadar remain read-only. Aimbot, RCS, trigger, bunny-hop, AutoAccept, knife/zeus, and other game-input paths are disabled.

After the client finishes startup, open `http://127.0.0.1:16668` on the same computer. The web radar is loopback-only; it streams player snapshots over WebSocket and downloads validated map assets into `Documents\DragonBurn\Data\WebRadarMaps` on demand.

The mapper remains a separate, manually invoked product. When `cfg::image` is empty, it reads `VoidSpectre-Core.sys` from its own directory. Embedded and encrypted `cfg::image` data remains supported. Run it before `VoidSpectre.exe` when the driver backend is desired. Its options are:

- `VoidSpectre-Mapper.exe --securemode`: allocate independent pages and apply per-section protection.
- `VoidSpectre-Mapper.exe --legacyimg`: use embedded `cfg::imageLegacy`, or `VoidSpectre-Core-legacy.sys` beside the mapper.
- `VoidSpectre-Mapper.exe --forceprefs`: force the original Windows kernel-preference prompt.

`VoidSpectre-Core.sys` supports both entry paths: normal Service Control Manager loading and kdmapper-style invocation with null `DriverObject`/`RegistryPath` parameters. Both paths derive the same case-insensitive host-specific token before registering the driver object, device, symbolic link, and IOCTL dispatch table. The device ACL continues to grant access only to SYSTEM and administrators.

---

### ❌Mapper errors

> Error: `Failed to read driver image from the mapper directory`.
>
> Solution: Place `VoidSpectre-Core.sys` next to `VoidSpectre-Mapper.exe`. For `--legacyimg`, provide `VoidSpectre-Core-legacy.sys` or populate `cfg::imageLegacy`.

> Error: `Failed to connect to intel driver`.
>
> Solution: Run the mapper as administrator and check the Windows vulnerable-driver blocklist/HVCI state.

> Error: `Failed to connect to kernel mode driver` after mapping.
>
> Solution: Check the mapper's returned NTSTATUS. The mapper, client, and driver must see the same active computer name so they derive the same `\\.\<32 hexadecimal characters>` path.

---

### 🖼️Preview

<p align="center">
<img src="imgs/img.png">
</p>

<p align="center">
<img src="imgs/img1.png">
</p>

<p align="center">
<img src="imgs/img2.png">
</p>

---

### 📲Contacts

<a href="https://github.com/ByteCorum"><img src="https://img.shields.io/badge/GitHub-100000?style=for-the-badge&logo=github&logoColor=white"></a>
<a href="https://discordapp.com/users/798503509522645012"><img src="https://img.shields.io/badge/Discord-003E54?style=for-the-badge&logo=Discord&logoColor=white"></a>

---

### 💸Support

<a href="https://ko-fi.com/bytecorum"><img src="https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white"></a>

---
