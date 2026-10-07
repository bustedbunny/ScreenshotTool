# ScreenshotTool

A portable Windows 11 x64 screenshot app with a system-tray icon, frozen desktop selection, annotation tools, and SDR/HDR exports. Built with C++20, Win32, Direct3D 11, Direct2D, DirectWrite, and Windows Imaging Component. No installer, administrator rights, third-party libraries, or Visual C++ runtime installation is required.

## Run

Run `ScreenshotTool.exe` from the portable package (`dist` after building). It stays in the system tray. **Print Screen** starts a capture; launching a second instance also starts a capture in the existing instance.

1. Press **Print Screen**, then drag to select a region. Captures are frozen before any overlay appears. **Pen** is selected automatically after the first completed region in each capture session, so you can draw immediately.
2. Drag any of the crop's eight resize handles with **any tool**. A drag starting outside the crop replaces it with any tool active; **Select (V)** does nothing when dragged from inside. Crop handles and active text boxes keep priority over replacement. Empty or interrupted crop gestures restore the previous region. Crop resizes and replacements keep drawings anchored to the desktop and retain your selected tool.
3. Choose **Pen**, **Highlight**, **Rectangle**, **Ellipse**, **Line**, **Arrow**, **Text**, or **Censor**, and start drawing inside the crop. A gesture started inside can continue anywhere on the captured desktop. Drawings outside the crop remain visible in the dimmed area and can be included by resizing or redrawing the crop. The toolbar stays visible during drawing and provides color, stroke width, text size, and censor mode controls.
4. Copy or save. Only selected pixels and annotations are exported.

The default censor is an opaque black rectangle. The optional pixelation mode averages blocks of pixels; use black when complete concealment is required.

### Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| Bare Print Screen | Capture all displays; held-key repeats are suppressed |
| V / F / H | Select / Pen / Highlight |
| R / E / L / A | Rectangle / Ellipse / Line / Arrow |
| T / B | Text / Censor |
| C / W / S / P | Color picker / stroke width menu / text size menu / censor mode toggle |
| Esc | Cancel the session (or discard active text first) |
| Ctrl+C | Copy an SDR image, then close after success |
| Ctrl+S | Quick save, then close after success |
| Ctrl+Shift+S | Native Save As dialog for SDR PNG |
| Ctrl+Z / Ctrl+Y | Undo / redo annotations |
| Ctrl+Enter while entering text | Commit the text |
| Tab / Shift+Tab | Navigate toolbar controls |

The fixed single-letter shortcuts work after a region is completed, with no Ctrl, Shift, Alt, or Windows key held, and pause during dragging or export. Held-key repeats are ignored. Hover a toolbar control to see its shortcut.

Text is edited directly on the screenshot, including outside the crop, with a caret, selection, dashed border, and eight handles. Click inside the crop with **Text** for a 300-pixel-wide box (limited by the remaining captured desktop width), or drag its initial bounds from inside the crop to any desktop point. Click or drag inside the active box to position the caret or select text; double-click selects a word. Drag the border to move the box and handles to resize it, including outside the crop. Crop handles take priority when they overlap the text box; using one commits the text before resizing the crop. Width changes wrap text without scaling the font, and the box grows vertically to retain all content.

**Color** and **Text size** apply to the whole active box and preserve its selection. Enter inserts a newline. Ctrl+Enter, a canvas click outside the box, switching tools, or exporting commits one annotation. Committed boxes cannot be reopened. Escape discards active text; during IME composition, the first Escape cancels composition. Empty boxes create no annotation. Clicking elsewhere inside the crop with Text starts the next box; dragging blank canvas outside the crop replaces the selection.

While editing, Ctrl+A selects all, Ctrl+C/X/V copy/cut/paste text, and Ctrl+Z/Y (or Ctrl+Shift+Z) undo/redo local edits. Arrow, Home/End, Ctrl, and Shift navigation operate on text. After commit, annotation undo/redo removes or restores the entire box. Single-letter tool shortcuts pause while typing or using a native dialog. Text uses Segoe UI, transparent background, left alignment, pixel-based sizes, and a 16,384 UTF-16-code-unit limit. Windows Text Services Framework supplies IME composition and candidate placement.

Modified Print Screen shortcuts are passed through. If Windows also opens Snipping Tool, turn off **Settings → Accessibility → Keyboard → Use the Print Screen key to open screen capture**. The app does not change this Windows setting.

### Export behavior

| Action | Output |
| --- | --- |
| Copy | sRGB image in CF_DIBV5, CF_DIB, and PNG clipboard formats; Windows provides CF_BITMAP compatibility |
| Quick save, SDR crop | One `_SDR.png` |
| Quick save, crop intersecting an HDR display | `_SDR.png` plus a lossless FP16 scRGB `_HDR.jxr` with the same stem |
| Save As | sRGB PNG at the selected path, with native overwrite confirmation |

Quick saves use the Windows **Pictures** known folder, under **ScreenshotTool**, including redirected Pictures folders. Example: `Screenshot_2026-09-16_14-32-08-123_SDR.png`. Collisions receive a numeric suffix. Both files in an HDR pair are staged before names are finalized; a failed transaction rolls back its files. File or clipboard errors leave the session open for retry. Canceling Save As returns to editing.

JPEG XR retains capture precision and HDR values above SDR white. SDR portions of an HDR image and HDR annotations use a 203-nit reference white. A viewer must support HDR JPEG XR to display those values correctly. PNG and clipboard images are tone-mapped and explicitly encoded as sRGB.

### Tray and settings

Right-click the tray icon for **Capture**, **Open screenshots folder**, **Settings**, **Check for updates**, **View GitHub releases**, or **Exit**. **Install update…** appears when a compatible release is available. Launch at sign-in is off by default. Enabling it writes only the current user's Run registry entry. If you move the executable, disable and re-enable that option to update its path.

Settings includes **Language**: **Automatic (system language)**, **English**, **Русский**, **简体中文**, **日本語**, **Deutsch**, and **Español**. Automatic follows your Windows display language at startup; unsupported languages use English, and Chinese variants use Simplified Chinese. **Apply** saves all pending settings and switches language immediately while Settings stays open. **OK** saves and closes Settings. **Cancel**, Escape, or the close button discard unapplied edits; settings already saved with **Apply** remain saved. Windows-owned Save As and color-picker dialogs use Windows' display language.

Color, stroke width, text size, language, and the automatic update preference persist in `%LOCALAPPDATA%\ScreenshotTool\settings.ini`. The censor always starts in black-cover mode for each session. Screenshot pixels remain in memory until export. Network requests are limited to GitHub release checks and user-confirmed update downloads; there is no telemetry or screenshot upload.

### Updates

Automatic checks are enabled by default and run once on each primary app startup. Disable **Automatically check for updates at startup** in Settings if desired; **Check for updates** remains available. There are no periodic checks. Existing versions without this feature need one manual upgrade first.

Checks use the latest published stable release of [bustedbunny/ScreenshotTool](https://github.com/bustedbunny/ScreenshotTool/releases/latest). Drafts, prereleases, and equal or older numeric versions are ignored. Automatic checks stay quiet when current or offline; manual checks report the result. GitHub rate limits are respected for the running session.

Click the available-update tray notification or **Install update…**, then choose **Download and restart**. The app verifies the release asset's size, SHA-256 digest, Windows x64 format, application identity, and embedded version. Capture remains available during download; installation waits for an active screenshot session to finish. Update dialogs also wait until the app is idle.

The update replaces only the executable at its current path, preserving settings, screenshots, and the sign-in entry. A temporary native helper waits for the app to exit, keeps a backup during replacement, and confirms the new app starts. Reported replacement or startup failures recover the old executable where possible. Administrator elevation is never requested. If the portable folder is protected or another program locks the executable, use **View GitHub releases** to download manually or move the app to a writable folder. ZIP documentation is refreshed by downloading the portable ZIP, rather than by executable updates.

Command-line entry point: `ScreenshotTool.exe --capture`.

## Build and test

Requirements:

- Windows 11 x64.
- Visual Studio 2022 with **Desktop development with C++**, a Windows 11 SDK, and **C++ CMake tools for Windows**.
- CMake 3.24 or newer (the Visual Studio bundled version works).

From PowerShell in the source directory:

```powershell
.\build.ps1
```

The script discovers CMake from Visual Studio when it is absent from PATH, builds Release, runs tests, and creates `dist\ScreenshotTool.exe` with documentation. It does not require an administrator shell.

Equivalent commands when CMake is on PATH:

```powershell
cmake --preset windows-x64
cmake --build --preset release --parallel
ctest --preset release
cmake --install build --config Release --prefix dist
```

The build uses `/MT` in Release and `/MTd` in Debug. Only Windows system DLLs are dynamically imported. To create a ZIP package, run `cpack --config build/CPackConfig.cmake -C Release`.

### Publishing releases

Set the version in `CMakeLists.txt`, commit it on the default branch, and push a matching `vX.Y.Z` tag (for example, `v1.1.0`). CMake generates the application version, executable resources, manifest, and ZIP name from that value. The **Release** GitHub Actions workflow builds and tests on Windows 2022, uploads `ScreenshotTool.exe` and `ScreenshotTool-X.Y.Z-windows-x64.zip` to a draft release, verifies both assets, then publishes. An upload failure leaves the draft unpublished; rerunning can finish a draft but refuses to modify an already published release.

The updater requires the uploaded asset to be named exactly `ScreenshotTool.exe`, with GitHub's `sha256:` asset digest. Numeric two-part tags such as the original `1.0` release remain readable, but workflow-created releases use three-part `vX.Y.Z` tags. No access token is stored in the application; the workflow uses its repository-scoped `GITHUB_TOKEN`.

Optional hardware probe, run from the interactive user's desktop:

```powershell
.\build\Release\ScreenshotToolTests.exe --capture-probe
```

The probe reports display metadata and discards all captured pixels without writing an image. Restricted sandbox accounts, locked desktops, remote sessions, or graphics drivers without desktop duplication support may deny capture. The app reports capture failures instead of falling back to an HDR-clipping SDR capture path.

## Validation and implementation

See [VALIDATION.md](docs/VALIDATION.md) for executed tests and the remaining interactive/hardware checklist. See [ARCHITECTURE.md](docs/ARCHITECTURE.md) for component responsibilities, color handling, and transaction semantics.

Display-layout, HDR-mode, or SDR-brightness changes abort the active session with a message to capture again. Device loss releases the overlays and reports the failing graphics operation. Desktop duplication acquires each output in sequence; it freezes every captured frame before showing overlays, but does not claim hardware-synchronized exposure across monitors.
