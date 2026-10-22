# Windows DLSS5 preview downloads

These are unsigned experimental builds from this fork, not official Firestorm
releases. The viewer uses OpenAL audio instead of FMOD, and OpenJPEG instead of
KDU. Its feature coverage can differ from an official release. Cloud build/test
success does not verify actual neural rendering or image quality on your GPU.

Download both `firestorm-dlss5-viewer-windows-x64` and
`firestorm-dlss5-integration-windows-x64` from the same successful Actions run.
Extract the downloads and check their SHA256SUMS files.

1. Install the viewer into a separate folder using the viewer Setup executable.
2. Establish a working RHI/OpenGL setup in that viewer folder. It must have
   add-on-enabled ReShade, Feeder, the RenoDX consumer, NVIDIA DLSS/DLSSNR
   runtimes, and Lumenite Kernel. These neural/runtime files are not bundled.
   Use Generic Depth enabled, Match resolution exactly, and motion provider 3.
   Enable **only Lumenite Kernel → DLSS5 Feed**. Any additional technique,
   including RTAO, makes the early pass skip processing. The normal chain then
   runs after UI/HUD drawing, so DLSS5 and other effects can affect overlays.
   Additional effects alongside UI-protected processing are not supported yet.
3. Extract the integration ZIP included in the integration download.
4. Close Firestorm, then run the extracted installer script, supplying the
   downloaded helper folder (example paths):

   ```powershell
   .\Install-FirestormNeural.ps1 -ViewerDirectory 'C:\Viewers\Firestorm-DLSS5Preview' -HelperBuildManifest 'C:\Downloads\integration\helper\manifest.json' -HelperHostPath 'C:\Downloads\integration\helper\dlss5-feed-host64.exe'
   ```

5. Start `Launch-FirestormNeural.cmd`, created by the integration installer in
   the viewer folder. The viewer Setup executable alone does not create it.
   Keep its console open until you quit normally so original add-ons/settings
   can be restored.

The folder must be writable by your user, or setup and the launcher require
administrator execution. The normal Firestorm profile is used. Resize currently
pauses about four seconds; neural rendering resumes after roughly eight seconds.
Read the integration ZIP's README for configuration, recovery and removal.

## Different DLSS5 controls in ReShade

The normal RHI setup displays the full **DLSS 5 Neural Rendering** panel because
the consumer runs inside Firestorm. Here it runs in a hidden helper with its own
overlay and neural hotkeys disabled, so Firestorm shows the helper client's
controls instead. The different UI does not imply an older version. Under
Add-ons → DLSS 5 Feed, **DLSS 5 neural-rendering settings (on the host)** mirrors
consumer settings. Change a value, then click **Apply to the DLSS 5 host**:
this writes the settings and restarts the helper, briefly interrupting neural
output. Slider edits alone are not applied. **Reload from host** rereads its INI.

The launcher copies neural settings from the preview viewer's `ReShade.ini`
(`[RenoDX.DLSS5]`) to the helper at startup. Intensity is preserved, while neural
uplift on, style 0, Upscaled hook and neural upscaling off are selected. Change
supported baseline values with both processes closed, then relaunch. Host-panel
changes apply after the helper restart but are not saved back to the viewer's
baseline: its generated `host64/ReShade.ini` is overwritten at the next launcher
session. Keep style 0, Upscaled hook and neural upscaling off for the tested
route. A separate normal Firestorm
installation's files/settings are not automatically synchronized with this one.

The helper download contains the locally modified MIT-licensed Feeder client
and the unchanged official v1.19.0 host, with dependency license notices.
Their source/build adaptation is in this fork. No ReShade proxy DLL, neural
consumer DLL, NVIDIA runtime/model, signing key or personal configuration is
included in the integration download. Artifacts expire after 30 days; they are
not GitHub Releases. Obtain RHI and the neural components from their own projects.
