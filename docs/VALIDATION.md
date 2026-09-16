# Validation record

## Environment

- Date: 2026-09-16.
- Windows build detected by CMake: 10.0.26200 (Windows 11).
- Compiler: Visual Studio 2022, MSVC 19.44.35207.1, x64.
- Windows SDK: 10.0.26100.0.
- CMake: Visual Studio bundled 3.31.6.
- Release executable uses the static C++ runtime.
- `dumpbin /DEPENDENTS` confirmed only Windows system DLL imports, with no `VCRUNTIME`, `MSVCP`, or dynamically linked C runtime dependency.
- `build.ps1` passed PowerShell syntax validation. CMake install and CPack ZIP packaging completed successfully.

## Automated checks

Final verification: **27 checks passed, 0 failed** in the Release CTest run. The hidden swap-chain test caught and verified a fix to the D2D surface binding flags before the initial packaging. The black-capture fix adds eight deterministic acquisition checks.

The CTest executable covers:

1. Mixed-DPI geometry and negative desktop coordinates.
2. Rotation mappings for all four orientations, with a pixel-bijection check.
3. Selection hit testing, movement limits, and resize-handle crossing.
4. Cropping, opaque black gaps, HDR intersection, and SDR reference-white scaling.
5. Undo, redo, history branching, and fixed annotation anchors.
6. Bare Print Screen, held-key repeats, paired releases, and modified shortcuts.
7. Session transitions and export retry recovery.
8. Timestamp formatting and collisions across PNG, JPEG XR, and reservation names.
9. Injected failure at every write/commit step of a paired export.
10. Preservation of an existing file when a commit collides.
11. Actual black-cover and pixelation flattening, including HDR values.
12. All 256 sRGB byte values through linear-light conversion and back.
13. PNG pixel round trip and explicit sRGB metadata.
14. Exact FP16 JPEG XR round trip, including negative scRGB and 1000-nit highlights.
15. Direct2D HDR tone mapping, monotonic patches, and SDR-brightness normalization.
16. Mixed SDR/HDR composition, matching annotation geometry/color intent, cropped drawing, and black censors in both encoded outputs.
17. Every drawing tool, DirectWrite text, pixelation, and encoded flattened pixels.
18. Real paired files, collision avoidance, no leftover staging files, and Save As replacement.
19. Hidden-window FP16 swap-chain creation and presentation with selection, annotations, and pixelation readback.
20. Cursor-only startup updates and wait timeouts followed by a desktop frame; only the accepted frame's signed HDR pixels reach the image.
21. Continuous cursor-only updates exhaust a single four-second deadline without reading their pixels.
22. Acquisition wait timeouts do not release frames that were never acquired.
23. The final acquisition wait is capped to the remaining deadline.
24. Cancellation before acquisition, during a wait, and immediately after acquiring either a cursor-only or desktop frame.
25. Acquisition failure after a cursor update releases only the previously acquired frame.
26. Protected-content and readback failures release their acquired frame exactly once.
27. A genuinely black desktop frame is accepted based on its metadata.

Color and presentation tests explicitly use the D3D11 WARP software device, so they exercise the real Windows graphics effects and codecs without depending on a particular GPU's color mode. The file tests use a unique temporary directory and clean it up.

Acquisition tests exercise the production retry/ownership helper with scripted DXGI results and a simulated monotonic clock. They require no live desktop or four-second sleeps. The public capture API and FP16 conversion remain unchanged.

## Hardware probe performed

Before the black-capture fix, the interactive-user `--capture-probe` succeeded on:

| Display | Size | Desktop origin | HDR | SDR reference white | Rotation | Adapter LUID |
| --- | --- | --- | --- | --- | --- | --- |
| `\\.\DISPLAY1` | 5120 × 1440 | 0, 0 | Enabled | 240 nits | Identity | 69461 |

The probe retained float desktop pixels in memory and wrote no screenshots. Desktop duplication correctly failed with access denied under the restricted sandbox account; it succeeded when run as the interactive Windows user. Visual Studio's MSBuild file tracker also required execution outside the sandbox; this is an execution-environment restriction, not an administrator requirement for the application.

After the black-capture fix, the Release build and all 27 tests passed. The rebuilt app was launched for verification. The fresh interactive-user capture probe failed with **"Windows hid protected content in this capture. Close the protected window and capture again."** No capture image was produced by that probe. Visible preview and exported desktop-content verification therefore remain pending; the startup-frame defect is covered by deterministic tests, but resolution of the reported symptom has not yet been confirmed on the affected display. Recheck repeated captures after the protected content is closed.

## Toolbar shortcuts and initial Pen selection

The 2026-09-16 toolbar update built successfully in Release using `build.ps1`; the existing suite reported **27 checks passed, 0 failed**. The portable package was refreshed, and its executable's SHA-256 matched the Release build. These existing checks do not exercise the new keyboard dispatch or initial-tool transition.

Interactive verification was blocked when automatic approval review rejected launching the rebuilt app with **"Computer Use was not approved to use screenshottool"**. The following checks remain pending:

- Every single-letter binding and tooltip with focus on the overlay and toolbar buttons.
- Text entry, native dialogs, modifier combinations, held-key repeats, active drags, and export blocking.
- First completed region selects Pen and the next drag draws; empty or interrupted initial drags leave the transition pending.
- Later crop moves, resizes, and replacements preserve the selected tool; a new capture session selects Pen after its first completed region again.
- Existing export, undo/redo, and Tab/Shift+Tab shortcuts.

## Interactive checks remaining

The app was launched and Paint was opened for interface testing. The user stopped Computer Use with the physical Escape key before end-to-end UI checks could be verified. These items are **not marked passed**:

- Bare Print Screen from another foreground app; modified shortcuts preserved.
- Frozen animated content, with no overlay captured into itself.
- Initial drag, selection move/resize, and toolbar placement near screen edges.
- Each drawing gesture, color/width/text controls, text shortcuts, and undo/redo.
- Clipboard paste into Paint and another application, with no toolbar/cursor/dimming.
- Quick save of SDR and HDR selections; native Save As, overwrite, cancellation, and retry after file/clipboard errors.
- Repeated sessions, tray menu, Settings, and optional sign-in startup.
- Visible HDR preview matching the original display and SDR output appearance.

## Hardware checks remaining

The available setup exposes one HDR monitor. The following need suitable hardware or an interactive user changing display settings:

- Mixed HDR and SDR outputs in the same crop.
- Different DPI factors, negative origins, physical gaps, and portrait displays.
- Displays on different adapters.
- Windows SDR-brightness changes and HDR toggling during a session.
- Display disconnects/reconnects, device loss, lock/unlock, and remote-desktop behavior.

Synthetic tests cover the geometry and color rules for these configurations. They do not substitute for driver- and display-dependent visual checks.
