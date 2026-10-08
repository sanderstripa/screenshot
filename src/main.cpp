#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <shlobj.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr int HOTKEY_ID = 100;
constexpr UINT_PTR ANIMATION_TIMER = 101;
constexpr DWORD FADE_IN_MS = 140;
constexpr DWORD FADE_OUT_MS = 125;
constexpr int DRAG_THRESHOLD = 7;

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
    bool valid() const { return width > 0 && height > 0 &&
        bgra.size() == static_cast<size_t>(width) * height * 4; }
};

RECT intersectRect(RECT a, RECT b) {
    RECT r{};
    if (!IntersectRect(&r, &a, &b)) return RECT{0, 0, 0, 0};
    return r;
}

bool hasArea(RECT r) { return r.right > r.left && r.bottom > r.top; }

RECT normalizeRect(POINT a, POINT b) {
    return RECT{std::min(a.x,b.x), std::min(a.y,b.y),
                std::max(a.x,b.x), std::max(a.y,b.y)};
}

bool captureDesktop(RECT& bounds, Image& out) {
    bounds = RECT{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    if (w <= 0 || h <= 0 || static_cast<uint64_t>(w) * h > 100000000ULL) return false;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (mem && dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        if (BitBlt(mem, 0, 0, w, h, screen, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT)) {
            out.width = w; out.height = h;
            out.bgra.resize(static_cast<size_t>(w) * h * 4);
            std::memcpy(out.bgra.data(), bits, out.bgra.size());
            for (size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 255;
            ok = true;
        }
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    if (mem) DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

// Signed distance to a rounded rectangular screenshot. 0 is the boundary.
float roundedDistance(float x, float y, float w, float h, float radius) {
    float qx = std::abs(x - w * .5f) - (w * .5f - radius);
    float qy = std::abs(y - h * .5f) - (h * .5f - radius);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f))
        + std::min(std::max(qx, qy), 0.0f) - radius;
}

Image styleScreenshot(const Image& desktop, RECT desktopBounds, RECT crop, int padding = 24) {
    Image result;
    if (!desktop.valid()) return result;
    crop = intersectRect(crop, desktopBounds);
    if (!hasArea(crop)) return result;
    const int w = crop.right - crop.left, h = crop.bottom - crop.top;
    const int x0 = crop.left - desktopBounds.left, y0 = crop.top - desktopBounds.top;
    const int ow = w + 2 * padding, oh = h + 2 * padding;
    if (static_cast<uint64_t>(ow) * oh > 100000000ULL) return result;
    result.width = ow; result.height = oh;
    result.bgra.assign(static_cast<size_t>(ow) * oh * 4, 0);
    const float radius = std::min({12.0f, w * .18f, h * .18f});
    const float shadowSigma = 11.0f;
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            float fx = x - padding + .5f, fy = y - padding + .5f;
            float d = roundedDistance(fx, fy, float(w), float(h), radius);
            float ds = roundedDistance(fx, fy - 4.0f, float(w), float(h), radius);
            float falloff = std::max(0.0f, ds) / shadowSigma;
            float shadow = .23f * std::exp(-.5f * falloff * falloff);
            auto* dst = &result.bgra[(static_cast<size_t>(y) * ow + x) * 4];
            dst[3] = static_cast<uint8_t>(std::clamp(shadow * 255.0f, 0.0f, 255.0f));
            int sx = x - padding, sy = y - padding;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                float coverage = std::clamp(.5f - d, 0.0f, 1.0f);
                if (coverage > 0) {
                    const auto* src = &desktop.bgra[(static_cast<size_t>(y0 + sy)
                        * desktop.width + x0 + sx) * 4];
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                    dst[3] = static_cast<uint8_t>(255 * (coverage +
                        (1.0f - coverage) * shadow));
                }
            }
        }
    }
    return result;
}

std::vector<uint8_t> encodePNG(const Image& image) {
    std::vector<uint8_t> bytes;
    if (!image.valid()) return bytes;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    ComPtr<IStream> stream;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)))) return bytes;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return bytes;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return bytes;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return bytes;
    if (FAILED(encoder->CreateNewFrame(&frame, &properties))) return bytes;
    if (FAILED(frame->Initialize(properties.Get()))) return bytes;
    if (FAILED(frame->SetSize(image.width, image.height))) return bytes;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA)
        return bytes;
    UINT stride = static_cast<UINT>(image.width * 4);
    if (FAILED(frame->WritePixels(image.height, stride,
        static_cast<UINT>(image.bgra.size()), const_cast<BYTE*>(image.bgra.data()))))
        return bytes;
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) return bytes;
    STATSTG st{};
    if (FAILED(stream->Stat(&st, STATFLAG_NONAME)) ||
        st.cbSize.QuadPart <= 0 || st.cbSize.QuadPart > 500000000) return bytes;
    HGLOBAL memory = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.Get(), &memory))) return bytes;
    const auto* ptr = static_cast<const uint8_t*>(GlobalLock(memory));
    if (ptr) {
        bytes.assign(ptr, ptr + static_cast<size_t>(st.cbSize.QuadPart));
        GlobalUnlock(memory);
    }
    return bytes;
}

// Write the same encoded PNG used by the clipboard, with a collision-proof name.
bool savePNG(const std::vector<uint8_t>& png, std::wstring& filename) {
    wchar_t profile[32768]{};
    if (!GetEnvironmentVariableW(L"USERPROFILE", profile, 32768)) return false;
    std::wstring dir = std::wstring(profile) + L"\\Pictures\\Screenshot";
    int code = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (code != ERROR_SUCCESS && code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS)
        return false;
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t name[100]{};
    swprintf_s(name, L"\\Screenshot-%04u-%02u-%02u_%02u-%02u-%02u-%03u",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    for (int i=0;i<1000;++i) {
        filename=dir+name+(i ? L"-"+std::to_wstring(i) : L"")+L".png";
        HANDLE file=CreateFileW(filename.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            if(GetLastError()==ERROR_FILE_EXISTS)continue;
            return false;
        }
        DWORD written=0;
        bool ok=WriteFile(file,png.data(),static_cast<DWORD>(png.size()),&written,nullptr)
            && written==png.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if(!ok)DeleteFileW(filename.c_str());
        return ok;
    }
    return false;
}

HGLOBAL clipboardBlock(const void* data, size_t count) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, count);
    if (!h) return nullptr;
    void* p = GlobalLock(h);
    if (!p) { GlobalFree(h); return nullptr; }
    std::memcpy(p, data, count);
    GlobalUnlock(h);
    return h;
}

bool publishClipboard(HWND owner, const Image& styled, const std::vector<uint8_t>& png) {
    if (png.empty()) return false;
    bool opened=false;
    for(int attempt=0;attempt<20;++attempt) {
        if(OpenClipboard(owner)){opened=true;break;}
        Sleep(25);
    }
    if(!opened)return false;
    EmptyClipboard();
    bool success = false;
    UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (pngFormat) {
        HGLOBAL data = clipboardBlock(png.data(), png.size());
        if (data) {
            if (SetClipboardData(pngFormat, data)) success = true;
            else GlobalFree(data);
        }
    }

    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(v5);
    v5.bV5Width = styled.width;
    v5.bV5Height = -styled.height; // top-down BGRA
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5RedMask = 0x00FF0000;
    v5.bV5GreenMask = 0x0000FF00;
    v5.bV5BlueMask = 0x000000FF;
    v5.bV5AlphaMask = 0xFF000000;
    v5.bV5CSType = LCS_sRGB;
    v5.bV5Intent = LCS_GM_IMAGES;
    std::vector<uint8_t> dib(sizeof(v5) + styled.bgra.size());
    std::memcpy(dib.data(), &v5, sizeof(v5));
    std::memcpy(dib.data() + sizeof(v5), styled.bgra.data(), styled.bgra.size());
    HGLOBAL handle = clipboardBlock(dib.data(), dib.size());
    if (handle) {
        if (SetClipboardData(CF_DIBV5, handle)) success = true;
        else GlobalFree(handle);
    }

    // Compatibility fallback for apps that only read CF_DIB.
    BITMAPINFOHEADER legacy{};
    legacy.biSize = sizeof(legacy);
    legacy.biWidth = styled.width;
    legacy.biHeight = -styled.height;
    legacy.biPlanes = 1;
    legacy.biBitCount = 32;
    legacy.biCompression = BI_RGB;
    std::vector<uint8_t> oldDib(sizeof(legacy) + styled.bgra.size());
    std::memcpy(oldDib.data(), &legacy, sizeof(legacy));
    std::memcpy(oldDib.data() + sizeof(legacy), styled.bgra.data(), styled.bgra.size());
    handle = clipboardBlock(oldDib.data(), oldDib.size());
    if (handle && !SetClipboardData(CF_DIB, handle)) GlobalFree(handle);
    CloseClipboard();
    return success;
}

RECT windowAt(POINT pt, HWND overlay, HWND controller, RECT all) {
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (w == overlay || w == controller || !IsWindowVisible(w) ||
            IsIconic(w) || GetAncestor(w, GA_ROOT) != w) continue;
        LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        if (ex & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
            && cloaked) continue;
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
            GetWindowRect(w, &r);
        r = intersectRect(r, all);
        if (hasArea(r) && r.right - r.left >= 50 && r.bottom - r.top >= 40
            && PtInRect(&r, pt)) return r;
    }
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &monitor))
        return intersectRect(monitor.rcMonitor, all);
    return all;
}

struct App {
    HINSTANCE instance = nullptr;
    HWND controller = nullptr, overlay = nullptr;
    RECT desktopRect{};
    Image desktop;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<ID2D1SolidColorBrush> brush;
    bool pressed = false, dragging = false, finishing = false, hoveringClose = false;
    POINT down{}, pointer{};
    RECT selection{};
    ULONGLONG animationStart = 0;

    void dismiss() {
        if (GetCapture() == overlay) ReleaseCapture();
        if (overlay) { HWND old = overlay; overlay = nullptr; DestroyWindow(old); }
        brush.Reset(); bitmap.Reset(); target.Reset(); desktop.bgra.clear();
        pressed = dragging = finishing = hoveringClose = false;
    }

    bool initializeDrawing() {
        if (target) return true;
        if (!factory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            factory.GetAddressOf()))) return false;
        auto w = static_cast<UINT32>(desktop.width);
        auto h = static_cast<UINT32>(desktop.height);
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(factory->CreateHwndRenderTarget(props,
            D2D1::HwndRenderTargetProperties(overlay, D2D1::SizeU(w, h),
                D2D1_PRESENT_OPTIONS_IMMEDIATELY), &target))) return false;
        if (FAILED(target->CreateBitmap(D2D1::SizeU(w, h), desktop.bgra.data(),
            w * 4, D2D1::BitmapProperties(D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &bitmap))) return false;
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1), &brush)))
            return false;
        return true;
    }

    RECT toolbar() const {
        int center = desktop.width / 2;
        return RECT{center - 48, 14, center + 48, 55};
    }
    bool closeHit(POINT local) const {
        RECT b = toolbar();
        RECT closeRect{b.right - 43, b.top, b.right, b.bottom};
        return PtInRect(&closeRect, local) != 0;
    }
    POINT localPoint(POINT screen) const {
        return POINT{screen.x - desktopRect.left, screen.y - desktopRect.top};
    }
    D2D1_RECT_F displayRect(RECT screen) const {
        return D2D1::RectF(float(screen.left - desktopRect.left),
            float(screen.top - desktopRect.top), float(screen.right - desktopRect.left),
            float(screen.bottom - desktopRect.top));
    }

    void paint() {
        if (!overlay || !initializeDrawing()) return;
        float progress = std::clamp(float(GetTickCount64() - animationStart)
            / float(finishing ? FADE_OUT_MS : FADE_IN_MS), 0.0f, 1.0f);
        float opacity = finishing ? (1 - progress) : progress;
        target->SetDpi(96,96);
        target->BeginDraw();
        target->DrawBitmap(bitmap.Get());
        brush->SetColor(D2D1::ColorF(0.015f, .025f, .045f, .57f * opacity));
        target->FillRectangle(D2D1::RectF(0,0,float(desktop.width),float(desktop.height)),
            brush.Get());

        if (hasArea(selection)) {
            auto bounds = displayRect(selection);
            target->DrawBitmap(bitmap.Get(), bounds, 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, bounds);
            brush->SetColor(D2D1::ColorF(.15f, .55f, 1.0f, .24f * opacity));
            target->DrawRoundedRectangle(D2D1::RoundedRect(bounds, 10, 10),
                brush.Get(), 4.0f);
            brush->SetColor(D2D1::ColorF(.96f, .98f, 1.0f, .96f * opacity));
            target->DrawRoundedRectangle(D2D1::RoundedRect(bounds, 10, 10),
                brush.Get(), 1.5f);
        }

        auto b = toolbar();
        auto pill = D2D1::RoundedRect(D2D1::RectF(float(b.left),float(b.top),
            float(b.right),float(b.bottom)), 20, 20);
        brush->SetColor(D2D1::ColorF(.065f,.077f,.097f,.92f * opacity));
        target->FillRoundedRectangle(pill, brush.Get());
        brush->SetColor(D2D1::ColorF(.88f,.91f,.96f,.27f * opacity));
        target->DrawRoundedRectangle(pill, brush.Get(), .8f);
        float iconX = float(b.left + 24), cy = float((b.top + b.bottom) / 2);
        brush->SetColor(D2D1::ColorF(.92f,.95f,1,.96f * opacity));
        target->DrawLine(D2D1::Point2F(iconX-8,cy-2),D2D1::Point2F(iconX-8,cy-7),brush.Get(),1.7f);
        target->DrawLine(D2D1::Point2F(iconX-8,cy-7),D2D1::Point2F(iconX-3,cy-7),brush.Get(),1.7f);
        target->DrawLine(D2D1::Point2F(iconX+3,cy+7),D2D1::Point2F(iconX+8,cy+7),brush.Get(),1.7f);
        target->DrawLine(D2D1::Point2F(iconX+8,cy+7),D2D1::Point2F(iconX+8,cy+2),brush.Get(),1.7f);
        brush->SetColor(D2D1::ColorF(.85f,.88f,.92f,(hoveringClose ? 1.0f : .75f) * opacity));
        float x = float(b.right - 22);
        target->DrawLine(D2D1::Point2F(x-5,cy-5),D2D1::Point2F(x+5,cy+5),brush.Get(),1.7f);
        target->DrawLine(D2D1::Point2F(x+5,cy-5),D2D1::Point2F(x-5,cy+5),brush.Get(),1.7f);
        HRESULT hr = target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            brush.Reset(); bitmap.Reset(); target.Reset();
        }
    }

    void updateHover(POINT screen) {
        pointer = screen;
        POINT local = localPoint(screen);
        hoveringClose = closeHit(local);
        if (hoveringClose) selection = RECT{};
        else if (pressed && dragging) selection = intersectRect(normalizeRect(down, screen), desktopRect);
        else if (!pressed) selection = windowAt(screen, overlay, controller, desktopRect);
        InvalidateRect(overlay, nullptr, FALSE);
    }

    void begin() {
        if (overlay) return;
        if (!captureDesktop(desktopRect, desktop)) return;
        pressed = dragging = finishing = hoveringClose = false;
        animationStart = GetTickCount64();
        overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"ScreenshotOverlay", L"Screenshot", WS_POPUP,
            desktopRect.left, desktopRect.top, desktop.width, desktop.height,
            nullptr, nullptr, instance, this);
        if (!overlay) { desktop.bgra.clear(); return; }
        GetCursorPos(&pointer);
        ShowWindow(overlay, SW_SHOW);
        SetWindowPos(overlay, HWND_TOPMOST, desktopRect.left, desktopRect.top,
            desktop.width, desktop.height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(overlay);
        SetFocus(overlay);
        updateHover(pointer);
        SetTimer(overlay, ANIMATION_TIMER, 16, nullptr);
    }

    void accept(RECT area) {
        area = intersectRect(area, desktopRect);
        if (!hasArea(area)) { dismiss(); return; }
        // The clipboard is populated before the 125-ms success animation.
        Image styled = styleScreenshot(desktop, desktopRect, area);
        auto png=encodePNG(styled);
        std::wstring filename;
        bool saved=styled.valid() && !png.empty() && savePNG(png,filename);
        bool copied=saved && publishClipboard(overlay,styled,png);
        if (saved && copied) {
            selection = area;
            finishing = true;
            animationStart = GetTickCount64();
            InvalidateRect(overlay, nullptr, FALSE);
        } else {
            dismiss();
            MessageBoxW(nullptr, saved
                ? L"PNG сохранён в Pictures\\Screenshot, но буфер обмена занят. Повторите захват."
                : L"Не удалось сохранить PNG в Pictures\\Screenshot. Проверьте доступ к папке.",
                L"Screenshot", MB_OK|MB_ICONWARNING);
        }
    }

    void handleOverlay(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_ERASEBKGND: return;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(overlay, &ps);
            paint();
            EndPaint(overlay, &ps);
            return;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS)); return;
        case WM_TIMER:
            if (wp == ANIMATION_TIMER) {
                if (finishing && GetTickCount64() - animationStart >= FADE_OUT_MS)
                    dismiss();
                else InvalidateRect(overlay, nullptr, FALSE);
            }
            return;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) dismiss();
            return;
        case WM_LBUTTONDOWN: {
            if (finishing) return;
            POINT pt{}; GetCursorPos(&pt);
            if (closeHit(localPoint(pt))) { dismiss(); return; }
            pressed = true; dragging = false; down = pt; pointer = pt;
            SetCapture(overlay);
            return;
        }
        case WM_MOUSEMOVE: {
            if (finishing) return;
            POINT pt{}; GetCursorPos(&pt);
            if (pressed && (std::abs(pt.x - down.x) > DRAG_THRESHOLD ||
                            std::abs(pt.y - down.y) > DRAG_THRESHOLD))
                dragging = true;
            updateHover(pt);
            return;
        }
        case WM_LBUTTONUP: {
            if (finishing || !pressed) return;
            POINT pt{}; GetCursorPos(&pt);
            if (GetCapture() == overlay) ReleaseCapture();
            bool didDrag = dragging;
            pressed = dragging = false;
            RECT crop = didDrag ? normalizeRect(down, pt) :
                windowAt(pt, overlay, controller, desktopRect);
            accept(crop);
            return;
        }
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            dismiss(); return;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !finishing) dismiss();
            return;
        }
    }
};

App* app = nullptr;

LRESULT CALLBACK overlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* p = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(p->lpCreateParams));
    }
    auto* p = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (p) {
        p->handleOverlay(msg, wp, lp);
        if (msg == WM_ERASEBKGND || msg == WM_PAINT || msg == WM_SETCURSOR ||
            msg == WM_TIMER || msg == WM_KEYDOWN || msg == WM_LBUTTONDOWN ||
            msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN ||
            msg == WM_MBUTTONDOWN || msg == WM_ACTIVATE) return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK controllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY && wp == HOTKEY_ID) {
        if (app) app->begin();
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int selfTest(bool outputTest=false) {
    Image sample{};
    sample.width = sample.height = 32;
    sample.bgra.resize(32 * 32 * 4, 0xFF);
    RECT rect{0,0,32,32};
    Image styled = styleScreenshot(sample, rect, rect);
    auto png = encodePNG(styled);
    constexpr uint8_t signature[] = {137,80,78,71,13,10,26,10};
    if(outputTest) {
        std::wstring file;
        if(!savePNG(png,file) || !publishClipboard(nullptr,styled,png))return 4;
        // Decode the actual saved file rather than merely checking a PNG signature.
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        UINT w=0,h=0;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) || FAILED(factory->CreateDecoderFromFilename(
            file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)) ||
            FAILED(decoder->GetFrame(0,&frame)) || FAILED(frame->GetSize(&w,&h)) ||
            w!=static_cast<UINT>(styled.width) || h!=static_cast<UINT>(styled.height))return 5;
    }
    return styled.valid() && png.size() > 8 &&
        std::memcmp(png.data(), signature, sizeof(signature)) == 0 ? 0 : 1;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 2;
    UINT hotkeyVK = VK_SNAPSHOT;
    UINT hotkeyModifiers = 0;
    HKEY configKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SanderStripa\\Screenshot",
            0, KEY_QUERY_VALUE, &configKey) == ERROR_SUCCESS) {
        DWORD type = 0, size = sizeof(DWORD), value = 0;
        if (RegQueryValueExW(configKey, L"HotkeyVK", nullptr, &type,
                reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS
                && type == REG_DWORD && value >= VK_BACK && value <= VK_F24)
            hotkeyVK = value;
        value = 0; type = 0; size = sizeof(DWORD);
        if (RegQueryValueExW(configKey, L"HotkeyModifiers", nullptr, &type,
                reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS
                && type == REG_DWORD)
            hotkeyModifiers = value & (MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN);
        RegCloseKey(configKey);
    }
    // CI: imitate a running installation while the new setup replaces it.
    if (commandLine && wcsstr(commandLine,L"--hold-install-test")) {
        HANDLE stop = CreateEventW(nullptr,TRUE,FALSE,
            L"Local\\Screenshot.SanderStripa.Exit");
        if(!stop) { CoUninitialize(); return 18; }
        WaitForSingleObject(stop,30000);
        CloseHandle(stop);
        CoUninitialize();
        return 0;
    }
    if (commandLine && wcsstr(commandLine, L"--self-test")) {
        int result = selfTest(wcsstr(commandLine,L"--self-test-output")!=nullptr);
        CoUninitialize();
        return result;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Screenshot.SanderStripa.Singleton");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }
    App current{};
    current.instance = instance;
    app = &current;
    WNDCLASSEXW controllerClass{};
    controllerClass.cbSize = sizeof(controllerClass);
    controllerClass.hInstance = instance;
    controllerClass.lpfnWndProc = controllerProc;
    controllerClass.lpszClassName = L"ScreenshotController";
    WNDCLASSEXW overlayClass = controllerClass;
    overlayClass.lpfnWndProc = overlayProc;
    overlayClass.lpszClassName = L"ScreenshotOverlay";
    overlayClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassExW(&controllerClass);
    RegisterClassExW(&overlayClass);
    current.controller = CreateWindowExW(0, L"ScreenshotController", L"Screenshot",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    const bool captureNow=commandLine && wcsstr(commandLine,L"--capture-now");
    if (!current.controller || !RegisterHotKey(current.controller, HOTKEY_ID,
        captureNow ? MOD_NOREPEAT : hotkeyModifiers | MOD_NOREPEAT,
        captureNow ? VK_F24 : hotkeyVK)) {
        MessageBoxW(nullptr, L"The configured Screenshot shortcut is unavailable.\n\n"
            L"Run Screenshot Setup again to choose another key. For Print Screen, "
            L"disable the Snipping Tool key in Windows Settings > Accessibility > Keyboard "
            L"and sign out if required.", L"Screenshot — shortcut unavailable",
            MB_OK | MB_ICONINFORMATION);
        if (current.controller) DestroyWindow(current.controller);
        CloseHandle(mutex);
        CoUninitialize();
        return 3;
    }
    // Cooperative shutdown allows the installer to replace a running copy cleanly.
    HANDLE quitEvent = CreateEventW(nullptr, TRUE, FALSE,
        L"Local\\Screenshot.SanderStripa.Exit");
    if(captureNow)PostMessageW(current.controller,WM_HOTKEY,HOTKEY_ID,0);
    MSG msg{};
    bool running = true;
    while (running) {
        DWORD state = MsgWaitForMultipleObjects(quitEvent ? 1 : 0,
            quitEvent ? &quitEvent : nullptr, FALSE, 500, QS_ALLINPUT);
        if (quitEvent && state == WAIT_OBJECT_0) break;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if(quitEvent) CloseHandle(quitEvent);
    current.dismiss();
    UnregisterHotKey(current.controller, HOTKEY_ID);
    if (IsWindow(current.controller)) DestroyWindow(current.controller);
    CloseHandle(mutex);
    CoUninitialize();
    return 0;
}
