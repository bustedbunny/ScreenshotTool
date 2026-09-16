# Architecture

## Components

| Files | Responsibility |
| --- | --- |
| `core.hpp`, `core.cpp` | Physical-pixel geometry, rotation mapping, immutable desktop data, annotation history, keyboard gate, state transitions, naming, export transaction orchestration |
| `capture.hpp`, `capture.cpp` | DXGI adapter/output enumeration, `DuplicateOutput1`, CPU staging, scRGB conversion, display bounds, rotation, DPI, HDR mode, SDR white metadata |
| `graphics.hpp`, `graphics.cpp` | Direct3D/Direct2D device resources, FP16 flip-model scRGB swap chains, one annotation renderer for preview and export, tone mapping, pixel readback |
| `overlay.hpp`, `overlay.cpp` | Monitor windows, selection dragging/resizing, annotation gestures, native text editor, dark toolbar, shortcuts and display-change validation |
| `export.hpp`, `export.cpp` | PNG/JPEG XR encoding, clipboard ownership, native Save As, collision-safe quick saves, staged writes |
| `settings.hpp`, `settings.cpp` | Per-user settings and optional sign-in registry entry |
| `app.hpp`, `app.cpp`, `main.cpp` | Single-instance mutex, tray, low-level keyboard hook, message loop, worker lifetimes and session state |

## Sessions and threads

The controller transitions `Idle → Capturing → Editing → Exporting → Idle`. Export failures return to `Editing`; canceling Save As retains the document. Capture and encoding/disk work run in a `std::jthread`, with completion posted to the controller window. Capture cancellation is checked between bounded duplication waits. Generation identifiers discard stale worker completions after cancellation/display changes. A dedicated keyboard thread runs its own message pump; callbacks only update the Print Screen gate and post a capture request, so UI rendering and modal dialogs cannot delay the hook.

The capture worker builds every monitor image before publishing a `shared_ptr<const DesktopImage>`. Overlays never recapture the desktop. Each output uses a device on its own adapter, and immutable CPU pixels bridge adapter boundaries. Every overlay owns its own presentation resources. UI and graphics objects are created and used on their owning thread; the export worker creates separate graphics/COM resources. Clipboard ownership is acquired on the UI thread using the persistent controller HWND.

Initial desktop acquisition skips successful cursor-only updates (`LastPresentTime == 0`) and releases each before retrying. It waits up to four seconds per output in slices of at most 100 ms, capped by the remaining deadline. Every successful acquisition installs a release guard before cancellation checks, metadata validation, or pixel readback. Timeout and cancellation fail capture before any overlay is shown. Frame readiness depends on update metadata, so an actual all-black desktop remains a valid image. The internal `capture_frame.hpp` helper accepts acquisition, release, readback, and clock callbacks for deterministic ownership and retry tests.

## Coordinates and annotation document

The process manifest requests Per Monitor V2 DPI awareness. Desktop bounds, cursor positions, crops, and annotations use physical pixels, with signed coordinates. D2D uses 96-DPI pixel units. Only toolbar controls scale with monitor DPI. Rotation is resolved when reading a capture into desktop orientation. The compositor initializes uncovered desktop gaps to opaque black.

The document is an ordered sequence of annotation values with a history cursor. Undo/redo moves the cursor; new annotations discard the redo tail. Moving/resizing the selection changes only the clip rectangle. It never transforms the annotation document. The same Direct2D paths, shapes, arrow geometry, grayscale-antialiased DirectWrite text, and censor logic render to previews and both export destinations. Opaque censors use aliased integer bounds. Pixelation reads the already-composited pixels and replaces them with block averages, so underlying image and earlier drawing are flattened.

Pixelation grids are anchored to their annotation rectangle. Edge blocks average available target pixels, so a block cut by a crop or a monitor boundary can have a slightly different average in the preview and export. Opaque black covers have no such dependency.

## Color

1. `DuplicateOutput1` requests FP16 and the required BGRA fallback. BGRA is accepted for SDR only; an HDR output returning BGRA fails with an actionable message.
2. FP16 data enters CPU float scRGB without clipping. SDR BGRA is converted from sRGB to linear light. Each monitor stores its HDR flag and SDR reference white from DisplayConfig.
3. Overlays use `R16G16B16A16_FLOAT`, flip sequential presentation, and `RGB_FULL_G10_NONE_P709`. Captured values are drawn unchanged. A 25%-opaque black shade covers the area outside selection. Preview annotations use the monitor's SDR white on HDR displays.
4. SDR export processes only HDR monitor regions. White-level adjustment first normalizes the captured SDR-white setting to 80 nits. Direct2D's HDR tone mapper then uses the normalized content peak and an 80-nit output white. This ordering keeps ordinary SDR content invariant under Windows' SDR-brightness adjustment; the automated patch test covers that behavior. SDR regions bypass tone mapping.
5. Annotations are composited in linear light. The final SDR image uses the IEC sRGB transfer function, clamps to the sRGB output gamut, and receives a PNG sRGB rendering-intent chunk. This explicit conversion prevents treating linear pixels as gamma-encoded pixels.
6. HDR export copies HDR source values and scales SDR regions by `203 / 80`. Annotation diffuse white also uses `203 / 80`. Encoding quantizes composited pixels once to FP16 and writes `GUID_WICPixelFormat64bppRGBAHalf` with `Lossless = VARIANT_TRUE`. Original FP16 capture values remain exact. The Windows JPEG XR full-float codec path was measured to change some FP32 values even in lossless mode, so it is deliberately not used.

Reference documentation: [Advanced Color capture and presentation](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range), [Direct2D HDR tone mapper](https://learn.microsoft.com/en-us/windows/win32/direct2d/hdr-tone-map-effect), [white-level adjustment](https://learn.microsoft.com/en-us/windows/win32/direct2d/white-level-adjustment-effect), and [Windows JPEG XR codec](https://learn.microsoft.com/en-us/windows/win32/wic/jpeg-xr-codec).

## Export transactions

Quick save reserves a shared filename stem with an exclusive `CREATE_NEW` lock file marked delete-on-close. Both destination names and reservations participate in collision detection. Each encoded image is written to a unique same-directory staging file, flushed, and closed before any final name is published. Final renames do not replace existing files. A failed second rename removes the first final file and all temporary files. It never removes a final file it did not publish.

This is a recoverable pair transaction, not a filesystem-wide atomic commit: observers can briefly see the first file before the second rename. Forced process termination or a power outage during finalization can leave a partial pair or staging file; ordinary reported errors roll back. Cleanup can also be prevented by external permission changes or file locks.

Save As writes a staging file and uses `ReplaceFileW` for a confirmed overwrite. Clipboard data is completely encoded and allocated before opening/emptying the clipboard. CF_DIBV5, CF_DIB, and PNG carry flattened opaque pixels. Clipboard ownership is checked before closing the session.

## Resource and platform limits

Capture and compositing use CPU float images in addition to GPU resources. Large desktops may require several hundred MB; extremely large crops are rejected, and device bitmap-size/allocation failures report an error. Pixelation performs GPU readback, so many large pixelation annotations can be more expensive than other tools. DRM-protected desktop content is rejected when DXGI reports masking. Hardware cursor overlays are not composited into the image.
