#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "settings.h"
#include "connect_ui.h"
#include "settings_backdrop.h"
#include "bundled_font.h"
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>
#include <algorithm>
#include <string>
#include <memory>
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
namespace {
constexpr int WIDTH=460, HEIGHT=636;
constexpr UINT_PTR SHEET_TIMER=3;
constexpr UINT_PTR RENDER_RETRY_TIMER=2;
constexpr wchar_t CONFIG[]=L"Software\\SanderStripa\\Screenshot";
bool readDword(const wchar_t* name,DWORD& value) {
    DWORD size=sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,CONFIG,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS;
}
bool writeDword(const wchar_t* name,DWORD value) {
    HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,CONFIG,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    bool ok=RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value))==ERROR_SUCCESS;
    RegCloseKey(key);return ok;
}
bool readPosition(POINT& point) {
    DWORD size=sizeof(point);
    return RegGetValueW(HKEY_CURRENT_USER,CONFIG,L"WindowPosition",RRF_RT_REG_BINARY,nullptr,&point,&size)==ERROR_SUCCESS&&size==sizeof(point);
}
POINT visiblePosition(POINT point,int width,int height) {
    MONITORINFO monitor{sizeof(monitor)};
    if(GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor)) {
        const auto& work=monitor.rcWork;
        point.x=std::clamp(point.x,work.left,std::max(work.left,work.right-width));
        point.y=std::clamp(point.y,work.top,std::max(work.top,work.bottom-height));
    }
    return point;
}
std::wstring shortcutName(UINT vk,UINT mod) {
    std::wstring name;
    if(vk==VK_SNAPSHOT)name=L"Print Screen";
    else if(vk>=VK_F1&&vk<=VK_F24)name=L"F"+std::to_wstring(vk-VK_F1+1);
    else {
        wchar_t text[100]{};
        UINT scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);
        if(vk==VK_INSERT||vk==VK_DELETE||vk==VK_HOME||vk==VK_END||vk==VK_PRIOR||vk==VK_NEXT)scan|=0x100;
        GetKeyNameTextW(static_cast<LONG>(scan<<16),text,100);name=text;
    }
    if(mod&MOD_WIN)name=L"Win + "+name;
    if(mod&MOD_SHIFT)name=L"Shift + "+name;
    if(mod&MOD_ALT)name=L"Alt + "+name;
    if(mod&MOD_CONTROL)name=L"Ctrl + "+name;
    return name;
}
struct Settings {
    HINSTANCE instance{};HWND hwnd{};float scale=1;
    UINT vk=VK_SNAPSHOT,mod=0,savedVK=VK_SNAPSHOT,savedMod=0;
    bool light=false,listening=false,confirmation=false,preview=false,english=false;
    bool languageOpen=false,available=false;
    float sheetProgress=0;ULONGLONG sheetTick=0;
    SettingsBackdrop backdrop;
    std::wstring version;
    bool keyboardFocus=false,positionReady=false;
    void savePosition() {
        if(preview||!positionReady||!hwnd||IsIconic(hwnd))return;
        RECT rect{};if(!GetWindowRect(hwnd,&rect))return;
        POINT point{rect.left,rect.top};HKEY key{};
        if(RegCreateKeyExW(HKEY_CURRENT_USER,CONFIG,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
            RegSetValueExW(key,L"WindowPosition",0,REG_BINARY,reinterpret_cast<const BYTE*>(&point),sizeof(point));
            RegCloseKey(key);
        }
    }
    unsigned renderFailures=0;
    DWORD themeMode=2;int hovered=-1,pressed=-1,focused=-1;
    const wchar_t* tr(const wchar_t* ru,const wchar_t* en) const {return english?en:ru;}
    std::wstring status,notice;
    std::function<bool(UINT,UINT)> apply;
    ComPtr<ID2D1Factory> graphics;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> fonts;
    ComPtr<FontLoader> fontLoader;
    ComPtr<IDWriteFontCollection> collection;
    ComPtr<IDWriteTextFormat> title,section,body,hintFont;
    std::wstring fontPath;
    HICON icon{}, smallIcon{};
    ~Settings() {
        title.Reset();section.Reset();body.Reset();hintFont.Reset();collection.Reset();
        if(fontLoader&&fonts)fonts->UnregisterFontCollectionLoader(fontLoader.Get());
        fontLoader.Reset();fonts.Reset();
        if(!fontPath.empty())DeleteFileW(fontPath.c_str());
        if(icon)DestroyIcon(icon);
        if(smallIcon)DestroyIcon(smallIcon);
    }
    D2D1_COLOR_F color(UINT rgb) {return D2D1::ColorF(rgb);}
    UINT ink() const {return light?0x242628:0xeeeeef;}
    UINT muted() const {return light?0x73767b:0x96979d;}
    UINT border() const {return light?0xe6e7e9:0x35363b;}
    UINT surface() const {return light?0xfcfcfd:0x202124;}
    UINT field() const {return light?0xf3f4f5:0x28292d;}
    UINT background() const {return light?0xffffff:0x08090b;}
    void text(const std::wstring& value,D2D1_RECT_F rect,IDWriteTextFormat* font,
        UINT rgb,float spacing=0,bool center=false) {
        ComPtr<IDWriteTextLayout> layout;
        if(FAILED(fonts->CreateTextLayout(value.c_str(),static_cast<UINT32>(value.size()),font,
            rect.right-rect.left,rect.bottom-rect.top,&layout)))return;
        if(center){layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);}
        if(spacing) {
            ComPtr<IDWriteTextLayout1> expanded;
            if(SUCCEEDED(layout.As(&expanded)))expanded->SetCharacterSpacing(spacing,0,0,DWRITE_TEXT_RANGE{0,static_cast<UINT32>(value.size())});
        }
        brush->SetColor(color(rgb));
        target->DrawTextLayout(D2D1::Point2F(rect.left,rect.top),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void round(D2D1_RECT_F rect,float radius,UINT rgb,bool fill=true) {
        brush->SetColor(color(rgb));auto shape=D2D1::RoundedRect(rect,radius,radius);
        if(fill)target->FillRoundedRectangle(shape,brush.Get());else target->DrawRoundedRectangle(shape,brush.Get(),1);
    }
    void line(float y) {
        brush->SetColor(color(border()));target->DrawLine(D2D1::Point2F(28,y),D2D1::Point2F(492,y),brush.Get(),1);
    }
    void button(D2D1_RECT_F rect,const wchar_t* value,bool primary=false,int id=-1) {
        const UINT surface=primary?ink():(light?0xfcfcfd:0x111215);
        round(rect,8,surface);
        if(!primary)round(rect,8,border(),false);
        if(id>=0)ConnectUI::feedback(target.Get(),brush.Get(),rect,color(ink()),hovered==id,pressed==id,keyboardFocus&&focused==id);
        text(value,rect,body.Get(),primary?background():ink(),0,true);
    }
    void theme() {
        BOOL dark=!light;DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        COLORREF outline=0xfffffffe; // Same custom border policy as Torrent.
        DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&outline,sizeof(outline));
        backdrop.reset();InvalidateRect(hwnd,nullptr,FALSE);
    }
    void renderFailed(HDC dc) {
        brush.Reset();target.Reset();
        // Never leave an unpainted system-gray client area after a drawing failure.
        RECT rect{};GetClientRect(hwnd,&rect);
        HBRUSH background=CreateSolidBrush(light?RGB(247,247,245):RGB(12,12,13));
        FillRect(dc,&rect,background);DeleteObject(background);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,light?RGB(23,26,29):RGB(230,231,233));
        const wchar_t* message=tr(L"Не удалось отрисовать настройки. Закройте окно и откройте его снова.",
            L"Could not draw settings. Close this window and open it again.");
        InflateRect(&rect,-28,-28);
        DrawTextW(dc,message,-1,&rect,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        if(++renderFailures<=3)SetTimer(hwnd,RENDER_RETRY_TIMER,250,nullptr);
    }
    void paint() {
        PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);
        if(!target) {
            RECT rect{};GetClientRect(hwnd,&rect);
            // This small, static window does not need a GPU swap chain. An opaque
            // software target also works with remote sessions and problematic drivers.
            auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
            HRESULT result=graphics->CreateHwndRenderTarget(properties,
                D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(rect.right,rect.bottom)),&target);
            if(SUCCEEDED(result))result=target->CreateSolidColorBrush(color(ink()),&brush);
            if(FAILED(result)){renderFailed(ps.hdc);EndPaint(hwnd,&ps);return;}
        }
        target->SetDpi(96*scale,96*scale);target->BeginDraw();
        target->Clear(color(background()));
        text(L"SCREENSHOT.",D2D1::RectF(28,20,340,49),title.Get(),ink(),1.6f);
        for(int id:{10,11}) {
            float cx=id==10?390.f:430.f;
            if(hovered==id||pressed==id)round(D2D1::RectF(cx-16,16,cx+16,48),16,id==11?(light?0xf6d1d9:0x54242f):(light?0xededef:0x202125));
            brush->SetColor(color(ink()));
            if(id==10)target->DrawLine(D2D1::Point2F(cx-6,32),D2D1::Point2F(cx+6,32),brush.Get(),1.25f);
            else {target->DrawLine(D2D1::Point2F(cx-5,27),D2D1::Point2F(cx+5,37),brush.Get(),1.25f);target->DrawLine(D2D1::Point2F(cx+5,27),D2D1::Point2F(cx-5,37),brush.Get(),1.25f);}
            ConnectUI::feedback(target.Get(),brush.Get(),D2D1::RectF(cx-16,16,cx+16,48),color(ink()),false,false,keyboardFocus&&focused==id);
        }
        text(tr(L"Горячая клавиша",L"Keyboard shortcut"),D2D1::RectF(28,80,432,103),section.Get(),ink());
        round(D2D1::RectF(28,110,432,146),8,field());round(D2D1::RectF(28,110,432,146),8,listening?muted():border(),false);
        ConnectUI::feedback(target.Get(),brush.Get(),D2D1::RectF(28,110,432,146),color(ink()),hovered==0,pressed==0,keyboardFocus&&focused==0);
        // The field always describes the committed shortcut, never a pending candidate.
        text(listening?tr(L"Нажмите сочетание…",L"Press a shortcut…"):shortcutName(savedVK,savedMod),D2D1::RectF(40,110,420,146),body.Get(),ink(),0,true);
        text(status.empty()?(listening?tr(L"Esc — отмена",L"Esc — cancel"):tr(L"Нажмите поле, затем нужную клавишу или сочетание",L"Click the field, then press a key or shortcut")):status,D2D1::RectF(28,155,432,195),hintFont.Get(),muted());
        text(tr(L"Папка скриншотов",L"Screenshot folder"),D2D1::RectF(28,201,432,224),section.Get(),ink());
        button(D2D1::RectF(28,232,208,268),tr(L"Открыть папку",L"Open folder"),false,8);
        text(tr(L"Внешний вид",L"Appearance"),D2D1::RectF(28,292,432,315),section.Get(),ink());
        text(tr(L"Тема",L"Theme"),D2D1::RectF(28,326,432,348),body.Get(),ink());
        round(D2D1::RectF(28,355,432,395),8,field());round(D2D1::RectF(28,355,432,395),8,border(),false);
        for(int id=3;id<=5;++id) {
            float left=28+(id-3)*(404.f/3);auto r=D2D1::RectF(left+2,357,left+404.f/3-2,393);
            if(themeMode==DWORD(id-3)){round(r,6,surface());round(r,6,border(),false);}
            else if(hovered==id)round(r,6,light?0xededef:0x202125);
            text(id==3?tr(L"Системная",L"System"):id==4?tr(L"Светлая",L"Light"):tr(L"Тёмная",L"Dark"),r,hintFont.Get(),ink(),0,true);
            if(keyboardFocus&&focused==id)ConnectUI::feedback(target.Get(),brush.Get(),r,color(muted()),false,false,true);
        }
        text(tr(L"Язык интерфейса",L"Interface language"),D2D1::RectF(28,421,285,447),body.Get(),ink());
        auto lang=D2D1::RectF(300,415,432,451);round(lang,7,field());round(lang,7,border(),false);
        text(english?L"English":L"Русский",D2D1::RectF(312,415,410,451),body.Get(),ink(),0,true);
        brush->SetColor(color(muted()));target->DrawLine(D2D1::Point2F(414,431),D2D1::Point2F(418,435),brush.Get(),1.25f);target->DrawLine(D2D1::Point2F(418,435),D2D1::Point2F(422,431),brush.Get(),1.25f);
        ConnectUI::feedback(target.Get(),brush.Get(),lang,color(ink()),hovered==6,false,keyboardFocus&&focused==6);
        text(tr(L"О Screenshot",L"About Screenshot"),D2D1::RectF(28,478,432,501),section.Get(),ink());
        text(tr(L"Версия Screenshot",L"Screenshot version"),D2D1::RectF(28,515,280,539),body.Get(),ink());
        text(version,D2D1::RectF(340,515,432,539),body.Get(),ink(),0,true);
        text(tr(L"Разработчик",L"Developer"),D2D1::RectF(28,547,280,571),body.Get(),ink());
        text(L"Sander Stripa",D2D1::RectF(300,547,432,571),body.Get(),muted(),0,true);
        button(D2D1::RectF(320,580,432,612),tr(L"Обновить",L"Update"),false,9);
        text(L"by connect.",D2D1::RectF(165,611,295,633),hintFont.Get(),hovered==12?(light?0x55585d:0xb6b7bd):muted(),0,true);
        if(languageOpen) {
            round(D2D1::RectF(298,454,434,532),8,surface());round(D2D1::RectF(298,454,434,532),8,border(),false);
            for(int id:{13,14}) {auto r=D2D1::RectF(302,id==13?458.f:493.f,430,id==13?493.f:528.f);if(hovered==id||focused==id)round(r,6,field());text(id==13?L"Русский":L"English",r,body.Get(),ink(),0,true);}
        }
        if(confirmation||sheetProgress>0) {
            backdrop.draw(target.Get(),WIDTH,HEIGHT,!light);
            target->SetTransform(D2D1::Matrix3x2F::Translation(0,-340*(1-sheetProgress)));
            for(int i=20;i>0;--i){brush->SetColor(D2D1::ColorF(0,light?.012f:.02f));target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(-i*.5f,0,WIDTH+i*.5f,314.f+i),14,14),brush.Get());}
            round(D2D1::RectF(0,-20,WIDTH,314),14,surface());
            text(L"Print Screen",D2D1::RectF(28,24,432,54),title.Get(),ink());
            text(tr(L"Заменить системное действие клавиши?",L"Replace the Windows key action?"),D2D1::RectF(28,76,432,119),section.Get(),ink());
            text(tr(L"Print Screen будет открывать Screenshot вместо стандартного инструмента Windows.",L"Print Screen will open Screenshot instead of the Windows capture tool."),D2D1::RectF(28,127,432,178),body.Get(),ink());
            text(tr(L"Windows может потребовать выхода из сеанса. Если появится системный запрос, подтвердите изменение в нём.",L"Windows may require signing out. If Windows requests permission, confirm it in the system dialog."),D2D1::RectF(28,187,432,240),hintFont.Get(),muted());
            button(D2D1::RectF(154,258,264,294),tr(L"Отмена",L"Cancel"),false,15);
            button(D2D1::RectF(276,258,432,294),tr(L"Да, заменить",L"Yes, replace"),true,1);
            target->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        if(!notice.empty()) {
            round(D2D1::RectF(20,72,440,304),8,surface());
            text(notice,D2D1::RectF(40,100,420,232),body.Get(),ink());
            button(D2D1::RectF(310,248,420,284),L"OK",true,1);
        }
        HRESULT result=target->EndDraw();
        if(FAILED(result))renderFailed(ps.hdc);
        else {renderFailures=0;KillTimer(hwnd,RENDER_RETRY_TIMER);}
        EndPaint(hwnd,&ps);
    }
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
        return create(18,DWRITE_FONT_WEIGHT_MEDIUM,title)&&create(12,DWRITE_FONT_WEIGHT_SEMI_BOLD,section)
            &&create(13,DWRITE_FONT_WEIGHT_NORMAL,body)&&create(11,DWRITE_FONT_WEIGHT_NORMAL,hintFont);
    }
    void sheet(bool open) {
        languageOpen=false;listening=false;
        if(open) {backdrop.reset();UpdateWindow(hwnd);backdrop.capture(hwnd,!light);}
        confirmation=open;sheetTick=GetTickCount64();
        BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);
        if(animate)SetTimer(hwnd,SHEET_TIMER,16,nullptr);else sheetProgress=open?1.f:0.f;
        focused=open?15:0;InvalidateRect(hwnd,nullptr,FALSE);
    }
    void cancel() {vk=savedVK;mod=savedMod;sheet(false);}
    void save(bool confirmed=false) {
        listening=false;
        if(preview){vk=savedVK;mod=savedMod;status=tr(L"Предпросмотр интерфейса",L"Interface preview");return;}
        DWORD snipping=1,size=sizeof(snipping);bool existed=false,changed=false;
        if(vk==VK_SNAPSHOT&&mod==0) {
            existed=RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Keyboard",L"PrintScreenKeyForSnippingEnabled",RRF_RT_REG_DWORD,nullptr,&snipping,&size)==ERROR_SUCCESS;
            if(!confirmed&&(snipping!=0||!available||vk!=savedVK||mod!=savedMod)){sheet(true);return;}
        }
        auto restoreWindows=[&] {
            if(!changed)return;HKEY k{};
            if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Keyboard",0,KEY_SET_VALUE,&k)==ERROR_SUCCESS){if(existed)RegSetValueExW(k,L"PrintScreenKeyForSnippingEnabled",0,REG_DWORD,reinterpret_cast<BYTE*>(&snipping),sizeof(snipping));else RegDeleteValueW(k,L"PrintScreenKeyForSnippingEnabled");RegCloseKey(k);}
            SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"Control Panel\\Keyboard"),SMTO_ABORTIFHUNG,300,nullptr);
        };
        bool ok=true;
        if(confirmed) {
            HKEY k{};DWORD zero=0;
            ok=RegCreateKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Keyboard",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS;
            if(ok){ok=RegSetValueExW(k,L"PrintScreenKeyForSnippingEnabled",0,REG_DWORD,reinterpret_cast<BYTE*>(&zero),sizeof(zero))==ERROR_SUCCESS;RegCloseKey(k);}
            changed=ok;
            if(ok)SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"Control Panel\\Keyboard"),SMTO_ABORTIFHUNG,300,nullptr);
        }
        if(!ok)status=tr(L"Windows не разрешила изменить Print Screen.",L"Windows could not change Print Screen.");
        else if(!apply(vk,mod)) {
            restoreWindows();ok=false;
            status=confirmed?tr(L"Windows пока не разрешила перехват. Прежняя клавиша сохранена; может потребоваться выход из сеанса.",L"Windows has not allowed capture. Previous shortcut retained; signing out may be required."):tr(L"Это сочетание занято. Прежняя клавиша сохранена.",L"Shortcut is in use. Previous shortcut retained.");
        } else if(!writeDword(L"HotkeyVK",vk)||!writeDword(L"HotkeyModifiers",mod)) {
            writeDword(L"HotkeyVK",savedVK);writeDword(L"HotkeyModifiers",savedMod);available=apply(savedVK,savedMod);restoreWindows();ok=false;
            status=tr(L"Не удалось сохранить настройки. Прежняя клавиша восстановлена.",L"Could not save settings. Previous shortcut restored.");
        }
        if(ok){savedVK=vk;savedMod=mod;available=true;status.clear();}
        else {vk=savedVK;mod=savedMod;}
        if(confirmation)sheet(false);InvalidateRect(hwnd,nullptr,FALSE);
    }
    void key(UINT key) {
        if(!listening)return;
        if(key==VK_ESCAPE){listening=false;status.clear();InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||key==VK_LWIN||key==VK_RWIN)return;
        UINT modifiers=0;
        if(GetKeyState(VK_CONTROL)&0x8000)modifiers|=MOD_CONTROL;
        if(GetKeyState(VK_SHIFT)&0x8000)modifiers|=MOD_SHIFT;
        if(GetKeyState(VK_MENU)&0x8000)modifiers|=MOD_ALT;
        if((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)modifiers|=MOD_WIN;
        bool special=(key>=VK_F1&&key<=VK_F24)||key==VK_SNAPSHOT||key==VK_PAUSE||key==VK_SCROLL||key==VK_INSERT||key==VK_HOME;
        if(!special&&(!(modifiers&(MOD_CONTROL|MOD_ALT|MOD_WIN))||key<VK_SPACE)){MessageBeep(MB_ICONWARNING);return;}
        vk=key;mod=modifiers;listening=false;status.clear();save();InvalidateRect(hwnd,nullptr,FALSE);
    }
    int hit(float x,float y) const {
        if(!notice.empty())return y>=248&&y<=284&&x>=310&&x<=420?1:-1;
        if(confirmation||sheetProgress>0) {
            if(!confirmation||sheetProgress<.99f)return -1;
            if(y>=258&&y<=294){if(x>=154&&x<=264)return 15;if(x>=276&&x<=432)return 1;}return -1;
        }
        if(y>=16&&y<=48){if(x>=414&&x<=446)return 11;if(x>=374&&x<=406)return 10;}
        if(languageOpen&&x>=298&&x<=434){if(y>=458&&y<493)return 13;if(y>=493&&y<=528)return 14;}
        if(x>=28&&x<=432&&y>=110&&y<=146)return 0;
        if(x>=28&&x<=208&&y>=232&&y<=268)return 8;
        if(x>=28&&x<432&&y>=355&&y<=395)return 3+int((x-28)/(404.f/3));
        if(x>=300&&x<=432&&y>=415&&y<=451)return 6;
        if(x>=320&&x<=432&&y>=580&&y<=612)return 9;
        if(x>=165&&x<=295&&y>=613&&y<=633)return 12;
        return -1;
    }
    void activate(int id) {
        if(!notice.empty()){if(id==1)notice.clear();InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(confirmation){if(id==15)cancel();else if(id==1)save(true);return;}
        if(sheetProgress>0)return;
        if(id==11){DestroyWindow(hwnd);return;}
        if(id==10){ShowWindow(hwnd,SW_MINIMIZE);return;}
        focused=id;listening=false;
        if(id>=3&&id<=5) {
            languageOpen=false;DWORD next=id-3,old=themeMode;bool nextLight=next==0?ConnectUI::systemLight():next==1;
            if(preview||(writeDword(L"ThemeMode",next)&&writeDword(L"ThemeLight",nextLight?1:0))){themeMode=next;light=nextLight;status.clear();theme();}
            else {writeDword(L"ThemeMode",old);writeDword(L"ThemeLight",light?1:0);status=tr(L"Не удалось сохранить тему.",L"Could not save theme.");}
        } else if(id==6){languageOpen=!languageOpen;focused=english?14:13;}
        else if(id==13||id==14) {
            if(preview||writeDword(L"LanguageEN",id==14?1:0)){english=id==14;status.clear();}
            else status=tr(L"Не удалось сохранить язык.",L"Could not save language.");
            languageOpen=false;focused=6;
        } else {
            languageOpen=false;
            if(id==0){listening=true;status.clear();}
            else if(id==8) {
                wchar_t profile[32768]{};
                if(GetEnvironmentVariableW(L"USERPROFILE",profile,32768)) {
                    std::wstring folder=std::wstring(profile)+L"\\Pictures\\Screenshot";
                    SHCreateDirectoryExW(hwnd,folder.c_str(),nullptr);
                    ShellExecuteW(hwnd,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                }
            } else if(id==9||id==12)ShellExecuteW(hwnd,L"open",id==9?L"https://github.com/sanderstripa/screenshot/releases":L"https://connect.sanderstripa.com/",nullptr,nullptr,SW_SHOWNORMAL);
        }
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void click(float x,float y) {keyboardFocus=false;activate(hit(x,y));}
    void tab(bool reverse) {
        listening=false;keyboardFocus=true;
        const std::vector<int> normal={0,8,3,4,5,6,9,12,10,11},consent={15,1},modal={1},languages={13,14};
        const auto& list=!notice.empty()?modal:(confirmation?consent:(languageOpen?languages:normal));int count=int(list.size()),index=-1;
        for(int i=0;i<count;i++)if(list[i]==focused)index=i;
        focused=list[(index+(reverse?count-1:1)+count)%count];InvalidateRect(hwnd,nullptr,FALSE);
    }

};
std::unique_ptr<Settings> settings;
LRESULT CALLBACK settingsProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto s=reinterpret_cast<Settings*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){s=static_cast<Settings*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);s->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(hwnd,message,wp,lp);
    switch(message) {
    case WM_SYSCOMMAND:if((wp&0xfff0)==SC_MAXIMIZE)return 0;break;
    case WM_NCLBUTTONDBLCLK:if(wp==HTCAPTION)return 0;break;
    case WM_NCCALCSIZE:if(wp)return 0;break;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);if(!s->confirmation&&s->sheetProgress==0&&p.y/s->scale<62&&p.x/s->scale<365)return HTCAPTION;return HTCLIENT;break;}
    case WM_SETTINGCHANGE:if(s->themeMode==0){s->light=ConnectUI::systemLight();s->theme();}return 0;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){POINT p;GetCursorPos(&p);ScreenToClient(hwnd,&p);SetCursor(LoadCursorW(nullptr,s->hit(p.x/s->scale,p.y/s->scale)==12?IDC_HAND:IDC_ARROW));return TRUE;}break;
    case WM_MOUSEMOVE:{int hit=s->hit(GET_X_LPARAM(lp)/s->scale,GET_Y_LPARAM(lp)/s->scale);if(hit!=s->hovered){s->hovered=hit;InvalidateRect(hwnd,nullptr,FALSE);}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);return 0;}
    case WM_MOUSELEAVE:s->hovered=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN:s->pressed=s->hit(GET_X_LPARAM(lp)/s->scale,GET_Y_LPARAM(lp)/s->scale);SetFocus(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:s->paint();return 0;
    case WM_TIMER:if(wp==SHEET_TIMER){auto now=GetTickCount64();float step=float(now-s->sheetTick)/220.f;s->sheetTick=now;s->sheetProgress=std::clamp(s->sheetProgress+(s->confirmation?step:-step),0.f,1.f);if(s->sheetProgress==(s->confirmation?1.f:0.f))KillTimer(hwnd,SHEET_TIMER);InvalidateRect(hwnd,nullptr,FALSE);return 0;}if(wp==RENDER_RETRY_TIMER){KillTimer(hwnd,RENDER_RETRY_TIMER);InvalidateRect(hwnd,nullptr,FALSE);return 0;}break;
    case WM_DISPLAYCHANGE:s->backdrop.reset();s->brush.Reset();s->target.Reset();s->renderFailures=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_SIZE:s->backdrop.reset();s->brush.Reset();s->target.Reset();InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_WINDOWPOSCHANGED:s->savePosition();break;
    case WM_QUERYENDSESSION:s->savePosition();return TRUE;
    case WM_DPICHANGED:{s->scale=HIWORD(wp)/96.0f;auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,int(WIDTH*s->scale),int(HEIGHT*s->scale),SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    case WM_LBUTTONUP:s->pressed=-1;s->click(GET_X_LPARAM(lp)/s->scale,GET_Y_LPARAM(lp)/s->scale);return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:
        if(message==WM_SYSKEYDOWN&&wp==VK_F4)return DefWindowProcW(hwnd,message,wp,lp);
        if(wp==VK_TAB){s->tab((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
        if(s->listening)s->key(static_cast<UINT>(wp));
        else if(wp==VK_ESCAPE){if(!s->notice.empty()){s->notice.clear();InvalidateRect(hwnd,nullptr,FALSE);}else if(s->confirmation){s->cancel();}else if(s->languageOpen){s->languageOpen=false;InvalidateRect(hwnd,nullptr,FALSE);}else DestroyWindow(hwnd);}
        else if(wp==VK_RETURN||wp==VK_SPACE){if(s->focused>=0)s->activate(s->focused);}
        return 0;
    case WM_KEYUP:case WM_SYSKEYUP:if(s->listening&&wp==VK_SNAPSHOT)s->key(VK_SNAPSHOT);return 0;
    case WM_DESTROY:KillTimer(hwnd,SHEET_TIMER);s->sheetProgress=0;KillTimer(hwnd,RENDER_RETRY_TIMER);s->savePosition();s->positionReady=false;s->hwnd=nullptr;s->backdrop.reset();s->brush.Reset();s->target.Reset();s->listening=false;s->confirmation=false;return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
void showScreenshotSettings(HINSTANCE instance,UINT vk,UINT modifiers,bool shortcutAvailable,
    std::function<bool(UINT,UINT)> apply,int previewTheme) {
    if(settings&&settings->hwnd){ShowWindow(settings->hwnd,SW_RESTORE);SetForegroundWindow(settings->hwnd);return;}
    if(!settings) {
        settings=std::make_unique<Settings>();settings->instance=instance;
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,settings->graphics.GetAddressOf()))||!settings->initializeFonts()) {
            settings.reset();MessageBoxW(nullptr,L"Не удалось открыть настройки Screenshot.",L"Screenshot",MB_OK|MB_ICONERROR);return;
        }
        settings->icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,256,256,0));
        const UINT iconDpi=GetDpiForSystem();
        settings->smallIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,
            GetSystemMetricsForDpi(SM_CXSMICON,iconDpi),GetSystemMetricsForDpi(SM_CYSMICON,iconDpi),0));
        WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=settingsProc;wc.lpszClassName=L"ScreenshotSettings";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=settings->icon;wc.hIconSm=settings->smallIcon;RegisterClassExW(&wc);
    }
    auto& s=*settings;s.apply=std::move(apply);s.vk=s.savedVK=vk;s.mod=s.savedMod=modifiers;
    DWORD value=0;s.available=shortcutAvailable;
    wchar_t exe[32768]{};GetModuleFileNameW(instance,exe,32768);DWORD ignored=0,bytes=GetFileVersionInfoSizeW(exe,&ignored);std::vector<BYTE> info(bytes);VS_FIXEDFILEINFO* fixed=nullptr;UINT length=0;
    s.version=L"—";if(bytes&&GetFileVersionInfoW(exe,0,bytes,info.data())&&VerQueryValueW(info.data(),L"\\",reinterpret_cast<void**>(&fixed),&length)&&length>=sizeof(*fixed))s.version=std::to_wstring(HIWORD(fixed->dwProductVersionMS))+L"."+std::to_wstring(LOWORD(fixed->dwProductVersionMS))+L"."+std::to_wstring(HIWORD(fixed->dwProductVersionLS));
    value=0;readDword(L"ThemeLight",value);s.light=value!=0;
    s.themeMode=s.light?1:2;readDword(L"ThemeMode",s.themeMode);if(s.themeMode>2)s.themeMode=2;
    s.light=s.themeMode==0?ConnectUI::systemLight():s.themeMode==1;
    value=0;readDword(L"LanguageEN",value);s.english=value!=0;
    s.languageOpen=false;s.sheetProgress=0;s.confirmation=false;s.listening=false;s.keyboardFocus=false;s.focused=-1;s.hovered=-1;s.pressed=-1;
    s.preview=previewTheme>=0;if(s.preview)s.light=previewTheme!=0;
    s.status=shortcutAvailable?L"":s.tr(L"Горячая клавиша недоступна. Назначьте другую.",L"Shortcut unavailable. Assign another.");
    s.scale=GetDpiForSystem()/96.0f;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=int(WIDTH*s.scale),height=int(HEIGHT*s.scale);
    POINT position{work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2};
    if(!s.preview)readPosition(position);
    position=visiblePosition(position,width,height);s.positionReady=false;
    s.hwnd=CreateWindowExW(WS_EX_APPWINDOW,L"ScreenshotSettings",L"Screenshot",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,position.x,
        position.y,width,height,nullptr,nullptr,instance,&s);
    if(!s.hwnd)return;
    s.scale=GetDpiForWindow(s.hwnd)/96.0f;width=int(WIDTH*s.scale);height=int(HEIGHT*s.scale);
    position=visiblePosition(position,width,height);
    SetWindowPos(s.hwnd,nullptr,position.x,position.y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    s.positionReady=true;
    DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;DwmSetWindowAttribute(s.hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    MARGINS margins{0,0,0,0};DwmExtendFrameIntoClientArea(s.hwnd,&margins);s.theme();
    SetWindowPos(s.hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    ShowWindow(s.hwnd,SW_SHOWNORMAL);
    // A process launched hidden at login must still show its first settings window.
    if(!IsWindowVisible(s.hwnd))ShowWindow(s.hwnd,SW_SHOWNORMAL);
    SetForegroundWindow(s.hwnd);UpdateWindow(s.hwnd);
}
void closeScreenshotSettings() {
    if(settings&&settings->hwnd)DestroyWindow(settings->hwnd);
    settings.reset();
}
bool screenshotSettingsFocused() {
    return settings&&settings->hwnd&&GetForegroundWindow()==settings->hwnd;
}




void showScreenshotCaptureNotice(bool saved) {
    if(!settings||!settings->hwnd)return;
    settings->notice=saved?settings->tr(L"PNG сохранён, но буфер обмена занят. Повторите захват.",L"PNG saved, but the clipboard is busy. Try another capture."):
        settings->tr(L"Не удалось сохранить PNG в Pictures\\Screenshot. Проверьте доступ к папке.",L"Could not save PNG to Pictures\\Screenshot. Check folder permissions.");
    settings->focused=1;InvalidateRect(settings->hwnd,nullptr,FALSE);
}





