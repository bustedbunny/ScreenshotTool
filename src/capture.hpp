#pragma once
#include "win.hpp"
#include <stop_token>
namespace shot {
class CaptureService {
public:
    std::shared_ptr<const DesktopImage> capture(std::stop_token stop = {}) const;
};
float querySdrWhite(const std::wstring& deviceName, bool required);
}
