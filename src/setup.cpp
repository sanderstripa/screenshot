#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <filesystem>
#include <d2d1.h>
#include <dwrite.h>
#include "bundled_font.h"
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cwchar>
#include <vector>
#include <wincodec.h>
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"ole32.lib")
using Microsoft::WRL::ComPtr;

namespace {
constexpr int W = 640, H = 400;
constexpr int PAYLOAD_ID = 301;
constexpr UINT WM_INSTALL_COMPLETE = WM_APP + 10;
constexpr UINT_PTR CLOCK_ID = 2;
enum Step { Welcome, Installing, Installed, Hotkey, Confirm, Ready };
struct Config { UINT vk = VK_SNAPSHOT, mod = 0; std::wstring title = L"Print Screen"; };
std::wstring errorMessage(DWORD code) {
    wchar_t buffer[256]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,code,0,buffer,256,nullptr);
    return buffer;
}
bool inside(POINT p, D2D1_RECT_F r) {
    return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
}
D2D1_RECT_F primaryRect(Step s) {
    if (s == Welcome) return D2D1::RectF(219,314,421,368);
    if (s == Hotkey) return D2D1::RectF(424,327,603,374);
    if (s == Confirm) return D2D1::RectF(410,320,602,372);
    return D2D1::RectF(219,313,421,366);
}
D2D1_RECT_F secondaryRect() {return D2D1::RectF(35,321,176,373);}
D2D1_RECT_F inputRect() {return D2D1::RectF(40,184,599,249);}
struct Installer {
    HINSTANCE instance{};
    HWND hwnd{};
    Step step = Welcome;
    Config config{};
    std::wstring status{};
    std::wstring failureDetail{};
    DWORD failureCode = 0;
    float scale = 1.0f;
    HICON largeIcon = nullptr;
    std::atomic<int> progress{0};
    std::atomic<int> result{-99};
    std::atomic<bool> working{false};
    bool listening = false;
    bool light = false, english = false;
    const wchar_t* tr(const wchar_t* ru,const wchar_t* en) const {return english?en:ru;}
    D2D1_COLOR_F ink() const {return D2D1::ColorF(light?0x111111:0xF4F4F4);}
    D2D1_COLOR_F mutedInk() const {return D2D1::ColorF(light?0x75797F:0x8B8E94);}
    D2D1_COLOR_F edge() const {return D2D1::ColorF(light?0xD9DADC:0x333639);}
    D2D1_COLOR_F surface() const {return D2D1::ColorF(light?0xF5F5F5:0x0A0A0A);}
    void theme() {BOOL dark=!light;DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));COLORREF border=light?RGB(218,218,218):RGB(45,47,50);DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&border,sizeof(border));redraw();}
    bool needsRelogin = false;
    bool hoverPrimary = false, hoverSecondary = false, hoverInput = false;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Bitmap> artwork;
    std::thread worker;
    ComPtr<IDWriteFactory> fonts;
    ComPtr<IDWriteTextFormat> regular, medium, smallText, headline;
    ComPtr<FontLoader> fontLoader;
    ComPtr<IDWriteFontCollection> collection;
    std::wstring fontPath;
    HICON icon = nullptr;
    bool initializeFonts() {
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(fonts.GetAddressOf()))))return false;
        wchar_t temp[MAX_PATH]{},file[MAX_PATH]{};
        HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(202),RT_RCDATA);
        if(resource&&GetTempPathW(MAX_PATH,temp)&&GetTempFileNameW(temp,L"ssf",0,file)) {
            fontPath=file;
            HANDLE output=CreateFileW(file,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);
            DWORD written=0,size=SizeofResource(instance,resource);
            if(output!=INVALID_HANDLE_VALUE) {
                bool valid=WriteFile(output,LockResource(LoadResource(instance,resource)),size,&written,nullptr)&&written==size;
                CloseHandle(output);
                if(valid) {
                    fontLoader=Make<FontLoader>();
                    if(SUCCEEDED(fonts->RegisterFontCollectionLoader(fontLoader.Get())))
                        fonts->CreateCustomFontCollection(fontLoader.Get(),file,static_cast<UINT32>((wcslen(file)+1)*sizeof(wchar_t)),&collection);
                }
            }
        }
        const wchar_t* family=collection?L"Montserrat":L"Segoe UI";
        auto create=[&](float size,DWRITE_FONT_WEIGHT weight,ComPtr<IDWriteTextFormat>& font) {
            return SUCCEEDED(fonts->CreateTextFormat(family,collection.Get(),weight,DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,size,L"ru-ru",&font));
        };
        return create(16,DWRITE_FONT_WEIGHT_NORMAL,regular)&&create(15,DWRITE_FONT_WEIGHT_MEDIUM,medium)
            &&create(12,DWRITE_FONT_WEIGHT_NORMAL,smallText)&&create(25,DWRITE_FONT_WEIGHT_MEDIUM,headline);
    }

    void fill(D2D1_RECT_F rect,D2D1_COLOR_F c) {
        brush->SetColor(c); target->FillRectangle(rect,brush.Get());
    }
    void rounded(D2D1_RECT_F rect,float rad,D2D1_COLOR_F c) {
        brush->SetColor(c);target->FillRoundedRectangle(D2D1::RoundedRect(rect,rad,rad),brush.Get());
    }
    void outline(D2D1_RECT_F rect,float rad,D2D1_COLOR_F c,float width=1) {
        brush->SetColor(c);target->DrawRoundedRectangle(D2D1::RoundedRect(rect,rad,rad),brush.Get(),width);
    }
    void label(const std::wstring& value,float x,float y,float width,float height,
               IDWriteTextFormat* font,D2D1_COLOR_F color,
               DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING) {
        if(!font) return;
        font->SetTextAlignment(align);
        brush->SetColor(color);
        target->DrawTextW(value.c_str(),(UINT32)value.size(),font,
            D2D1::RectF(x,y,x+width,y+height),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void background() {
        target->Clear(surface());
    }
    void header() {
        label(L"Screenshot",64,17,220,32,smallText.Get(),ink());
        if(step==Welcome) {
            auto toggle=D2D1::RectF(477,16,519,38);
            rounded(toggle,11,surface());outline(toggle,11,edge());
            brush->SetColor(D2D1::ColorF(light?0x62666B:0x888B90));
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(light?508.0f:488.0f,27),7,7),brush.Get());
            regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            label(english?L"EN":L"RU",28,342,50,28,regular.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
            regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        // Render titlebar controls in custom client area, hit testing is in WM_LBUTTONUP.
        brush->SetColor(mutedInk());
        target->DrawLine(D2D1::Point2F(553,27),D2D1::Point2F(564,27),brush.Get(),1.2f);
        target->DrawLine(D2D1::Point2F(598,20),D2D1::Point2F(608,30),brush.Get(),1.25f);
        target->DrawLine(D2D1::Point2F(608,20),D2D1::Point2F(598,30),brush.Get(),1.25f);
    }
    void button(D2D1_RECT_F rect,std::wstring text,bool active=true) {
        rounded(rect,13,D2D1::ColorF(light?(active?0xFFFFFF:0xF5F5F5):(active?0x141414:0x0A0A0A)));
        outline(rect,13,edge());
        label(text,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,medium.Get(),
            ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void secondaryButton(D2D1_RECT_F rect,std::wstring text) {
        rounded(rect,12,D2D1::ColorF(light?0xFFFFFF:0x141414));
        outline(rect,12,edge());
        regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        label(text,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,regular.Get(),
            ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
        regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    void ring(float x,float y,bool warning=false,bool green=false) {
        auto color=ink();
        brush->SetColor(color);
        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),40,40),brush.Get(),3.6f);
        if(warning) {
            target->DrawLine(D2D1::Point2F(x,y-19),D2D1::Point2F(x,y+6),brush.Get(),4);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y+18),2.5f,2.5f),brush.Get());
        } else {
            target->DrawLine(D2D1::Point2F(x-18,y+2),
                D2D1::Point2F(x-5,y+15),brush.Get(),4);
            target->DrawLine(D2D1::Point2F(x-5,y+15),
                D2D1::Point2F(x+23,y-17),brush.Get(),4);
        }
    }
    void content() {
        const auto white=ink();
        const auto muted=mutedInk();
        if(step==Welcome) {
            label(L"Screenshot",40,231,560,50,headline.Get(),white,DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),tr(L"Установить",L"Install"));
        }
        if(step==Installing) {
            label(tr(L"Установка Screenshot",L"Installing Screenshot"),43,112,550,54,headline.Get(),white);
            rounded(D2D1::RectF(44,203,593,221),9,edge());
            int p=progress.load();
            float f=std::clamp(p/100.0f,0.0f,1.0f);
            if(f>0) rounded(D2D1::RectF(44,203,44+549*f,221),9,ink());
            label(std::to_wstring(p)+L"%",527,237,70,31,regular.Get(),muted,
                DWRITE_TEXT_ALIGNMENT_TRAILING);
            if(result.load()>=1) {
                label(tr(L"Не удалось установить приложение",L"Installation failed"),44,262,552,36,
                    regular.Get(),D2D1::ColorF(1,.65f,.56f));
                label(failureDetail,44,297,540,48,smallText.Get(),muted);
                button(D2D1::RectF(430,345,602,390),tr(L"Повторить",L"Retry"));
            }
        }
        if(step==Installed) {
            ring(320,143);
            label(tr(L"Установка завершена",L"Installation complete"),0,218,W,44,headline.Get(),white,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),tr(L"Продолжить",L"Continue"));
        }
        if(step==Hotkey) {
            label(tr(L"Горячая клавиша",L"Keyboard shortcut"),40,93,560,53,headline.Get(),white);
            label(tr(L"Нажмите клавишу для создания скриншота.",L"Press a key to capture screenshots."),40,146,563,34,
                regular.Get(),muted);
            rounded(inputRect(),11,surface());
            outline(inputRect(),11,listening?mutedInk():edge());
            label(listening?tr(L"Нажмите клавишу…",L"Press a key…"):config.title,57,184,520,65,
                medium.Get(),white,DWRITE_TEXT_ALIGNMENT_CENTER);
            if(listening) label(tr(L"Esc — отмена",L"Esc — cancel"),40,262,300,30,smallText.Get(),muted);
            button(primaryRect(step),tr(L"Далее",L"Next"));
        }
        if(step==Confirm) {
            ring(320,112,true);
            label(tr(L"Заменить системное действие?",L"Replace the Windows action?"),25,172,590,50,headline.Get(),white,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"Изменить действие Print Screen в Windows?",L"Change the Print Screen action in Windows?"),45,232,550,32,
                regular.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"Назначить эту клавишу Screenshot?",L"Assign this key to Screenshot?"),45,264,550,32,
                regular.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            secondaryButton(secondaryRect(),tr(L"Отмена",L"Cancel"));
            button(primaryRect(step),tr(L"Да, заменить",L"Yes, replace"));
        }
        if(step==Ready) {
            ring(320,120,false,true);
            label(tr(L"Готово!",L"Ready!"),40,199,560,52,headline.Get(),white,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            std::wstring note = needsRelogin
                ? tr(L"Для активации клавиши перезапустите сеанс Windows.",L"Sign out of Windows and back in to activate the key.")
                : std::wstring(tr(L"Горячая клавиша: ",L"Shortcut: "))+config.title;
            label(note,30,246,580,32,regular.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"PNG → Pictures\\Screenshot + буфер обмена",L"PNG to Pictures\\Screenshot + clipboard"),30,278,580,28,
                smallText.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),tr(L"Закрыть",L"Close"));
        }
    }
    void paint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd,&ps);
        if(!target) {
            RECT bounds{};GetClientRect(hwnd,&bounds);
            UINT pxWidth = static_cast<UINT>(bounds.right-bounds.left);
            UINT pxHeight = static_cast<UINT>(bounds.bottom-bounds.top);
            if(!factory || FAILED(factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(pxWidth,pxHeight)),&target))) {
                EndPaint(hwnd,&ps);return;
            }
            target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);
            ComPtr<IWICImagingFactory> imaging;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> bitmap;
            HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(201),RT_RCDATA);
            auto bytes=resource ? static_cast<BYTE*>(LockResource(LoadResource(instance,resource))) : nullptr;
            if(bytes && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&imaging))) && SUCCEEDED(imaging->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory(bytes,SizeofResource(instance,resource))) &&
                SUCCEEDED(imaging->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)) &&
                SUCCEEDED(decoder->GetFrame(0,&bitmap))) {
                ComPtr<IWICFormatConverter> converter;
                if(SUCCEEDED(imaging->CreateFormatConverter(&converter)) && SUCCEEDED(converter->Initialize(
                    bitmap.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,
                    WICBitmapPaletteTypeCustom)))target->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&artwork);
            }
        }
        target->BeginDraw();
        target->SetDpi(96*scale,96*scale);
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        background();header();content();
        if(artwork) {
            target->DrawBitmap(artwork.Get(),D2D1::RectF(18,14,46,42));
            if(step==Welcome)target->DrawBitmap(artwork.Get(),D2D1::RectF(254,83,386,215));
        }
        HRESULT hr=target->EndDraw();
        if(hr==D2DERR_RECREATE_TARGET){artwork.Reset();brush.Reset();target.Reset();}
        EndPaint(hwnd,&ps);
    }
    void redraw() { InvalidateRect(hwnd,nullptr,FALSE); }
    bool saveConfig() {
        HKEY key=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\SanderStripa\\Screenshot",0,
            nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
        DWORD vk=config.vk,mod=config.mod;
        bool a=RegSetValueExW(key,L"HotkeyVK",0,REG_DWORD,(BYTE*)&vk,sizeof(vk))==ERROR_SUCCESS;
        bool b=RegSetValueExW(key,L"HotkeyModifiers",0,REG_DWORD,(BYTE*)&mod,sizeof(mod))==ERROR_SUCCESS;
        RegCloseKey(key);
        return a&&b;
    }
    bool disableSnippingBinding() {
        HKEY key=nullptr;
        LONG code=RegCreateKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Keyboard",0,
            nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
        if(code!=ERROR_SUCCESS)return false;
        DWORD value=0;
        bool ok=RegSetValueExW(key,L"PrintScreenKeyForSnippingEnabled",0,
            REG_DWORD,(BYTE*)&value,sizeof(value))==ERROR_SUCCESS;
        RegCloseKey(key);
        // Some Windows versions require a sign-out for this setting to take effect.
        SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,
            reinterpret_cast<LPARAM>(L"Control Panel\\Keyboard"),
            SMTO_ABORTIFHUNG,300,nullptr);
        return ok;
    }
    bool verifyHotkey() {
        if(RegisterHotKey(hwnd,998,config.mod|MOD_NOREPEAT,config.vk)) {
            UnregisterHotKey(hwnd,998);return true;
        }
        return false;
    }
    bool launchBackground() {
        wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local)))return false;
        std::wstring file=std::wstring(local)+L"\\Programs\\Screenshot\\Screenshot.exe";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd=L"\""+file+L"\" --background";
        if(CreateProcessW(file.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,
            nullptr,nullptr,&si,&pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
        return false;
    }
    void activateHotkey() {
        if(!saveConfig()) {
            MessageBoxW(hwnd,tr(L"Не удалось сохранить клавишу.",L"Could not save the shortcut."),L"Screenshot",MB_OK|MB_ICONERROR);
            return;
        }
        if(config.vk==VK_SNAPSHOT && config.mod==0) {
            if(!disableSnippingBinding()) {
                MessageBoxW(hwnd,tr(L"Windows не разрешила изменить системную привязку Print Screen.\nОтключите её вручную в Параметры → Специальные возможности → Клавиатура.",L"Windows could not change the Print Screen binding.\nDisable it in Settings > Accessibility > Keyboard."),
                    L"Screenshot",MB_OK|MB_ICONWARNING);
                return;
            }
        }
        needsRelogin=!verifyHotkey();
        if(!launchBackground()) {
            MessageBoxW(hwnd,tr(L"Не удалось запустить Screenshot. Повторите установку.",L"Could not start Screenshot. Please reinstall."),
                L"Screenshot",MB_OK|MB_ICONERROR);
            return;
        }
        step=Ready;
        redraw();
    }
    void error(const std::wstring& stage,DWORD code) {
        failureCode=code;
        failureDetail=stage;
        if(code) failureDetail+=L"\nWindows: "+std::to_wstring(code)+L" "+errorMessage(code);
        result=1;
        PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);
    }
    bool writeDWORD(HKEY key,const wchar_t* name,DWORD value) {
        return RegSetValueExW(key,name,0,REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),sizeof(value))==ERROR_SUCCESS;
    }
    bool writeString(HKEY key,const wchar_t* name,const std::wstring& value) {
        return RegSetValueExW(key,name,0,REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
    }
    std::wstring installDirectory() {
        wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local)))
            return {};
        return std::wstring(local)+L"\\Programs\\Screenshot";
    }
    bool stopOldScreenshot(const std::wstring& filename) {
        HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,
            L"Local\\Screenshot.SanderStripa.Exit");
        if(event) { SetEvent(event); CloseHandle(event); Sleep(400); }
        // Previous v0.1/v0.2 binaries had no cooperative shutdown.
        // Only stop a process whose full image path matches our installation.
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE)return false;
        PROCESSENTRY32W entry{sizeof(entry)};
        bool ok=true;
        if(Process32FirstW(snapshot,&entry)) do {
            if(_wcsicmp(entry.szExeFile,L"Screenshot.exe")!=0 ||
                entry.th32ProcessID==GetCurrentProcessId())continue;
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|
                PROCESS_TERMINATE|SYNCHRONIZE,FALSE,entry.th32ProcessID);
            if(!process)continue;
            wchar_t image[32768]{};
            DWORD length=32768;
            if(QueryFullProcessImageNameW(process,0,image,&length) &&
                _wcsicmp(image,filename.c_str())==0) {
                if(!TerminateProcess(process,0) ||
                    WaitForSingleObject(process,5000)==WAIT_TIMEOUT)ok=false;
            }
            CloseHandle(process);
        } while(Process32NextW(snapshot,&entry));
        CloseHandle(snapshot);
        return ok;
    }
    bool registerInstall(const std::wstring& dir) {
        HKEY key=nullptr;
        const std::wstring exe=dir+L"\\Screenshot.exe";
        const std::wstring uninstaller=dir+L"\\Uninstall.exe";
        if(RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
        bool a=writeString(key,L"Screenshot",L"\""+exe+L"\" --background");
        RegCloseKey(key);
        if(!a)return false;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screenshot",
            0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
        bool ok=writeString(key,L"DisplayName",L"Screenshot") &&
            writeString(key,L"DisplayVersion",L"0.5.1") &&
            writeString(key,L"Publisher",L"Sander Stripa") &&
            writeString(key,L"InstallLocation",dir) &&
            writeString(key,L"DisplayIcon",exe) &&
            writeString(key,L"UninstallString",L"\""+uninstaller+L"\" --uninstall") &&
            writeDWORD(key,L"NoModify",1) &&
            writeDWORD(key,L"NoRepair",1);
        RegCloseKey(key);
        return ok&&createStartMenuShortcut(dir);
    }
    std::wstring shortcutPath() {
        wchar_t programs[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_PROGRAMS,nullptr,0,programs)))return {};
        return std::wstring(programs)+L"\\Screenshot.lnk";
    }
    bool createStartMenuShortcut(const std::wstring& dir) {
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
        bool ok=false;
        {
            ComPtr<IShellLinkW> link;
            ComPtr<IPersistFile> file;
            const auto path=shortcutPath();const auto exe=dir+L"\\Screenshot.exe";
            if(!path.empty()&&SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))&&
                SUCCEEDED(link->SetPath(exe.c_str()))&&SUCCEEDED(link->SetArguments(L"--settings"))&&
                SUCCEEDED(link->SetWorkingDirectory(dir.c_str()))&&SUCCEEDED(link->SetIconLocation(exe.c_str(),0))&&
                SUCCEEDED(link->SetDescription(L"Screenshot — настройки и горячая клавиша"))&&SUCCEEDED(link.As(&file))) {
                ok=SUCCEEDED(file->Save(path.c_str(),TRUE));
                if(ok)SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,path.c_str(),nullptr);
            }
        }
        if(SUCCEEDED(initialized))CoUninitialize();return ok;
    }
    int uninstall() {
        std::wstring dir=installDirectory();
        if(dir.empty())return 2;
        const std::wstring exe=dir+L"\\Screenshot.exe";
        stopOldScreenshot(exe);
        RegDeleteKeyValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"Screenshot");
        RegDeleteTreeW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screenshot");
        DeleteFileW(exe.c_str());
        const auto shortcut=shortcutPath();if(!shortcut.empty())DeleteFileW(shortcut.c_str());
        std::wstring self=dir+L"\\Uninstall.exe";
        MoveFileExW(self.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);
        RemoveDirectoryW(dir.c_str());
        return 0;
    }
    bool installPayload() {
        const std::wstring dir=installDirectory();
        if(dir.empty()){error(tr(L"Не удалось определить папку пользователя",L"Could not locate your user folder"),GetLastError());return false;}
        if(SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr)!=ERROR_SUCCESS &&
            GetFileAttributesW(dir.c_str())==INVALID_FILE_ATTRIBUTES) {
            error(std::wstring(tr(L"Не удалось создать папку ",L"Could not create folder "))+dir,GetLastError());return false;
        }
        progress=24;PostMessageW(hwnd,WM_APP+11,0,0);
        HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(PAYLOAD_ID),RT_RCDATA);
        if(!resource){error(tr(L"В установщике отсутствует Screenshot.exe",L"Screenshot.exe is missing from the installer"),ERROR_RESOURCE_NAME_NOT_FOUND);return false;}
        HGLOBAL data=LoadResource(instance,resource);
        const DWORD bytes=SizeofResource(instance,resource);
        const BYTE* ptr=static_cast<const BYTE*>(LockResource(data));
        if(!ptr||bytes<1024||ptr[0]!='M'||ptr[1]!='Z'){
            error(tr(L"Встроенный Screenshot.exe повреждён",L"The embedded Screenshot.exe is invalid"),ERROR_INVALID_DATA);return false;
        }
        std::wstring temp=dir+L"\\Screenshot.new";
        HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE) {error(tr(L"Не удалось записать Screenshot.exe",L"Could not write Screenshot.exe"),GetLastError());return false;}
        DWORD wrote=0;
        bool success=WriteFile(f,ptr,bytes,&wrote,nullptr) && wrote==bytes && FlushFileBuffers(f);
        DWORD writeError=GetLastError();
        CloseHandle(f);
        if(!success){DeleteFileW(temp.c_str());error(tr(L"Ошибка записи файлов",L"Could not write installation files"),writeError);return false;}
        progress=59;PostMessageW(hwnd,WM_APP+11,0,0);
        const std::wstring exe=dir+L"\\Screenshot.exe";
        if(!stopOldScreenshot(exe)){
            DeleteFileW(temp.c_str());error(tr(L"Не удалось закрыть прежнюю версию Screenshot",L"Could not close the previous Screenshot version"),ERROR_SHARING_VIOLATION);return false;
        }
        // Same-directory atomic replace: never leave a half-written Screenshot.exe.
        if(!MoveFileExW(temp.c_str(),exe.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DWORD code=GetLastError();DeleteFileW(temp.c_str());
            error(tr(L"Не удалось заменить Screenshot.exe. Проверьте антивирус и права доступа.",L"Could not replace Screenshot.exe. Check antivirus and permissions."),code);
            return false;
        }
        progress=83;PostMessageW(hwnd,WM_APP+11,0,0);
        wchar_t self[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,self,MAX_PATH) ||
            !CopyFileW(self,(dir+L"\\Uninstall.exe").c_str(),FALSE)){
            error(tr(L"Не удалось подготовить удаление приложения",L"Could not prepare the uninstaller"),GetLastError());return false;
        }
        if(!registerInstall(dir)){
            error(tr(L"Не удалось зарегистрировать приложение в Windows",L"Could not register the app in Windows"),GetLastError());return false;
        }
        // Replacing an EXE at the same path must also invalidate Explorer's cached icon.
        SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,exe.c_str(),nullptr);
        SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,self,nullptr);
        SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
        return true;
    }
    void installationThread() {
        failureDetail.clear();failureCode=0;
        if(installPayload()) {
            progress=100;result=0;
            PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);
        }
    }
    void startInstallation() {
        if(working.exchange(true))return;
        step=Installing;result=-99;progress=8;redraw();
        if(worker.joinable())worker.join();
        worker=std::thread([this]{installationThread();});
    }
    void choose(UINT vk,UINT mod) {
        if(vk==VK_SHIFT||vk==VK_CONTROL||vk==VK_MENU||vk==VK_LWIN||vk==VK_RWIN)
            return;
        // Do not permit plain letters, Escape, Enter or other basic typing keys.
        bool allowed=(vk>=VK_F1&&vk<=VK_F24)||vk==VK_SNAPSHOT||vk==VK_PAUSE||
                     vk==VK_SCROLL||vk==VK_INSERT||vk==VK_HOME;
        if(!allowed) {MessageBeep(MB_ICONWARNING);return;}
        config.vk=vk;config.mod=mod;
        if(vk==VK_SNAPSHOT) config.title=L"Print Screen";
        else if(vk>=VK_F1&&vk<=VK_F24) config.title=L"F"+std::to_wstring(vk-VK_F1+1);
        else if(vk==VK_INSERT)config.title=L"Insert";
        else if(vk==VK_HOME)config.title=L"Home";
        else if(vk==VK_PAUSE)config.title=L"Pause";
        else config.title=L"Scroll Lock";
        if(mod&MOD_CONTROL)config.title=L"Ctrl + "+config.title;
        if(mod&MOD_ALT)config.title=L"Alt + "+config.title;
        if(mod&MOD_SHIFT)config.title=L"Shift + "+config.title;
        listening=false;redraw();
    }
    void nextClick(POINT p) {
        if(step==Welcome&&p.x>=469&&p.x<=525&&p.y>=10&&p.y<=44){light=!light;theme();return;}
        if(step==Welcome&&p.x>=20&&p.x<=86&&p.y>=334&&p.y<=378){english=!english;redraw();return;}
        if(p.y<47 && p.x>574) { SendMessageW(hwnd,WM_CLOSE,0,0);return; }
        if(p.y<47 && p.x>530) { ShowWindow(hwnd,SW_MINIMIZE);return; }
        if(step==Hotkey&&inside(p,inputRect())) {listening=true;redraw();return;}
        if(step==Confirm&&inside(p,secondaryRect())) {step=Hotkey;redraw();return;}
        if(step==Installing && result.load()==1) {
            if(inside(p,D2D1::RectF(430,345,602,390))) {
                working=false;startInstallation();
            }
            return;
        }
        if(!inside(p,primaryRect(step)))return;
        switch(step) {
            case Welcome:startInstallation();break;
            case Installing:if(result.load()==1 && inside(p,D2D1::RectF(430,345,602,390))){working=false;startInstallation();}break;
            case Installed:step=Hotkey;redraw();break;
            case Hotkey:
                if(config.vk==VK_SNAPSHOT && config.mod==0) {step=Confirm;redraw();}
                else if(verifyHotkey()) activateHotkey();
                else MessageBoxW(hwnd,tr(L"Эта клавиша занята другим приложением.\nВыберите другую клавишу.",L"This shortcut is used by another app.\nChoose another key."),L"Screenshot",MB_OK|MB_ICONWARNING);
                break;
            case Confirm:activateHotkey();break;
            case Ready:DestroyWindow(hwnd);break;
        }
    }
};
Installer* app=nullptr;

LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    Installer* s=reinterpret_cast<Installer*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        s=reinterpret_cast<Installer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
        s->hwnd=hwnd;
    }
    if(!s)return DefWindowProcW(hwnd,msg,w,l);
    switch(msg) {
        case WM_NCHITTEST:{
            LRESULT hit=DefWindowProcW(hwnd,msg,w,l);
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            ScreenToClient(hwnd,&p);
            p.x=int(p.x/s->scale); p.y=int(p.y/s->scale);
            if(p.y<47&&p.x>=56&&p.x<(s->step==Welcome?465:527))return HTCAPTION;
            return hit;
        }
        case WM_NCCALCSIZE: if(w)return 0;break;
        case WM_DPICHANGED: {
            s->scale=HIWORD(w)/96.0f;
            auto rect=reinterpret_cast<RECT*>(l);
            SetWindowPos(hwnd,nullptr,rect->left,rect->top,int(W*s->scale),int(H*s->scale),
                SWP_NOZORDER|SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            s->artwork.Reset();s->brush.Reset();s->target.Reset();s->redraw();return 0;
        case WM_PAINT:s->paint();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_INSTALL_COMPLETE:
            if(s->result.load()==0)s->step=Installed;
            s->working=false;s->redraw();return 0;
        case WM_APP+11:s->redraw();return 0;
        case WM_LBUTTONUP:{
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            p.x=int(p.x/s->scale);p.y=int(p.y/s->scale);
            s->nextClick(p);return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if(s->step==Hotkey&&s->listening&&w==VK_SNAPSHOT) {
                UINT mod=0;
                if(GetKeyState(VK_CONTROL)&0x8000)mod|=MOD_CONTROL;
                if(GetKeyState(VK_MENU)&0x8000)mod|=MOD_ALT;
                if(GetKeyState(VK_SHIFT)&0x8000)mod|=MOD_SHIFT;
                s->choose(static_cast<UINT>(w),mod);return 0;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if(s->step==Hotkey&&s->listening) {
                if(w==VK_ESCAPE){s->listening=false;s->redraw();return 0;}
                UINT mod=0;
                if(GetKeyState(VK_CONTROL)&0x8000)mod|=MOD_CONTROL;
                if(GetKeyState(VK_MENU)&0x8000)mod|=MOD_ALT;
                if(GetKeyState(VK_SHIFT)&0x8000)mod|=MOD_SHIFT;
                s->choose(static_cast<UINT>(w),mod);return 0;
            }
            if(w==VK_ESCAPE&&s->step!=Installing){DestroyWindow(hwnd);return 0;}
            break;
        case WM_CLOSE:
            if(s->step==Installing&&s->working.load()) {
                MessageBeep(MB_ICONWARNING);return 0;
            }
            DestroyWindow(hwnd);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR commandLine,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (commandLine && wcsstr(commandLine,L"--self-test")) {
        HRSRC payload=FindResourceW(instance,MAKEINTRESOURCEW(PAYLOAD_ID),RT_RCDATA);
        if(!payload||SizeofResource(instance,payload)<1024)return 8;
        HGLOBAL bytes=LoadResource(instance,payload);
        const BYTE* ptr=static_cast<const BYTE*>(LockResource(bytes));
        return (ptr && ptr[0]=='M' && ptr[1]=='Z')?0:9;
    }
    HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(co))return 2;
    if (commandLine && wcsstr(commandLine,L"--uninstall")) {
        Installer probe;int code=probe.uninstall();CoUninitialize();return code;
    }
    if (commandLine && wcsstr(commandLine,L"--integration-test")) {
        Installer probe;
        probe.instance=instance;
        probe.installationThread();
        wchar_t local[MAX_PATH]{};
        if(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local)!=S_OK)
            {CoUninitialize();return 11;}
        const std::wstring installed=std::wstring(local)+L"\\Programs\\Screenshot\\Screenshot.exe";
        DWORD attrs=GetFileAttributesW(installed.c_str());
        int testResult=(probe.result.load()==0 && attrs!=INVALID_FILE_ATTRIBUTES)?0:12;
        CoUninitialize();
        return testResult;
    }
    Installer setup;setup.instance=instance;
    setup.scale=std::clamp(GetDpiForSystem()/96.0f,1.0f,2.5f);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,setup.factory.GetAddressOf());
    if(!setup.factory||!setup.initializeFonts()){CoUninitialize();return 3;}
    setup.medium->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Preview mode exercises the actual native UI without installing anything.
    if(commandLine && wcsstr(commandLine,L"--preview")) {
        int step=0,dpi=96;
        swscanf_s(commandLine,L"--preview %d %d",&step,&dpi);
        setup.step=static_cast<Step>(std::clamp(step,0,5));
        setup.scale=std::clamp(dpi/96.0f,1.0f,3.0f);
        setup.progress=62;
        setup.light=wcsstr(commandLine,L"light")!=nullptr;
        setup.english=wcsstr(commandLine,L"en")!=nullptr;
    }
    setup.icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),
        IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    setup.largeIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),
        IMAGE_ICON,256,256,LR_DEFAULTCOLOR));
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance=instance;wc.lpszClassName=L"ScreenshotPremiumSetup";
    wc.lpfnWndProc=procedure;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=setup.icon;
    RegisterClassExW(&wc);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int physicalW=int(W*setup.scale);
    int physicalH=int(H*setup.scale);
    int x=work.left+(work.right-work.left-physicalW)/2;
    int y=work.top+(work.bottom-work.top-physicalH)/2;
    HWND hwnd=CreateWindowExW(WS_EX_APPWINDOW,L"ScreenshotPremiumSetup",
        L"Screenshot",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,x,y,physicalW,physicalH,nullptr,nullptr,instance,&setup);
    if(!hwnd){CoUninitialize();return 4;}
    if(!commandLine || !wcsstr(commandLine,L"--preview")) {
        DWORD vk=VK_SNAPSHOT,mod=0,size=sizeof(DWORD);
        if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\SanderStripa\\Screenshot",L"HotkeyVK",
            RRF_RT_REG_DWORD,nullptr,&vk,&size)==ERROR_SUCCESS) {
            size=sizeof(DWORD);
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\SanderStripa\\Screenshot",L"HotkeyModifiers",
                RRF_RT_REG_DWORD,nullptr,&mod,&size);
            setup.choose(vk,mod&(MOD_CONTROL|MOD_SHIFT|MOD_ALT));
        }
    }
    // Keep a real DWM frame: Windows 11 owns corners and shadow, never a black bitmap mask.
    const DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    BOOL dark=!setup.light;
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    setup.theme();
    MARGINS margins{1,1,1,1};DwmExtendFrameIntoClientArea(hwnd,&margins);
    SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    ShowWindow(hwnd,show==SW_HIDE?SW_SHOWNORMAL:show);
    UpdateWindow(hwnd);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    // Do not free Installer before the background worker exits.
    if(setup.worker.joinable())setup.worker.join();
    setup.brush.Reset();setup.target.Reset();
    if(setup.icon)DestroyIcon(setup.icon);
    if(setup.largeIcon)DestroyIcon(setup.largeIcon);
    setup.icon=nullptr;
    setup.regular.Reset();setup.medium.Reset();setup.smallText.Reset();setup.headline.Reset();
    setup.collection.Reset();
    if(setup.fontLoader)setup.fonts->UnregisterFontCollectionLoader(setup.fontLoader.Get());
    setup.fontLoader.Reset();setup.fonts.Reset();setup.factory.Reset();
    if(!setup.fontPath.empty())DeleteFileW(setup.fontPath.c_str());
    CoUninitialize();
    return 0;
}


