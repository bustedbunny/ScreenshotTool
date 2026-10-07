#pragma once
#include "win.hpp"
#include <chrono>
#include <stop_token>

namespace shot::detail {
// Keep the retry/ownership policy independent of DXGI resources so tests can
// supply frame sequences and a clock without needing a live desktop.
template<class Acquire, class Release, class Consume, class Now>
void withDesktopFrame(std::stop_token stop, Acquire acquire, Release release, Consume consume, Now now) {
    using namespace std::chrono;
    const auto deadline=now()+seconds(4);
    for(;;) {
        if(stop.stop_requested())throw AppError(TextId::CaptureCanceled);
        const auto remaining=deadline-now();
        if(remaining<=decltype(remaining)::zero())
            throw AppError(TextId::CaptureTimeout);
        const auto wait=std::min(milliseconds(100),ceil<milliseconds>(remaining));
        DXGI_OUTDUPL_FRAME_INFO info{};
        const HRESULT hr=acquire(static_cast<UINT>(wait.count()),info);
        if(hr==DXGI_ERROR_WAIT_TIMEOUT)continue;
        check(hr,"Could not capture the desktop. Unlock Windows, close other capture software, or update the graphics driver and retry");
        // Install ownership before checking cancellation or inspecting the frame.
        struct FrameRelease {
            Release& release;
            ~FrameRelease(){release();}
        } guard{release};
        if(stop.stop_requested())throw AppError(TextId::CaptureCanceled);
        if(info.ProtectedContentMaskedOut)
            throw AppError(TextId::ProtectedContent);
        // AcquireNextFrame also succeeds for cursor-only updates. A fresh
        // duplication session must wait for an actual desktop-image update.
        if(info.LastPresentTime.QuadPart==0)continue;
        consume();
        return;
    }
}
}
