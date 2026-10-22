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
3. Extract the integration ZIP included in the integration download.
4. Close Firestorm, then run the extracted installer script, supplying the
   downloaded helper folder (example paths):

   ```powershell
   .\Install-FirestormNeural.ps1 -ViewerDirectory 'C:\Viewers\Firestorm-DLSS5Preview' -HelperBuildManifest 'C:\Downloads\integration\helper\manifest.json' -HelperHostPath 'C:\Downloads\integration\helper\dlss5-feed-host64.exe'
   ```

5. Start `Launch-FirestormNeural.cmd` in the viewer folder. Keep its console
   open until you quit normally so original add-ons/settings can be restored.

The folder must be writable by your user, or setup and the launcher require
administrator execution. The normal Firestorm profile is used. Resize currently
pauses about four seconds; neural rendering resumes after roughly eight seconds.
Read the integration ZIP's README for configuration, recovery and removal.

The helper download contains the locally modified MIT-licensed Feeder client
and the unchanged official v1.19.0 host, with dependency license notices.
Their source/build adaptation is in this fork. No ReShade proxy DLL, neural
consumer DLL, NVIDIA runtime/model, signing key or personal configuration is
included in the integration download. Artifacts expire after 30 days; they are
not GitHub Releases. Obtain RHI and the neural components from their own projects.
