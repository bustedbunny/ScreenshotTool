#pragma once
#include "graphics.hpp"
#include "settings.hpp"
#include "text_store.hpp"

namespace shot {
enum class SessionAction { Copy,QuickSave,SaveAs,Cancel };
class OverlaySession {
public:
    OverlaySession(HINSTANCE instance,std::shared_ptr<const DesktopImage> desktop,Settings& settings,std::function<void(SessionAction)> action,std::function<void(std::wstring)> failure);
    ~OverlaySession();
    void show();
    bool translate(MSG& message);
    void busy(bool value);
    HWND owner() const;
    void validateDisplays();
    Rect selection() const {return selection_;}
    std::span<const Annotation> annotations() const {return history_.visible();}
    void commitText(bool discard=false);
private:
    friend struct OverlayTestAccess;
    struct MonitorWindow {
        OverlaySession* session{};size_t index{};HWND hwnd{};
        std::unique_ptr<Graphics> graphics;ComPtr<ID2D1Bitmap1> background;
    };
    struct ToolbarButton {int id{};HWND hwnd{};std::wstring label,tip;bool enabled{true},selected{};std::optional<Pixel> swatch;};
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK toolbarProc(HWND,UINT,WPARAM,LPARAM);

    LRESULT monitorMessage(MonitorWindow&,UINT,WPARAM,LPARAM);
    LRESULT toolbarMessage(UINT,WPARAM,LPARAM);

    void mouseDown(HWND window,Point point);
    void mouseMove(Point point);
    void mouseUp(Point point);
    void repaint();
    void repaintText(Rect previous,Rect current);
    void placeToolbar();
    void createToolbar();
    void layoutToolbar(unsigned dpi);
    void command(int id);
    void refreshButtons();
    void refreshStatus();
    RECT statusRect() const;
    void paintToolbar(HDC dc,const RECT& dirty);
    void editText(HWND window,Point point);
    void textChanged(const TextUpdate& update);
    void textClipboard(bool copy,bool cut);
    void closeTextEditor();
    void render(MonitorWindow& window);
    void report(const std::exception& error);
    HINSTANCE instance_{};
    std::shared_ptr<const DesktopImage> desktop_;
    Settings& settings_;
    std::function<void(SessionAction)> action_;
    std::function<void(std::wstring)> failure_;
    std::vector<std::unique_ptr<MonitorWindow>> windows_;
    HWND toolbar_{},tooltip_{},textWindow_{};
    HFONT toolbarFont_{};
    HBRUSH darkBrush_{};
    std::vector<ToolbarButton> buttons_;
    std::wstring toolbarStatus_;
    unsigned toolbarDpi_{96};int toolbarWidth_{},toolbarHeight_{};
    bool busy_{},dragging_{},selecting_{},closing_{},errorReported_{},pixelated_{};
    bool firstRegionCompleted_{};
    Point dragStart_{};Rect selection_{},original_{};Handle handle_{Handle::None};
    Tool tool_{Tool::Select};History history_;
    std::optional<Annotation> draft_;
    InlineText text_;
    ComPtr<TextStore> textStore_;
    bool textDragging_{},textCreating_{},textSelecting_{},caretVisible_{true};
    Point textStart_{};Rect textOriginal_{};Handle textHandle_{Handle::None};
};
}
