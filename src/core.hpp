#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace shot {
struct Point { int x{}, y{}; bool operator==(const Point&) const = default; };
struct Rect {
    int left{}, top{}, right{}, bottom{};
    int width() const { return std::max(0, right - left); }
    int height() const { return std::max(0, bottom - top); }
    bool empty() const { return right <= left || bottom <= top; }
    bool contains(Point p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
    bool operator==(const Rect&) const = default;
};
Rect normalized(Point a, Point b);
Rect intersect(Rect a, Rect b);
Rect united(Rect a, Rect b);
Rect translated(Rect r, Point delta);
Point clampPoint(Point p, Rect bounds);
enum class Handle { None, Move, NW, N, NE, E, SE, S, SW, W };
Handle hitSelection(Rect r, Point p, int radius = 7);
Rect adjustSelection(Rect original, Handle handle, Point delta, Rect desktop);
enum class Rotation { Identity, Clockwise90, Clockwise180, Clockwise270 };
Point sourcePixel(Point oriented, int rawWidth, int rawHeight, Rotation rotation);
struct Pixel { float r{}, g{}, b{}, a{1}; bool operator==(const Pixel&) const = default; };
struct Image {
    int width{}, height{};
    std::vector<Pixel> pixels;
    Image() = default;
    Image(int w, int h);
    Pixel& at(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
    const Pixel& at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};
float srgbToLinear(float v);
float linearToSrgb(float v);
Pixel scaled(Pixel p, float factor);
struct MonitorImage {
    Rect bounds;
    std::wstring deviceName;
    bool hdr{};
    float sdrWhiteNits{80};
    float maxLuminance{80};
    unsigned dpi{96};
    Rotation rotation{};
    std::int64_t adapterLuid{};
    Image image; // Linear scRGB; HDR 1.0 = 80 nits, SDR 1.0 = display white.
};
struct DesktopImage {
    std::vector<MonitorImage> monitors;
    Rect bounds() const;
    bool intersectsHdr(Rect crop) const;
};
Image composite(const DesktopImage& desktop, Rect crop, bool hdrExport);
void pixelate(Image& image, Rect imageDesktopBounds, Rect censorBounds, int blockSize = 12);
void blackCover(Image& image, Rect imageDesktopBounds, Rect censorBounds);

enum class Tool { Select, Pen, Highlighter, Rectangle, Ellipse, Line, Arrow, Text, Censor };
struct Annotation {
    Tool tool{Tool::Pen};
    std::vector<Point> points;
    Pixel color{1, 0.12f, 0.22f, 1}; // UI colors are sRGB, converted by the renderer.
    float width{3};
    float textSize{24};
    std::wstring text;
    bool pixelated{};
};
class History {
public:
    void add(Annotation annotation);
    bool undo();
    bool redo();
    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ < items_.size(); }
    std::span<const Annotation> visible() const { return {items_.data(), cursor_}; }
    void clear() { items_.clear(); cursor_ = 0; }
private:
    std::vector<Annotation> items_;
    size_t cursor_{};
};
enum class State { Idle, Capturing, Editing, Exporting };
class SessionState {
public:
    State get() const { return state_; }
    bool beginCapture();
    void captured();
    bool beginExport();
    void exportFailed();
    void reset() { state_ = State::Idle; }
private:
    State state_{State::Idle};
};
struct PrintScreenResult { bool suppress{}, trigger{}; };
class PrintScreenGate {
public:
    PrintScreenResult key(bool down, bool modified);
private:
    bool held_{}, suppressed_{};
};
std::wstring timestampStem(int year, int month, int day, int hour, int minute, int second, int millisecond);
std::wstring availableStem(const std::wstring& stem, const std::function<bool(const std::wstring&)>& exists);

// File operations are injectable so failure rollback is testable without a disk fault.
struct FileOperations {
    std::function<void(const std::filesystem::path&, const std::filesystem::path&)> moveNoReplace;
    std::function<void(const std::filesystem::path&)> remove;
};
struct StagedFile { std::filesystem::path temporary, final; std::function<void(const std::filesystem::path&)> write; };
void publishFiles(std::span<const StagedFile> files, const FileOperations& operations);
}
