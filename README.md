# Firestorm with DLSS5 neural rendering

This fork's **`dlls5` branch** adds experimental DLSS5 neural rendering to
Firestorm on **Windows x64**, using the viewer's existing **OpenGL** renderer.
It runs the ReShade/Feeder neural pass on the world before viewer UI and HUDs are
drawn, so those overlays stay sharp during normal processing. Text baked into
world textures and other world content can still be changed by the neural pass.

This is a community Feeder/RenoDX integration, not an official NVIDIA SDK plugin
or an official Firestorm release. No Vulkan renderer port is required.

## Current status

The helper restart route passed one **1920x1111 → 1006x691 → 1920x1111** cycle,
with actual neural evaluation at all three sizes, zero duplicate Feed calls and
normal viewer exit. Nine automated checks and package installation/restoration
checks pass. The tested machine is an RTX 5070 Laptop GPU with an Intel/NVIDIA
hybrid configuration; other hardware/runtime combinations need verification.

**Resizing currently pauses for about four seconds, and neural rendering returns
after roughly eight seconds.** This is a developer preview. Longer sessions,
other resize/device-loss sequences, broader content quality and runtime use on
a fresh installation remain to be checked. The older in-process v0.8 package
failed resizing; use the v0.9 helper route described here.

## What changed

- A small viewer patch signals when the world is ready, before overlay drawing.
- A separate ReShade observer processes and composites only the world rectangle,
  preserving GL state and checking for duplicate effects at normal presentation.
- A local Feeder helper-client adaptation starts a fresh consumer process for each
  later texture build, bypassing the consumer fault seen during resize recreation.
- Build, install and launch scripts verify component hashes, preserve the working
  RHI configuration and restore temporarily swapped files after normal exit.

See the [integration changes against the upstream base](https://github.com/morgana-app/phoenix-firestorm/compare/0de5043af0...dlls5),
[viewer notification ABI](indra/newview/fsscenesignal.h),
[scene composition](tools/dlss5-observer/early_neural.cpp),
[helper adaptation](tools/dlss5-observer/feeder_helper_restart.inc), and
[architecture, changelog and TODO](doc/dlss5.md).
The integration milestone is on `dlls5`, based on upstream `0de5043af0`.

## How to use it

### Download a GitHub Actions preview

Open [Actions → Build Windows DLSS5 preview](https://github.com/Morgana-app/phoenix-firestorm/actions/workflows/dlss5-windows-preview.yml),
choose **Run workflow**, select `dlls5`, and leave the full viewer build enabled.
After a successful run, download both the viewer and integration artifacts.
The first hosted full viewer build passed on 2026-10-22 and took about 80 minutes.
Follow the [download setup guide](tools/dlss5-observer/CI-DOWNLOAD-README.md).
Builds run manually; no official signing/deployment credentials are required.
The unsigned viewer uses OpenAL/OpenJPEG instead of FMOD/KDU. The integration
download includes the patched helper client and official Feeder host with their
license notices; ReShade, the neural consumer and NVIDIA runtimes/models remain
separate. Artifacts expire after 30 days. A cloud build does not verify neural
rendering on your GPU.

### Build from source

The steps below build the modified viewer and add-ons yourself. This repository
tracks source and packaging tools; generated viewer builds, integration ZIPs,
ReShade/Feeder binaries, neural consumer binaries and NVIDIA models are not tracked.
Installing the observer into stock Firestorm does not provide the required
scene-ready notification.

1. **Check out this branch and build the viewer.**

   ```powershell
   git clone --branch dlls5 https://github.com/morgana-app/phoenix-firestorm.git
   cd phoenix-firestorm
   ```

   Follow the [Windows build guide](doc/building_windows.md) using this checkout
   on `dlls5`. Prepare a separate, runnable folder containing the modified
   `firestorm-bin.exe` and its normal viewer dependencies.

2. **Set up a working RHI/OpenGL baseline in that viewer folder.**

   Use [RHI](https://github.com/RankFTW/RHI) to install ReShade with add-on support
   and the Feeder/consumer stack, selecting **OpenGL** for Firestorm. The tested
   stack uses Feeder **1.19.0**, RenoDX consumer **8.5.0-rc10**, ReShade **6.8.0**
   and Lumenite Kernel **2026.10.17**.
   Obtain external components through their respective distributions and terms.

   Set Generic Depth **enabled**, aspect-ratio heuristics **Match resolution
   exactly**, and copy-before-clears **off**. Set `DLSS5_MV_PROVIDER=3` without a
   conflicting effect-specific override; enable **Lumenite Kernel**, then exactly
   one **DLSS5 Feed**. Use one neural pass, **Upscaled** hook, **style 0**, and
   neural upscaling **off**. Check that the baseline renders correctly first.
   The viewer folder must be writable by your user, or run setup and the launcher
   as administrator. Keep this dependency set fixed while establishing the setup.

3. **Build the observer and the patched helper client.**

   The [source/build instructions](tools/dlss5-observer/PACKAGE-README.md#sourcebuild)
   list the observer's ReShade/GL headers and CMake commands. Build the observer
   into `tools/dlss5-observer/build/v05` if using the packaging defaults.

   Obtain the official [Feeder v1.19.0 source and release ZIP](https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/tag/v1.19.0).
   Add its required Vulkan headers as upstream documents, then build our helper
   adaptation (example dependency location):

   ```powershell
   .\tools\dlss5-observer\Build-FeederHelper.ps1 -SourceDirectory 'C:\deps\DLSS5-Feeder-1.19.0'
   ```

   The Vulkan headers compile Feeder's existing transports; the viewer still
   uses OpenGL. Keep the emitted `build/feeder-helper/manifest.json` and its build
   inputs together. Also extract the release's unchanged
   `host64\dlss5-feed-host64.exe`. The modified helper client is **fsresize1**;
   using the unmodified client does not include the resize workaround.

4. **Create the integration package and install it.**

   ```powershell
   .\tools\dlss5-observer\Build-Package.ps1 -FeederSourceDirectory 'C:\deps\DLSS5-Feeder-1.19.0'
   ```

   Extract the resulting ZIP from `tools/dlss5-observer/build/packages`. From
   that extracted folder, close Firestorm and run:

   ```powershell
   .\Install-FirestormNeural.ps1 -ViewerDirectory 'C:\path\to\modified-viewer' -HelperBuildManifest 'C:\path\to\phoenix-firestorm\tools\dlss5-observer\build\feeder-helper\manifest.json' -HelperHostPath 'C:\deps\feeder-release\host64\dlss5-feed-host64.exe'
   ```

   Substitute your actual paths. A checkout of this branch already contains the
   viewer patch; apply the generated `viewer-signal.patch` only when building
   from the recorded upstream base. The package excludes third-party binaries
   and models; the installer takes the helper files from your local build/download.

5. **Start `Launch-FirestormNeural.cmd` in the modified viewer folder.**

   This is the normal-use launcher and uses your normal Firestorm profile.
   Keep its console open until you quit the viewer normally. It selects the
   helper route, preserves shader parameters and neural intensity, and restores
   the original add-ons/settings after exit. It temporarily selects the helper's
   high-performance GPU; both processes must render on the same compatible GPU.

For recovery, removal and detailed prerequisites, read the
[package guide](tools/dlss5-observer/PACKAGE-README.md).
Logs are archived under the selected viewer's `firestorm-neural/runs` folder.
Check the helper's consumer log for **feature 18 evaluation succeeded** at each
settled size: FPS or composition counts alone do not prove neural rendering.

Developer-only diagnostic launchers and the private test profile are documented
in [the observer guide](tools/dlss5-observer/README.md) and
[the helper experiment](tools/dlss5-observer/HELPER-TEST.md).
See the [architecture, changelog and TODO](doc/dlss5.md) for design decisions,
validation scope and remaining work.

---

## Original Firestorm README


<img align="left" width="100" height="100" src="doc/firestorm_256.png" alt="Logo of Firestorm viewer"/>

**[Firestorm](https://www.firestormviewer.org) is a free client for 3D virtual worlds such as Second Life and various OpenSim worlds where users can create, connect and chat with others from around the world.**

This repository contains the official source code for the Firestorm viewer.

## Open Source

Firestorm is a third party viewer derived from the official [Second Life](https://github.com/secondlife/viewer) client. The client codebase has been open source since 2007 and is available under the LGPL license.

## Download

Pre-built versions of the viewer releases for Windows, Mac and Linux can be downloaded from the [official website](https://www.firestormviewer.org/choose-your-platform/).

## Build Instructions

Build instructions for each operating system can be found using the links below and in the official [wiki](https://wiki.firestormviewer.org).

- [Windows](doc/building_windows.md)
- [Mac](doc/building_macos.md)
- [Linux](doc/building_linux.md)

> [!NOTE]
> We do not provide support for compiling the viewer or issues resulting from using a self-compiled viewer. However, there is a self-compilers group within Second Life that can be joined to ask questions related to compiling the viewer: [Firestorm Self Compilers](https://tinyurl.com/firestorm-self-compilers)

## Contribute

Help make Firestorm better! You can get involved with improvements by filing bugs and suggesting enhancements via [JIRA](https://jira.firestormviewer.org) or [creating pull requests](CONTRIBUTING.md).

## Community respect

This section is guided by the [TPV Policy](https://secondlife.com/corporate/third-party-viewers) and the [Second Life Code of Conduct](https://github.com/secondlife/viewer?tab=coc-ov-file).

Firestorm code is made available during ongoing development, with the **master** branch representing the current nightly build. Developers and self-compilers are encouraged to work on their own forks and contribute back via pull requests, as detailed in the [contributing guide](CONTRIBUTING.md).

If you intend to use our code for your own viewer beyond personal use, please only use code from official release branches (for example, `Firestorm_7.1.13`), rather than from pre-release/preview or nightly builds.
