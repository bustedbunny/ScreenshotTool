#pragma once
#include "core.hpp"
#include <windows.h>
#include "localization.hpp"
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <dwrite.h>
#include <wincodec.h>
#include <memory>
#include <sstream>

namespace shot {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) { std::ostringstream s;s<<operation<<" (0x"<<std::hex<<static_cast<unsigned long>(hr)<<")";throw std::runtime_error(s.str()); }
}
inline void wincheck(BOOL ok,const char* operation) { if(!ok) check(HRESULT_FROM_WIN32(GetLastError()),operation); }
inline std::wstring widen(const std::string& value) {
    int n=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),n);return out;
}
inline RECT nativeRect(Rect r) { return {r.left,r.top,r.right,r.bottom}; }
inline D2D1_RECT_F drect(Rect r) { return D2D1::RectF(static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right),static_cast<float>(r.bottom)); }
inline D2D1_POINT_2F dpoint(Point p) { return D2D1::Point2F(static_cast<float>(p.x),static_cast<float>(p.y)); }
class ComApartment {
public:
    explicit ComApartment(DWORD mode=COINIT_APARTMENTTHREADED) { check(CoInitializeEx(nullptr,mode),"Initialize COM"); }
    ~ComApartment(){CoUninitialize();}
    ComApartment(const ComApartment&)=delete;
    ComApartment& operator=(const ComApartment&)=delete;
};
struct HandleCloser { void operator()(void* h) const { if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h); } };
using UniqueHandle=std::unique_ptr<void,HandleCloser>;
}
