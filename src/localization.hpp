#pragma once
#include <windows.h>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace shot {
enum class Language { Automatic, English, Russian, Chinese, Japanese, German, Spanish };
inline constexpr std::array Languages{Language::English,Language::Russian,Language::Chinese,Language::Japanese,Language::German,Language::Spanish};
Language resolveLanguage(Language preference, LANGID systemLanguage);
Language systemLanguage();
std::string_view languageTag(Language language);
Language parseLanguage(std::string_view tag);
const wchar_t* languageName(Language language);
HFONT createUiFont(Language language, int pixelHeight, int weight = FW_NORMAL);

enum class TextId {
    Automatic, SettingsTitle, SettingsHeader, SettingsInstructions, SettingsPrintScreen, AutomaticUpdates, LaunchAtSignIn, SignInHelp, LanguageLabel, Ok, Cancel, Apply,
    Select, Pen, Highlight, Rectangle, Ellipse, Line, Arrow, Text, Censor,
    SelectTip, PenTip, HighlightTip, RectangleTip, EllipseTip, LineTip, ArrowTip, TextTip, CensorTip,
    Color, Width, TextSize, CoverBlack, CoverPixelate, ColorTip, WidthTip, TextSizeTip, CensorModeTip,
    Undo, Redo, Copy, Save, SaveAs, UndoTip, RedoTip, CopyTip, SaveTip, SaveAsTip, CancelTip,
    WidthValue, TextSizeValue, Pixels, Exporting, SelectionStatus, OverlayTitle, ToolbarTitle,
    TrayTip, Ready, Capture, CaptureMenu, OpenFolder, Settings, Exit, CheckUpdates, UpdateBusy, InstallUpdate, ViewReleases,
    DisplayLayoutChanged, DisplayScalingChanged, ExportRetry, Copied, Saved,
    RateLimited, UpdateAvailable, UpdateManualAvailable, UpdateTitle, InstallQuestion, DownloadRestart, ViewNotes, UpdateExplanation, Downloading, ManualInstallHelp,
    UpToDate, NoNewRelease, NoRelease, IncompatibleRelease, ReleaseAvailable, HttpFailure, UpdateCanceled, InvalidMetadata, MalformedMetadata, ParserFailed,
    PngFilter, OpenFolderFailed, OpenReleaseFailed, StartupFailed, SettingsFailed, CaptureFailed, RenderFailed, ExportFailed, UpdateFailed, SaveSettingsFailed,
    CodecFormat, ImageTooLarge, EncodedTooLarge, IncompleteStream, ClipboardAllocation, IncompleteWrite, UniqueFilename, ClipboardTaken,
    AdapterDisconnected, PixelFormat, DisplayConfigurationChanged, HdrBrightness, HdrSurface, DesktopFormat, CaptureLayoutChanged, NoDisplays, CaptureSettingsChanged,
    CaptureCanceled, CaptureTimeout, ProtectedContent,
    UpdateStartupFailed, DownloadHttpFailure, HelperInitializeFailed, ReplaceFailed, BackupHelp,
    OperationFailed, SelectedImageTooLarge, TooManyScreenshots, SdrBrightnessChanged, HdrModeChanged,
    Count
};
std::wstring_view text(Language language, TextId id);
std::wstring format(Language language, TextId id, std::initializer_list<std::wstring_view> arguments = {});
struct Message {
    TextId id{TextId::StartupFailed};
    std::vector<std::wstring> arguments;
    std::wstring diagnostic;
    std::wstring render(Language language, TextId guidance = TextId::Count) const;
};
class AppError : public std::runtime_error {
public:
    explicit AppError(TextId id, std::vector<std::wstring> arguments = {});
    const Message& message() const { return message_; }
private:
    Message message_;
};
Message errorMessage(const std::exception& error, TextId context);
// Keep shell notification truncation on Unicode boundaries and mark shortened text.
std::wstring abbreviate(std::wstring_view value, size_t maximum);
}
