#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
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
            rounded(D2D1::RectF(43,110,214,288),32,D2D1::ColorF(.035f,.071f,.125f,.75f));
            logo(72,129,125);
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
                label(L"Не удалось установить приложение",44,274,552,36,
                    regular.Get(),D2D1::ColorF(1,.65f,.56f));
                button(D2D1::RectF(430,327,602,377),L"Повторить");
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
            if(!factory || FAILED(factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(W,H)),&target))) {
                EndPaint(hwnd,&ps);return;
            }
            target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush);
        }
        target->BeginDraw();
        background();header();content();
        HRESULT hr=target->EndDraw();
        if(hr==D2DERR_RECREATE_TARGET){brush.Reset();target.Reset();}
        // The approved embedded icon, rather than Inno Setup's stock setup artwork.
        if(icon) DrawIconEx(ps.hdc,17,14,icon,32,32,0,nullptr,DI_NORMAL);
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
    void installationThread() {
        wchar_t dir[MAX_PATH]{};
        if(!GetTempPathW(MAX_PATH,dir)) {result=1;PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);return;}
        wchar_t path[MAX_PATH]{};
        if(!GetTempFileNameW(dir,L"SHS",0,path)) {result=1;PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);return;}
        HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(PAYLOAD_ID),RT_RCDATA);
        if(!resource) {DeleteFileW(path);result=1;PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);return;}
        HGLOBAL data=LoadResource(instance,resource);
        DWORD bytes=SizeofResource(instance,resource);
        void* ptr=LockResource(data);
        HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            result=1;PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);return;
        }
        DWORD wrote=0;
        BOOL stored=WriteFile(file,ptr,bytes,&wrote,nullptr);
        CloseHandle(file);
        if(!stored||wrote!=bytes) {
            DeleteFileW(path);result=1;PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);return;
        }
        progress=31;PostMessageW(hwnd,WM_APP+11,0,0);
        std::wstring command=L"\""+std::wstring(path)+L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        BOOL created=CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child);
        DWORD exitCode=1;
        if(created) {
            CloseHandle(child.hThread);
            // Smooth but honest progress: indeterminate until actual installer exits.
            progress=55;PostMessageW(hwnd,WM_APP+11,0,0);
            WaitForSingleObject(child.hProcess,INFINITE);
            GetExitCodeProcess(child.hProcess,&exitCode);
            CloseHandle(child.hProcess);
        }
        DeleteFileW(path);
        progress=exitCode==0?100:55;
        result=exitCode==0?0:1;
        PostMessageW(hwnd,WM_INSTALL_COMPLETE,0,0);
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
        if(!inside(p,primaryRect(step)))return;
        switch(step) {
            case Welcome:startInstallation();break;
            case Installing:if(result.load()==1){working=false;startInstallation();}break;
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
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};s->nextClick(p);return 0;
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
    if (commandLine && wcsstr(commandLine,L"--self-test")) {
        HRSRC payload=FindResourceW(instance,MAKEINTRESOURCEW(PAYLOAD_ID),RT_RCDATA);
        if(!payload||SizeofResource(instance,payload)<1024)return 8;
        HGLOBAL bytes=LoadResource(instance,payload);
        const BYTE* ptr=static_cast<const BYTE*>(LockResource(bytes));
        return (ptr && ptr[0]=='M' && ptr[1]=='Z')?0:9;
    }
    HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(co))return 2;
    Installer setup;setup.instance=instance;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,setup.factory.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(setup.fonts.GetAddressOf()));
    if(!setup.factory||!setup.fonts){CoUninitialize();return 3;}
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18,L"ru-ru",&setup.regular);
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,17,L"ru-ru",&setup.medium);
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"ru-ru",&setup.smallText);
    setup.fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,30,L"ru-ru",&setup.headline);
    setup.icon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance=instance;wc.lpszClassName=L"ScreenshotPremiumSetup";
    wc.lpfnWndProc=procedure;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=setup.icon;
    RegisterClassExW(&wc);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int x=work.left+(work.right-work.left-W)/2;
    int y=work.top+(work.bottom-work.top-H)/2;
    HWND hwnd=CreateWindowExW(WS_EX_APPWINDOW,L"ScreenshotPremiumSetup",
        L"Screenshot",WS_POPUP,x,y,W,H,nullptr,nullptr,instance,&setup);
    if(!hwnd){CoUninitialize();return 4;}
    ShowWindow(hwnd,show==SW_HIDE?SW_SHOWNORMAL:show);
    UpdateWindow(hwnd);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    // Do not free Installer before the background worker exits.
    while(setup.working.load())Sleep(40);
    setup.brush.Reset();setup.target.Reset();
    setup.icon=nullptr;
    setup.regular.Reset();setup.medium.Reset();setup.smallText.Reset();setup.headline.Reset();
    setup.fonts.Reset();setup.factory.Reset();
    CoUninitialize();
    return 0;
}
