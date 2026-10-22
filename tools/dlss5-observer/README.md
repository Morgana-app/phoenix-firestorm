# Firestorm Scene Observer

Windows x64 ReShade add-on that runs the Kernel/Feed neural chain at Firestorm's
explicit scene-ready boundary, before UI/HUD drawing. The current v0.9 helper
preview uses a separate consumer process and restarts it for later texture
builds to recover from the observed resize recreation fault.

Read the [architecture, changelog and TODO](../../doc/dlss5.md) for the data flow,
validated scope and known limits. Use the [download guide](CI-DOWNLOAD-README.md)
or [source/package guide](PACKAGE-README.md) for setup. Normal use starts
`Launch-FirestormNeural.cmd` in the modified viewer folder; keep its console open
until normal exit restores the original configuration. The viewer folder must
be writable by your user, or setup and launching require administrator execution.

## Effect-chain restriction

UI protection requires exactly **Lumenite Kernel → DLSS5 Feed**, with no other
enabled techniques. `early_neural::run()` rejects a chain containing any extra
technique, such as RTAO. Processing then falls back to the ordinary ReShade chain
after UI/HUD drawing; this also removes DLSS5's early UI protection. Other
effects may affect overlays without necessarily blurring them. Supporting
additional effects requires extending the early-chain policy and validation.

## ReShade UI in helper mode

The normal in-process consumer supplies the full **DLSS 5 Neural Rendering**
panel. Here the consumer runs in a hidden helper, whose overlay/hotkeys are
disabled; the viewer shows the helper client's controls instead, including the
**DLSS 5 neural-rendering settings (on the host)** mirror under Add-ons → DLSS 5
Feed. Its **Apply to the DLSS 5 host** button writes settings and restarts the
helper; slider edits alone do not apply them. **Reload from host** rereads its
stored values. This UI difference does not establish a version difference.
Consumer settings are copied
from the viewer's root `ReShade.ini` at launch, with style 0, Upscaled hook,
neural uplift on and neural upscaling off selected. Changes applied through the
host panel are not copied back to the viewer baseline, so the next launcher
session resets them. Instant live updates and persistent synchronization remain
unimplemented. See the
[package guide](PACKAGE-README.md#why-the-neural-settings-ui-differs-from-normal-rhi)
for persistent settings changes between sessions.

## Build and tests

Requires Windows x64, MSVC, Windows SDK, CMake, ReShade 6.8.0 source/headers,
Khronos GL headers and `ReShade.fxh`. No NVIDIA SDK is needed to compile this
add-on. `Get-CiDependencies.ps1` downloads the pinned public dependencies with
hash checks; `Build-CiIntegration.ps1` builds, tests and packages them in a clean
checkout. See the workflow for its Visual Studio environment.

For an existing dependency setup, use a Visual Studio developer shell:

```powershell
cmake -S tools/dlss5-observer -B tools/dlss5-observer/build/v05 -A x64
cmake --build tools/dlss5-observer/build/v05 --config Release
ctest --test-dir tools/dlss5-observer/build/v05 -C Release --output-on-failure
```

Override `RESHADE_INCLUDE_DIR`, `GL_HEADERS_DIR`, `RESHADE_SOURCE_DIR` and
`IDENTITY_SHADER_INCLUDE_DIR` as needed. The real add-on is
`build/v05/Release/firestorm_scene_observer.addon64`. Modules under
`test-receiver` or `test-missing-export` are fixtures and must never be installed.

Nine tests cover scene ABI/eligibility/dispatch, detection/order, shader parsing,
native GL image/state preservation, resize policy and helper restart policy.
Native GL tests report skipped when their required context is unavailable.
These tests do not run the neural consumer or establish GPU image quality.

## Development diagnostics

The source retains observation, copy, identity and input-audit modes for debugging.
They are not the normal-use launch path. `Prepare-TestViewer.ps1` and
`Run-LifecycleControl.ps1` stage and run those controls; diagnostic shader
discovery may require temporarily disabling the viewer's shader cache. The
explicit scene signal is used by the normal lean path. `FS_SCENE_EARLY_TRACE=1`
enables full tracing when investigating it.

Read `ReShade.log` entries beginning `[FSObserver]` for scene eligibility,
resource geometry, early Kernel/Feed execution, composition and duplicate
witnesses. Packet identifiers and resource extents do not prove guide-image
alignment. Check the helper consumer log for `feature 18 evaluation succeeded`
at each settled size; composition counts alone do not prove neural output.
Detailed helper logs and recovery are covered in [the helper guide](HELPER-TEST.md).

Do not load diagnostic receiver modules, combine competing launchers or reuse
an interrupted session before restoring its held files. Package fixture tests
can verify installation/preparation/restoration without launching the viewer.
