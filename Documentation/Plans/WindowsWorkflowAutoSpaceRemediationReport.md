# 🔍 `WindowsWorkflowAutoSpaceRemediationReport` — Merge Integration, Windows CI Diagnostics & Cross-Build Verification (`2026-11-25`)

> **TL;DR**: Integrated commit `6e27c37` from `egoistinbox-hue/Frontier` (`arena/2809d087-frontier`) onto `arena/1552dd30-frontier` (`27e4c41`), diagnosed why GitHub Actions run `45511631723` failed on `windows-2022` (`Install Ninja and sccache` unretried `choco install` failure before CMake configure), resolved Windows `<winnt.h>` / `slangc` cross-compile defects (`SocketTransport.cpp`, `CMakeLists.txt`), updated `.github/workflows/windows-build.yml` plus `.github/workflows/frontier-ci.yml` to build, bundle, and headlessly verify `AutoSpace`, and verified full Windows x64 `Editor` + `Shipping` builds.

---

## 🔍 1. Root-Cause Findings (`Frontier Windows Build`)

| Symptom / Location | Root Cause | Remediation Applied |
| :--- | :--- | :--- |
| **🔴 Run `45511631723` failed at `Install Ninja and sccache` (`.github/workflows/windows-build.yml`)** | Unretried `choco install ninja -y` / `choco install sccache -y` exited `1` on Chocolatey community feed instability before CMake ran; subsequent steps (`pip`, `PrepareDependencies.py`, `PrepareFonts.py`, `PrepareShaderToolchain.py`) also lacked retry guards | Replaced `choco install` with cached, SHA-256-verified direct GitHub Release zip downloads (`ninja v1.13.2`, `sccache v0.20.1`) with 4-attempt retry + explicit `$LASTEXITCODE` checks, plus 3-attempt retry loops on setup scripts |
| **🔴 `Engine/NetworkingSpace/Transports/SocketTransport.cpp:44` compile failure on Windows (`<winnt.h>`)** | Anonymous-namespace `RotateLeft32` collided with `<winnt.h>` `#define RotateLeft32 _rotl` pulled in via `<winsock2.h>` / `<ws2tcpip.h>` | Renamed helper to `ChaChaRotateLeft32` in `Engine/NetworkingSpace/Transports/SocketTransport.cpp` |
| **🔴 `CMakeLists.txt:319` cross-compile `FRONTIER_SLANGC_BIN` selection** | `if(WIN32)` checked target OS rather than `CMAKE_HOST_WIN32`, unconditionally selecting `slangc.exe` even during Linux-to-Windows cross-compilation | Updated `CMakeLists.txt` line `319` to check `if(CMAKE_HOST_WIN32)` so host `slangc` matches the build machine |
| **⚠️ `AutoSpace` omitted from Windows CI build, portable folder, and trigger paths** | `.github/workflows/windows-build.yml` and `.github/workflows/frontier-ci.yml` omitted `Projects/**` / `Content/**` push paths, omitted `AutoSpace` target, and omitted `Projects/AutoSpace` + `Content/` from portable payloads | Added `Projects/**` + `Content/**` trigger paths, added `AutoSpace` build target, copied `AutoSpace(.exe)`, `Projects/AutoSpace/`, and `Content/` into portable packages, added `WorldHost.Initialize()` in `AutoSpaceLifecycle.cpp`, and added headless `--verify` smoke execution |

---

## 🛠️ 2. Verified Tool SHA-256 Checksums & Workflow Changes

| Component | Source / Verification | Value / Status |
| :--- | :--- | :--- |
| **Ninja `v1.13.2` (`ninja-win.zip`)** | GitHub Release download (`actions/cache@v4` under `${{ runner.temp }}/build-tools`) | SHA-256: `07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65` |
| **`sccache` `v0.20.1` (`sccache-v0.20.1-x86_64-pc-windows-msvc.zip`)** | GitHub Release download (`actions/cache@v4` under `${{ runner.temp }}/build-tools`) | SHA-256: `ca500a01139e9af8d5f5a5e69b6a0dd941ed79ed3399a91cc35bba484fe732fa` |
| **`Projects/AutoSpace/Source/AutoSpaceLifecycle.cpp`** | Calls `(void)WorldHost.Initialize()` before `WorldHost.LoadWorldSpaceFile(...)` | Verified active host mount on `MainWorld.WorldSpace` (`0` geometry instances, Sun + Clear Sky active) |

---

## ✔️ 3. Cross-Build & Payload Verification Ledger

| Target / Check | Configuration | Outcome | Notes |
| :--- | :---: | :---: | :--- |
| **Windows x64 (`Editor`)** | `Automation/WindowsToolchain.cmake` (Zig `0.13.0` / Clang 18, Slang `182` `.spv`) | 🟢 **PASS** | `Frontier.exe`, `FrontierSketcher.exe`, `Projects/AutoSpace/AutoSpace.exe`, `Projects/Untitled/Untitled.exe`, `Binaries/Win64/libFrontier{Engine,WorkspaceUi}.dll` compiled and linked cleanly (`0` errors) |
| **Windows x64 (`Shipping`)** | `Automation/WindowsToolchain.cmake` (`-DFRONTIER_PROFILE=Shipping`) | 🟢 **PASS** | `FrontierEngine`, `FrontierEngineRuntimeDll`, `AutoSpace.exe`, `Untitled.exe` compiled and linked cleanly (`0` errors) |
| **POSIX Header / Empty-Geometry Safety Audit** | `Engine/` & `Projects/` static inspection | 🟢 **PASS** | Zero `__builtin_` or `M_PI` uses; OS headers guarded; `ShowcaseGalleryScene` and `VulkanWorldRenderSequence` safely handle `ClusterInstances.empty()` |
| **GitHub Actions Windows Live Run (`45545274966`)** | Remote `windows-2022` CI (`msvc-dev-cmd`, `cl`, `Ninja`, `sccache`) | 🟢 **PASS** | Completed in `16m52s`; `Install Ninja and sccache`, MSVC compile, portable `AutoSpace.exe --headless --verify`, and `Frontier-Workspaces-Windows-x64` upload succeeded |
| **GitHub Actions Linux Live Run (`45545274940`)** | Remote `ubuntu-24.04` CI (`Editor` + `Shipping`) | 🟢 **PASS** | Completed in `21m20s`; `Frontier`, `SolidArc`, `AutoSpace`, portable `AutoSpace --headless --verify`, and `Frontier-Workspaces-Linux-x64` upload succeeded |
