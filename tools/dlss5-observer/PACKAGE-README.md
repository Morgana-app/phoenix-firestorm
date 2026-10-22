# Firestorm neural rendering — helper preview 0.9

This developer integration processes the world before UI/HUD. The helper restart
workaround passed one 1920x1111 → 1006x691 → 1920x1111 cycle with actual neural
rendering at all sizes and normal Firestorm exit. **Resizing pauses for about
four seconds; neural output returns after roughly eight seconds.** World text
can still change. Visual quality, long sessions and other resize/device-loss
sequences are not certified.

Requires Windows x64, a Firestorm build with the included scene-notification
patch, and a working RHI/OpenGL setup. **Stock Firestorm is not supported.**
The old in-process v0.8 preview failed resizing; this package installs the
separate helper route instead.

## Install and launch

For prebuilt GitHub Actions downloads, follow [the download setup guide](https://github.com/Morgana-app/phoenix-firestorm/blob/dlls5/tools/dlss5-observer/CI-DOWNLOAD-README.md).
The separate CI download includes a patched helper client, portable manifest and
official host with license notices. The integration ZIP itself still excludes
those binaries. The installer accepts that portable manifest as well as a local
build manifest; neural consumer and NVIDIA runtimes/models remain separate.

Third-party binaries/models are not in this ZIP. Before installation, obtain:

- Official DLSS5-Feeder v1.19.0 source and release ZIP from
  https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/tag/v1.19.0.
- A locally built `1.19.0-fsresize1` helper client and its `manifest.json`, produced
  by the supplied `Build-FeederHelper.ps1` (build instructions below).
- The unchanged release file `host64\dlss5-feed-host64.exe`.

Close Firestorm and extract this package. In PowerShell run:

```powershell
.\Install-FirestormNeural.ps1 -ViewerDirectory 'C:\path\to\patched-firestorm' -HelperBuildManifest 'C:\path\to\helper-build\manifest.json' -HelperHostPath 'C:\path\to\feeder-release\host64\dlss5-feed-host64.exe'
```

The folder must be writable by your user; otherwise run the installer and
launcher as administrator. Installation checks package hashes, the viewer patch
marker, RHI dependencies and helper hashes. The marker is a prerequisite check,
not certification of arbitrary viewer builds. It copies integration scripts,
our observer and your supplied helper files into `firestorm-neural`. It preserves
an existing baseline observer and creates one only if absent. It does not modify
the viewer EXE or existing RHI DLLs/settings during installation.

**Use `Launch-FirestormNeural.cmd` in the selected viewer folder.** Keep its
console open until you quit Firestorm normally. This launcher selects the helper
route and uses your normal viewer profile. Logs are in `firestorm-neural\runs`.

The launcher temporarily swaps Feeder/observer files, holds the in-process
consumer outside ReShade's add-on search directory, and runs that consumer only
in `host64`. Current RHI runtimes/models are copied locally into that helper
folder. It chooses Kernel → Feed, style 0, Upscaled hook and no neural upscaling,
while preserving shader parameters and neural intensity. Helper hotkeys are
disabled; adjust intensity in the baseline settings between sessions. A temporary
Windows GPU preference selects the high-performance adapter for the helper and
is restored on exit. On multi-NVIDIA-GPU systems both processes must use the same
GPU; this package has only been tested on an Intel/NVIDIA laptop.

RHI baseline: OpenGL, ReShade add-on support, Generic Depth enabled with **Match
resolution exactly**, and `DLSS5_MV_PROVIDER=3` with Lumenite Kernel installed.
The launcher restores the original add-ons/configuration after exit. It does
not repair the consumer's internal lifecycle: it bypasses recreation by using a
fresh process. Helper shutdown can require forced termination through Feeder's
existing shutdown path; graceful consumer teardown is not established.

## Recovery and removal

If interrupted, close Firestorm and its helper first. Preserve the run logs.
If a run contains `held-copy-settings`, restore its `ReShade.ini` and
`dlss5-feed.cfg` to the viewer folder. Then restore each file actually held in
`firestorm-neural\runs\held-helper-session` to the viewer folder, using
`original-hashes.json` to verify it. Restore held files before deleting their
folders or any `running.lock`. Do not start a second launcher over a held session.

To remove, close both processes and recover any held session first. Remove
`Launch-FirestormNeural.cmd` and the integration's `firestorm-neural` folder after
saving needed logs. `helper-components.json` records whether the installer created
the root observer; remove it only if it was created by this installer. The `host64`
folder can be removed if it contains this integration's ownership marker and is
no longer needed. Existing RHI files remain in place.

## Source/build

Own source/tests, viewer patch and license notices are included. The helper
client/host, ReShade, consumer, shaders, NVIDIA DLLs/models and full viewer binary
are excluded. No public upload is performed. Hashes provide local integrity,
not a publisher signature.

Apply `viewer-signal.patch` to its manifest's recorded Firestorm base revision,
then follow the viewer's normal build instructions. For the observer, supply
ReShade v6.8.0 source headers, Khronos GL headers and a Shaders directory with
`ReShade.fxh`:

```powershell
cmake -S source/tools/dlss5-observer -B build -A x64 -DRESHADE_INCLUDE_DIR=C:/deps/reshade-6.8.0/include -DGL_HEADERS_DIR=C:/deps/gl-headers -DIDENTITY_SHADER_INCLUDE_DIR=C:/path/to/reshade-shaders/Shaders
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use only `build/Release/firestorm_scene_observer.addon64`; similarly named test
fixtures are not installable.

For the helper client, extract official Feeder v1.19.0 source, add Khronos
Vulkan-Headers v1.3.290 under its `external/vulkan` as upstream documents, then run:

```powershell
.\source\tools\dlss5-observer\Build-FeederHelper.ps1 -SourceDirectory 'C:\deps\DLSS5-Feeder-1.19.0'
```

This needs Visual Studio C++ Build Tools, not the neural SDK. Vulkan headers
compile upstream's existing transports; Firestorm still runs OpenGL. The script
verifies pristine source, applies the local opt-in restart adaptation, builds
with upstream's helper command and emits `build/feeder-helper/manifest.json`
beside the supplied build script. Pass that manifest to the installer. The build
manifest retains local absolute input paths and must remain with that build.
See the included Feeder MIT, ReShade and LGPL notices and upstream dependencies'
licenses when working with their components.
