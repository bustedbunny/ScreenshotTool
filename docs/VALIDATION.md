# Validation record

## Outside drag selection replacement - 2026-10-07

Release build completed successfully with MSVC x64 using `cmake --build --preset release --parallel`. The fresh `ctest --preset release --verbose` run passed **65 checks, 0 failed** across `ScreenshotToolTests` (49) and `ScreenshotToolUpdateTests` (16), in 15.75 seconds. The rebuilt executable is `build/Release/ScreenshotTool.exe`. `git diff --check` passed.

Regression coverage verifies outside replacement with all nine tools in all four drag directions, desktop clamping, click-only and zero-width/height rollback, capture-loss restoration, toolbar visibility, tool retention, and unchanged annotation coordinates/history. It also covers immediate replacement after the first crop automatically activates Pen, inside-started drawing/text gestures crossing outside, active outside text caret/word selection/movement/resizing and TSF extents, crop-handle priority, and composing text committed exactly once before blank outside replacement.

Gesture tests use real offscreen Win32 windows without desktop capture or injected input. Manual screenshot-session interaction was not exercised in this run.

## GitHub release auto-updates - 2026-09-30

Release builds succeeded with MSVC x64 in `build/update-validation`; the original `build/Release/ScreenshotTool.exe` was locked by a running instance and was left running. The fresh CTest run passed **64 checks, 0 failed** across `ScreenshotToolTests` (48) and `ScreenshotToolUpdateTests` (16), in 5.47 seconds. The generated executable version is **1.1.0**. A read-only live probe through the new WinHTTP client successfully queried GitHub and reported the app current against the existing `1.0` release. Import inspection confirmed only Windows system DLL dependencies.

Updater checks cover numeric version normalization/order, stable release/asset selection, malformed metadata and missing digests, HTTP failures/rate-limit backoff, cancellation, preference round trips, shell-free Windows argument quoting, downloaded size/hash/PE/version verification, protected folders, staging cleanup, locked targets, backup preservation, replacement and rollback. A real disposable-process test prepares an update while its target is running, waits for that parent to exit, replaces it, acknowledges startup from the new executable and removes the helper. Its paths contain spaces and Japanese characters. No test updates the user's running app or sign-in registry entry.

Portable ZIP creation succeeded with CPack. The workflow's three multiline PowerShell blocks passed syntax parsing; its version-validation block accepted `v1.1.0` and rejected `v1.2.0`. Workflow publication is intentionally unexecuted: no tag, draft or published release was created. GitHub-hosted execution remains to be verified on the first tagged release.

Reproduction commands, with the Visual Studio bundled CMake tools if they are absent from PATH:

```powershell
cmake --preset windows-x64 -B build/update-validation
cmake --build build/update-validation --config Release --parallel
ctest --test-dir build/update-validation -C Release --output-on-failure
./build/update-validation/Release/ScreenshotToolUpdateTests.exe --github-probe
cmake --build build/update-validation --config Release --target PACKAGE
```

### Interactive checks pending

Native desktop controls are unavailable in the current Computer Use surface, so native tray/settings visuals and actual screenshot-session interaction were not exercised.

- Confirm the Settings dialog shows version 1.1.0, preserves sign-in behavior and drawing settings, and persists the automatic-check toggle.
- Check manually against a newer test release; click the tray notification, view its release notes, cancel, then confirm download and restart.
- Check during capture/editing/export and download during a screenshot session; confirm prompts and replacement wait until idle and capture remains responsive.
- Verify an incompatible release offers manual installation, and an unwritable portable location reports failure while the current app remains usable.
- On the first real tagged release, confirm Actions leaves failed uploads in draft and publishes both verified assets only after successful build and tests.

## Crop editing with every tool and desktop-wide drawing - 2026-09-30

Release build completed successfully with MSVC x64 using `cmake --build --preset release --parallel`. The fresh `ctest --preset release --output-on-failure` run passed: **48 checks passed, 0 failed** in `ScreenshotToolTests` (one CTest executable, 0.90 seconds). The rebuilt executable is `build/Release/ScreenshotTool.exe`.

Five new regression checks cover:

1. All eight crop resize handles with all nine tools, matching resize cursors, no annotation/text creation on crop handles, repeated-pointer no-op behavior, tool retention, no region movement from the interior, and the export-busy cursor.
2. Select replacement from outside in all four drag directions, unchanged crops after clicks/zero-height replacements, original-region restoration on capture loss during replacement or resize, and the initial region-to-Pen transition.
3. Pen, Highlighter, Rectangle, Ellipse, Line, Arrow and Censor starting outside and crossing the crop, toolbar visibility during drawing, repeated-pointer no-op behavior, full desktop annotation coordinates, preservation through shrink/replacement, and annotation undo/redo.
4. Actual TSF-backed text creation outside the crop, desktop-based default width and initial dragging, outside caret/word selection and double-click routing, desktop-clipped screen/IME extents, text movement with layout reuse, crop/text handle priority, composing-text commit exactly once before crop resize, and the default width at the desktop edge.
5. WARP FP16 preview pixels for every drawing tool, text, black cover and pixelation. Outside-region drawing receives the same 25% shade as its desktop background; the inside-region preview matches the cropped export away from editing decorations. Exports retain crop dimensions and exclude wholly outside censors. Text handles appear outside the crop, and overlapping crop handles draw above text decorations.

The preview pixel test calls the same private drawing path used before swap-chain presentation and reads its FP16 target before `Present`, which rotates the swap-chain buffers. Existing real swap-chain presentation, layout reuse, composition, targeted invalidation, toolbar mutation/clipping, preview/export, acquisition, codecs and file-transaction tests remain passing. Native gesture fixtures use offscreen Win32 windows and keep the toolbar offscreen; they do not inject desktop input.

### Interactive checks pending

Computer Use failed before app interaction. Initial import reported `node_repl kernel exited unexpectedly` with **`windows sandbox failed: helper_unknown_error: setup refresh had errors`**. A reset and initialization retry reported **`trusted Node process exited unexpectedly; kernel reset, rerun your request`**. No visual or affected-display checks are marked passed.

- Resize all eight crop handles while each tool is active; confirm resize cursors and crop priority over an active text box.
- Confirm Select cannot move the crop and can replace it from outside; verify interrupted/empty replacements restore it.
- Draw, create/edit/move text, use censor/pixelation, and preserve annotations while shrinking or replacing the crop.
- Verify shaded outside-region previews, crop-only copy/save output, and a stable visible toolbar during drawing.
- Verify live IME candidate positioning outside the crop and physical negative-origin, cross-monitor, mixed-DPI and SDR/HDR behavior.

## Text drag lag and toolbar flicker - 2026-09-17

Release build succeeded with MSVC x64. Final CTest run: **43 checks passed, 0 failed** in `ScreenshotToolTests` (one CTest executable, 0.40 seconds). `git diff --check` passed. Existing undo, composition, crop, preview/export pixel, acquisition, codec and file-transaction checks remain passing.

The normal build could not replace `build/Release/ScreenshotTool.exe` because that executable was already running. It was left running; a separate Release build completed at **`build/drag-validation/Release/ScreenshotTool.exe`**. The running instance still contains the older code. Reproduction commands (using the Visual Studio bundled CMake/CTest tools):

```powershell
cmake --preset windows-x64 -B build/drag-validation
cmake --build build/drag-validation --config Release --parallel
ctest --test-dir build/drag-validation -C Release --output-on-failure
./build/drag-validation/Release/ScreenshotToolTests.exe --text-drag-benchmark
```

Six added checks cover:

1. Layout identity retained across translation, height-only resize, color, selection, geometry undo/redo and composition decorations; width, font size and content still rebuild/reflow. Repeated normalized bounds, including heights clamped to content, emit no change.
2. Real rendered glyph pixels overflowing a narrow box fit within the cached visual extent.
3. Geometry reports TSF layout changes and translated caret extents, including deferred notification under a lock, without false text/selection notifications. Actual selection still notifies.
4. Toolbar message counts and update regions: repeated refresh/canvas repaint causes no mutation; changed labels, enabled state, tool selection and color swatches affect only the relevant controls. Status invalidation stays within its own rectangle.
5. Production toolbar has `WS_CLIPCHILDREN`. Offscreen GDI painting fills only the dirty area and preserves excluded child-button pixels. The test emulates BeginPaint's child exclusion on a memory DC; physical on-screen flicker remains an interactive check.
6. Production drag handler reuses text storage/layout, preserves settings and the caret timer interval/visibility, keeps the toolbar visible, and invalidates only monitors intersecting old/new visual extents. Monitor jumps skip intervening displays; handle and glyph overhang include adjacent displays; crop clipping excludes invisible regions. Repeated pointers and identical mouse-up cause no extra text or toolbar update.

The existing FP16 presentation check also exercises active selection, composition, caret, border and handles at two white scales using the cached Direct2D brushes and stroke styles. Toolbar/invalidation fixtures are real offscreen Win32 windows; they do not capture or inject input into the desktop.

### Drag-handler measurements

Release benchmark on this machine, 20,000 moving pointer samples followed by 20,000 identical samples per box. Timing includes `OverlaySession::mouseMove`, geometry handling, a TSF test sink, and Win32 monitor invalidation across three synthetic monitor bounds. Initial layout creation, undo snapshot setup, message dispatch, GPU painting and display presentation are excluded. These are handler timings, not end-to-end pointer/frame latency or a before/after comparison.

| Box | Moving total (ms) | Median (us) | p95 (us) | p99 (us) | Identical-position total (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Empty | 67.5093 | 1.5 | 8.8 | 54.0 | 1.0460 |
| Multiline, 58 UTF-16 units | 51.6608 | 1.6 | 8.3 | 23.1 | 1.0504 |
| 16,384 UTF-16 units | 45.8910 | 1.5 | 5.2 | 17.5 | 0.9997 |

Each case recorded **zero layout rebuilds, zero toolbar mutations, zero text notifications and zero selection notifications** during dragging. The moving pass emitted 19,999 geometry/layout notifications (its first sample matched the original bounds); the identical-position pass emitted none. Identical-position p95 and p99 were 0.1 us in all cases; sub-resolution median readings rounded to zero.

### Interactive and hardware checks pending

Computer Use could not initialize. A reset and initialization retry failed with `node_repl kernel exited unexpectedly`; its diagnostic was **`windows sandbox failed: helper_unknown_error: setup refresh had errors`**. No interaction with the rebuilt app or affected display was performed. Pending:

- Click/drag creation, border movement, all eight resize handles, and color/font-size dialogs on the affected display.
- No toolbar flashing, disappearing labels or accumulated pointer lag with empty, multiline and 16,384-character boxes, including sustained movement and height/width resizing.
- Actual IME candidate positioning during movement/reflow and live composition/selection/focus behavior.
- Physical cross-monitor, negative-origin, mixed-DPI and SDR/HDR behavior, plus visible preview/export comparisons.

## Paint-style inline text — 2026-09-17

Release build completed successfully with MSVC x64. The final `ctest --preset release` run passed: **37 checks passed, 0 failed** in `ScreenshotToolTests` (one CTest executable). `git diff --check` passed. The rebuilt executable is `build/Release/ScreenshotTool.exe`.

The inline editor replaces the floating window and native EDIT control. Ten new regression checks cover:

1. Default/clipped box widths, directional selection replacement, deletion, local undo/redo branching, and empty commit.
2. UTF-16 surrogate pairs, combining clusters, atomic WM_CHAR surrogate input, and the 16,384-code-unit limit.
3. Word selection, Ctrl/Shift navigation, vertical movement, and wrapped-line trailing caret affinity.
4. Whole-box size/color changes, preserved selection/caret, format undo, and no-op formatting.
5. Eight handle hit targets, border movement, negative coordinates, width reflow, height growth, content preservation, and one undo step per resize gesture.
6. Composition cancellation/undo, commit/discard, complete-box annotation history, and fresh state for repeated boxes.
7. Exact floating-point document-renderer pixels before/after commit and between preview/export, crop clipping, split-monitor rendering, HDR white scaling, and legacy unwrapped text. Editing decorations are excluded from these document-renderer comparisons.
8. ITextStoreACP read/write locks, asynchronous upgrades, reentrant synchronous rejection, reverse selection, replacement ranges, sink notifications, and actual caret screen extents at negative coordinates.
9. A TSF composition that starts after its initial service insertion; cancel restores the pre-insertion state without adding a duplicate undo step.
10. Real TSF document/context activation on a hidden HWND, composition cleanup, and repeated teardown.

The existing capture, geometry, export, WARP graphics, FP16 presentation, codec, and file-transaction checks also pass. TSF protocol tests use a test sink; activation tests use Windows TSF itself. They do not establish compatibility with a live language IME.

### Interactive validation pending

The requested Windows 11 Paint comparison could not start. Computer Use's Node runtime exited during initialization on both the initial attempt and the retry after reset, with **"windows sandbox failed: helper_unknown_error: setup refresh had errors"**. No Paint or ScreenshotTool interaction was performed for this change. The following remain pending:

- Side-by-side Paint comparison: click/drag creation, caret placement, drag selection, double-click words, all handles, border movement, and typing at wrapped line ends.
- Actual keyboard and clipboard routing, including cut/copy/paste, AltGr, navigation, local undo/redo, and repeated annotations.
- Opening, choosing, and canceling Color/Text size UI while preserving caret and forward/reverse selections.
- Live Japanese/Chinese/Korean IME input, composition underlining, candidate-window placement at the rendered caret, and first-Escape composition cancellation.
- Canvas click-away, Ctrl+Enter, tool changes, and exports committing exactly once; Escape discarding; no reopening committed boxes.
- Visible overlay/export comparisons with editing decorations excluded, and session cancellation while composing or dragging.
- Physical mixed-DPI and SDR/HDR monitors, negative origins, cross-monitor movement/resize, crop edges, and per-monitor candidate placement. Synthetic geometry/pixel tests do not replace these hardware checks.

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

Baseline verification (before inline text): **27 checks passed, 0 failed** in the Release CTest run. The hidden swap-chain test caught and verified a fix to the D2D surface binding flags before the initial packaging. The black-capture fix adds eight deterministic acquisition checks.

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

## Live text preview

The 2026-09-16 text preview fix built successfully in Release. The fresh CTest run reported **27 checks passed, 0 failed**. These existing checks cover annotation rendering and export, but do not exercise the floating editor or its input notifications.

The native multiline editor now uses an owned tool window above the graphics overlay. Every EN_CHANGE updates the draft annotation and invalidates all monitor overlays; commit adds one history entry, while discard removes the draft. Editor placement prefers the right or left of the selection and otherwise uses the work-area corner farthest from the text anchor.

Interactive verification could not start: the Computer Use Node runtime exited during initialization, and its retry reported **"windows sandbox failed: helper_unknown_error: setup refresh had errors"**. The following checks remain pending:

- Live text appears before focus changes, using the selected color and size, and tracks typing, paste, deletion, and newlines.
- The floating editor, caret, and text selection remain visible across overlay repaints; native clipboard, undo, and IME behavior is retained.
- Ctrl+Enter and click-away commit exactly once; Escape discards; empty text creates no history entry; annotation undo/redo and copy/save use only committed text.
- Work-area placement, crop clipping, negative monitor origins, mixed DPI, and SDR/HDR visual consistency.

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
