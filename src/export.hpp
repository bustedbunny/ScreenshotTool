#pragma once
#include "graphics.hpp"
namespace shot {
struct EncodedImage { int width{},height{}; std::vector<BYTE> bgra,png,jxr; };
class ExportService {
public:
    EncodedImage prepare(const DesktopImage& desktop,Rect crop,std::span<const Annotation> annotations,bool includeHdr);
    static std::vector<BYTE> encodePng(const Image& linear);
    static std::vector<BYTE> encodeJxr(const Image& scRgb);
    static std::vector<BYTE> toBgra(const Image& linear);
    static std::vector<std::filesystem::path> quickSave(const EncodedImage& image,const std::filesystem::path& directory);
    static void saveAs(const EncodedImage& image,const std::filesystem::path& path);
    static std::optional<std::filesystem::path> chooseSavePath(HWND owner,const std::filesystem::path& folder);
    static void copy(HWND owner,const EncodedImage& image);
};
std::filesystem::path picturesDirectory();
std::wstring currentStem();
std::wstring uniqueToken();
void writeNewFile(const std::filesystem::path& path,std::span<const BYTE> bytes);
FileOperations nativeFileOperations();
}
