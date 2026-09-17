#pragma once
#include "win.hpp"

namespace shot {
// One layout policy for editing, hit testing, preview and export. All units are pixels.
ComPtr<IDWriteTextLayout> textLayout(const Annotation& annotation);
// Geometry includes reflow that moves glyph/caret extents, even if the outer box is unchanged.
enum class TextChange : unsigned { None=0,Content=1,Selection=2,Geometry=4,Formatting=8,Composition=16 };
constexpr TextChange operator|(TextChange a,TextChange b){return static_cast<TextChange>(static_cast<unsigned>(a)|static_cast<unsigned>(b));}
constexpr bool has(TextChange value,TextChange flags){return (static_cast<unsigned>(value)&static_cast<unsigned>(flags))!=0;}
struct TextUpdate {
    TextChange flags;
    Rect previousVisual;
    // Only content changes supply old text; valid for the synchronous callback.
    std::wstring_view previousText{};
};
class InlineText {
public:
    static constexpr size_t limit=16384;
    struct State {Annotation annotation;UINT32 anchor{},caret{};bool trailing{};};
    std::function<void(const TextUpdate&)> changed;
    void begin(Point point,Rect crop,Pixel color,float size);
    std::optional<Annotation> finish(bool discard=false);
    bool active() const {return active_;}
    const Annotation& annotation() const {return state_.annotation;}
    IDWriteTextLayout* layout() const {return layout_.Get();}
    Rect visualBounds() const;
    UINT32 anchor() const {return state_.anchor;}
    UINT32 caret() const {return state_.caret;}
    UINT32 start() const {return std::min(anchor(),caret());}
    UINT32 end() const {return std::max(anchor(),caret());}
    void select(UINT32 anchor,UINT32 caret,bool trailing=false);
    void placeCaret(Point point,bool extend);
    void replace(UINT32 start,UINT32 end,std::wstring_view value);
    void insert(std::wstring_view value) {replace(start(),end(),value);}
    void character(wchar_t value);
    void erase(bool back,bool word);
    void navigate(UINT key,bool shift,bool control);
    void selectWord(UINT32 position);
    void format(Pixel color,float size);
    void bounds(Rect bounds);
    void beginGesture();
    void endGesture();
    void beginServiceEdit();void endServiceEdit();
    void beginComposition();
    void endComposition(bool cancel=false);
    bool composing() const {return composition_.has_value();}
    std::pair<UINT32,UINT32> compositionRange() const {return compositionRange_;}
    void compositionRange(UINT32 start,UINT32 end);
    bool undo();bool redo();
    bool canUndo() const {return !undo_.empty();}
    bool canRedo() const {return !redo_.empty();}
    UINT32 hit(Point point) const;
    Handle hitBorder(Point point) const;
    Rect caretRect(UINT32 position) const;
    std::vector<D2D1_RECT_F> rangeRects(UINT32 start,UINT32 end) const;
    UINT32 previous(UINT32 position) const;
    UINT32 next(UINT32 position) const;
    static Rect resized(Rect original,Handle handle,Point delta);
private:
    void rebuild();
    void fitBounds();
    void notify(TextChange flags,Rect previous,std::wstring_view old={});
    void restore(State state,TextChange flags=TextChange::None);
    void checkpoint();
    UINT32 word(UINT32 position,bool forward) const;
    State state_;
    ComPtr<IDWriteTextLayout> layout_;
    int contentHeight_{};
    Rect contentVisual_{};
    std::vector<UINT32> boundaries_;
    std::vector<State> undo_,redo_;
    std::optional<State> gesture_,composition_,serviceEdit_;
    bool serviceComposition_{};
    std::pair<UINT32,UINT32> compositionRange_{};
    bool active_{};
    wchar_t highSurrogate_{};
    std::optional<int> verticalX_;
};
}
