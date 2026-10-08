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
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cwchar>
#include <vector>
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
    if (s == Welcome) return D2D1::RectF(417,314,607,368);
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
    bool needsRelogin = false;
    bool hoverPrimary = false, hoverSecondary = false, hoverInput = false;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> fonts;
    ComPtr<IDWriteTextFormat> regular, medium, smallText, headline;
    HICON icon = nullptr;

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
        // Soft, window-sized blue-black lighting, not a bitmap stretched to fit.
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP sg[3] = {
            {0,D2D1::ColorF(0.018f,0.040f,0.071f)},
            {.56f,D2D1::ColorF(0.039f,0.071f,0.126f)},
            {1,D2D1::ColorF(0.020f,0.055f,0.133f)}
        };
        if(SUCCEEDED(target->CreateGradientStopCollection(sg,3,&stops))) {
            ComPtr<ID2D1LinearGradientBrush> lg;
            target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(10,0),D2D1::Point2F(630,425)),stops.Get(),&lg);
            if(lg) target->FillRectangle(D2D1::RectF(0,0,W,H),lg.Get());
        }
        // Delicate diagonal cobalt shape echoing the selected first mockup.
        ComPtr<ID2D1PathGeometry> path;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))) {
            ComPtr<ID2D1GeometrySink> sink;
            if(SUCCEEDED(path->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(462,400),D2D1_FIGURE_BEGIN_FILLED);
                sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(505,282),
                    D2D1::Point2F(611,227),D2D1::Point2F(640,164)));
                sink->AddLine(D2D1::Point2F(640,400));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                brush->SetColor(D2D1::ColorF(.025f,.22f,.58f,.16f));
                target->FillGeometry(path.Get(),brush.Get());
            }
        }
        outline(D2D1::RectF(.5f,.5f,W-.5f,H-.5f),15,
            D2D1::ColorF(.53f,.68f,.89f,.38f));
    }
    void header() {
        label(L"Screenshot",64,17,220,32,smallText.Get(),D2D1::ColorF(.96f,.98f,1));
        // Render titlebar controls in custom client area, hit testing is in WM_LBUTTONUP.
        brush->SetColor(D2D1::ColorF(.70f,.79f,.91f));
        target->DrawLine(D2D1::Point2F(553,27),D2D1::Point2F(564,27),brush.Get(),1.2f);
        target->DrawLine(D2D1::Point2F(598,20),D2D1::Point2F(608,30),brush.Get(),1.25f);
        target->DrawLine(D2D1::Point2F(608,20),D2D1::Point2F(598,30),brush.Get(),1.25f);
    }
    void logo(float x,float y,float size) {
        // Layers and two white crop corners echo the approved Screenshot icon.
        rounded(D2D1::RectF(x+size*.24f,y+size*.10f,x+size*.91f,y+size*.76f),
            size*.10f,D2D1::ColorF(.26f,.36f,.50f,.56f));
        rounded(D2D1::RectF(x+size*.10f,y+size*.24f,x+size*.78f,y+size*.92f),
            size*.11f,D2D1::ColorF(.76f,.83f,.92f));
        brush->SetColor(D2D1::ColorF(.11f,.49f,.99f));
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+size*.75f,y+size*.88f),
            size*.22f,size*.22f),brush.Get());
        brush->SetColor(D2D1::ColorF(.99f,.99f,1));
        float thick=std::max(3.0f,size*.065f);
        target->DrawLine(D2D1::Point2F(x+size*.17f,y+size*.42f),
                         D2D1::Point2F(x+size*.17f,y+size*.32f),brush.Get(),thick);
        target->DrawLine(D2D1::Point2F(x+size*.17f,y+size*.32f),
                         D2D1::Point2F(x+size*.28f,y+size*.32f),brush.Get(),thick);
        target->DrawLine(D2D1::Point2F(x+size*.68f,y+size*.82f),
                         D2D1::Point2F(x+size*.75f,y+size*.82f),brush.Get(),thick);
        target->DrawLine(D2D1::Point2F(x+size*.75f,y+size*.82f),
                         D2D1::Point2F(x+size*.75f,y+size*.74f),brush.Get(),thick);
    }
    void button(D2D1_RECT_F rect,std::wstring text,bool active=true) {
        rounded(rect,13,D2D1::ColorF(active?.055f:.13f,active?.39f:.16f,
            active?.95f:.21f,1));
        outline(rect,13,D2D1::ColorF(.47f,.72f,1,.34f));
        label(text,rect.left,rect.top+13,rect.right-rect.left,30,medium.Get(),
            D2D1::ColorF(1,1,1),DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void secondaryButton(D2D1_RECT_F rect,std::wstring text) {
        rounded(rect,12,D2D1::ColorF(.12f,.17f,.25f,.94f));
        outline(rect,12,D2D1::ColorF(.53f,.66f,.83f,.36f));
        label(text,rect.left,rect.top+12,rect.right-rect.left,30,regular.Get(),
            D2D1::ColorF(.91f,.94f,1),DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void ring(float x,float y,bool warning=false,bool green=false) {
        auto color=warning?D2D1::ColorF(1,.67f,.19f):
            green?D2D1::ColorF(.30f,.86f,.71f):D2D1::ColorF(.45f,.72f,1);
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
        const auto white=D2D1::ColorF(.96f,.97f,1);
        const auto muted=D2D1::ColorF(.72f,.79f,.89f);
        if(step==Welcome) {
            label(L"Screenshot",252,133,330,60,headline.Get(),white);
            button(primaryRect(step),L"Установить  →");
        }
        if(step==Installing) {
            label(L"Установка Screenshot",43,112,550,54,headline.Get(),white);
            rounded(D2D1::RectF(44,203,593,221),9,D2D1::ColorF(.19f,.25f,.35f,.90f));
            int p=progress.load();
            float f=std::clamp(p/100.0f,0.0f,1.0f);
            if(f>0) rounded(D2D1::RectF(44,203,44+549*f,221),9,D2D1::ColorF(.10f,.43f,.99f));
            label(std::to_wstring(p)+L"%",527,237,70,31,regular.Get(),muted,
                DWRITE_TEXT_ALIGNMENT_TRAILING);
            if(result.load()>=1) {
                label(L"Не удалось установить приложение",44,262,552,36,
                    regular.Get(),D2D1::ColorF(1,.65f,.56f));
                label(failureDetail,44,297,540,48,smallText.Get(),muted);
                button(D2D1::RectF(430,345,602,390),L"Повторить");
            }
        }
        if(step==Installed) {
            ring(320,143);
            label(L"Установка завершена",0,218,W,44,headline.Get(),white,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),L"Продолжить  →");
        }
        if(step==Hotkey) {
            label(L"Горячая клавиша",40,93,560,53,headline.Get(),white);
            label(L"Нажмите клавишу для создания скриншота.",40,146,563,34,
                regular.Get(),muted);
            rounded(inputRect(),11,D2D1::ColorF(.018f,.031f,.052f,.94f));
            outline(inputRect(),11,D2D1::ColorF(listening?.41f:.37f,
                listening?.67f:.50f,listening?1.0f:.68f));
            label(listening?L"Нажмите клавишу…":config.title,57,198,520,37,
                medium.Get(),white);
            if(listening) label(L"Esc — отмена",40,262,300,30,smallText.Get(),muted);
            button(primaryRect(step),L"Далее  →");
        }
        if(step==Confirm) {
            ring(320,112,true);
            label(L"Заменить системное действие?",25,172,590,50,headline.Get(),white,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            label(L"Print Screen сейчас открывает «Ножницы».",45,232,550,32,
                regular.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            label(L"Назначить эту клавишу Screenshot?",45,264,550,32,
                regular.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            secondaryButton(secondaryRect(),L"Отмена");
            button(primaryRect(step),L"Да, заменить");
        }
        if(step==Ready) {
            ring(320,120,false,true);
            label(L"Готово!",40,199,560,52,headline.Get(),white,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            std::wstring note = needsRelogin
                ? L"Для активации клавиши перезапустите сеанс Windows."
                : L"Горячая клавиша: "+config.title;
            label(note,30,252,580,46,regular.Get(),muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),L"Закрыть");
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
        }
        target->BeginDraw();
        target->SetDpi(96,96);
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale));
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        background();header();content();
        HRESULT hr=target->EndDraw();
        if(hr==D2DERR_RECREATE_TARGET){brush.Reset();target.Reset();}
        // The approved embedded icon, rather than Inno Setup's stock setup artwork.
        if(icon) DrawIconEx(ps.hdc,int(17*scale),int(13*scale),icon,
                int(31*scale),int(31*scale),0,nullptr,DI_NORMAL);
        if(step==Welcome && largeIcon) {
            DrawIconEx(ps.hdc,int(60*scale),int(109*scale),largeIcon,
                int(160*scale),int(160*scale),0,nullptr,DI_NORMAL);
        }
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
    void launchBackground() {
        wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local)))return;
        std::wstring file=std::wstring(local)+L"\\Programs\\Screenshot\\Screenshot.exe";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd=L"\""+file+L"\"";
        if(CreateProcessW(file.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,
            nullptr,nullptr,&si,&pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    void activateHotkey() {
        if(!saveConfig()) {
            MessageBoxW(hwnd,L"Не удалось сохранить клавишу.",L"Screenshot",MB_OK|MB_ICONERROR);
            return;
        }
        if(config.vk==VK_SNAPSHOT && config.mod==0) {
            if(!disableSnippingBinding()) {
                MessageBoxW(hwnd,L"Windows не разрешила изменить системную привязку Print Screen.\n"
                    L"Отключите её вручную в Параметры → Специальные возможности → Клавиатура.",
                    L"Screenshot",MB_OK|MB_ICONWARNING);
            }
        }
        needsRelogin=!verifyHotkey();
        launchBackground();
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
        bool a=writeString(key,L"Screenshot",L"\""+exe+L"\"");
        RegCloseKey(key);
        if(!a)return false;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screenshot",
            0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
        bool ok=writeString(key,L"DisplayName",L"Screenshot") &&
            writeString(key,L"DisplayVersion",L"0.3.0") &&
            writeString(key,L"Publisher",L"Sander Stripa") &&
            writeString(key,L"InstallLocation",dir) &&
            writeString(key,L"DisplayIcon",exe) &&
            writeString(key,L"UninstallString",L"\""+uninstaller+L"\" --uninstall") &&
            writeDWORD(key,L"NoModify",1) &&
            writeDWORD(key,L"NoRepair",1);
        RegCloseKey(key);
        return ok;
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
        std::wstring self=dir+L"\\Uninstall.exe";
        MoveFileExW(self.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);
        RemoveDirectoryW(dir.c_str());
        return 0;
    }
    bool installPayload() {
        const std::wstring dir=installDirectory();
        if(dir.empty()){error(L"Не удалось определить папку пользователя",GetLastError());return false;}
        if(SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr)!=ERROR_SUCCESS &&
            GetFileAttributesW(dir.c_str())==INVALID_FILE_ATTRIBUTES) {
            error(L"Не удалось создать папку "+dir,GetLastError());return false;
        }
        progress=24;PostMessageW(hwnd,WM_APP+11,0,0);
        HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(PAYLOAD_ID),RT_RCDATA);
        if(!resource){error(L"В установщике отсутствует Screenshot.exe",ERROR_RESOURCE_NAME_NOT_FOUND);return false;}
        HGLOBAL data=LoadResource(instance,resource);
        const DWORD bytes=SizeofResource(instance,resource);
        const BYTE* ptr=static_cast<const BYTE*>(LockResource(data));
        if(!ptr||bytes<1024||ptr[0]!='M'||ptr[1]!='Z'){
            error(L"Встроенный Screenshot.exe повреждён",ERROR_INVALID_DATA);return false;
        }
        std::wstring temp=dir+L"\\Screenshot.new";
        HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE) {error(L"Не удалось записать Screenshot.exe",GetLastError());return false;}
        DWORD wrote=0;
        bool success=WriteFile(f,ptr,bytes,&wrote,nullptr) && wrote==bytes && FlushFileBuffers(f);
        DWORD writeError=GetLastError();
        CloseHandle(f);
        if(!success){DeleteFileW(temp.c_str());error(L"Ошибка записи файлов",writeError);return false;}
        progress=59;PostMessageW(hwnd,WM_APP+11,0,0);
        const std::wstring exe=dir+L"\\Screenshot.exe";
        if(!stopOldScreenshot(exe)){
            DeleteFileW(temp.c_str());error(L"Не удалось закрыть прежнюю версию Screenshot",ERROR_SHARING_VIOLATION);return false;
        }
        // Same-directory atomic replace: never leave a half-written Screenshot.exe.
        if(!MoveFileExW(temp.c_str(),exe.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DWORD code=GetLastError();DeleteFileW(temp.c_str());
            error(L"Не удалось заменить Screenshot.exe. Проверьте антивирус и права доступа.",code);
            return false;
        }
        progress=83;PostMessageW(hwnd,WM_APP+11,0,0);
        wchar_t self[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,self,MAX_PATH) ||
            !CopyFileW(self,(dir+L"\\Uninstall.exe").c_str(),FALSE)){
            error(L"Не удалось подготовить удаление приложения",GetLastError());return false;
        }
        if(!registerInstall(dir)){
            error(L"Не удалось зарегистрировать приложение в Windows",GetLastError());return false;
        }
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
        std::thread([this]{installationThread();}).detach();
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
        if(p.y<47 && p.x>574) { DestroyWindow(hwnd);return; }
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
                else MessageBoxW(hwnd,L"Эта клавиша занята другим приложением.\n"
                     L"Выберите другую клавишу.",L"Screenshot",MB_OK|MB_ICONWARNING);
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
            if(p.y<47&&p.x>=56&&p.x<527)return HTCAPTION;
            return hit;
        }
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
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(setup.fonts.GetAddressOf()));
    if(!setup.factory||!setup.fonts){CoUninitialize();return 3;}
    setup.fonts->CreateTextFormat(L"Segoe UI Variable",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18,L"ru-ru",&setup.regular);
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,17,L"ru-ru",&setup.medium);
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"ru-ru",&setup.smallText);
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,30,L"ru-ru",&setup.headline);
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
        L"Screenshot",WS_POPUP,x,y,physicalW,physicalH,nullptr,nullptr,instance,&setup);
    if(!hwnd){CoUninitialize();return 4;}
    // Clip actual HWND to a rounded silhouette: no opaque black corner pixels.
    int radius=int(28*setup.scale);
    HRGN region=CreateRoundRectRgn(0,0,physicalW+1,physicalH+1,radius,radius);
    if(region && !SetWindowRgn(hwnd,region,TRUE)) DeleteObject(region);
    const DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    ShowWindow(hwnd,show==SW_HIDE?SW_SHOWNORMAL:show);
    UpdateWindow(hwnd);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    // Do not free Installer before the background worker exits.
    while(setup.working.load())Sleep(40);
    setup.brush.Reset();setup.target.Reset();
    if(setup.icon)DestroyIcon(setup.icon);
    if(setup.largeIcon)DestroyIcon(setup.largeIcon);
    setup.icon=nullptr;
    setup.regular.Reset();setup.medium.Reset();setup.smallText.Reset();setup.headline.Reset();
    setup.fonts.Reset();setup.factory.Reset();
    CoUninitialize();
    return 0;
}
