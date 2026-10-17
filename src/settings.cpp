#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "settings.h"
#include "connect_ui.h"
#include "approved_settings_icon.h"
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

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
namespace {
constexpr int WIDTH=520, HEIGHT=382;
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
    bool light=false,listening=false,confirmation=false,preview=false,appearance=false,english=false;
    bool keyboardFocus=false;
    DWORD themeMode=2;int hovered=-1,pressed=-1,focused=-1;
    const wchar_t* tr(const wchar_t* ru,const wchar_t* en) const {return english?en:ru;}
    std::wstring status,notice;
    std::function<bool(UINT,UINT)> apply;
    ComPtr<ID2D1Factory> graphics;
    ComPtr<ID2D1PathGeometry> settingsIcon;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> fonts;
    ComPtr<FontLoader> fontLoader;
    ComPtr<IDWriteFontCollection> collection;
    ComPtr<IDWriteTextFormat> title,section,body,hintFont;
    std::wstring fontPath;
    HICON icon{};
    ~Settings() {
        title.Reset();section.Reset();body.Reset();hintFont.Reset();collection.Reset();
        if(fontLoader&&fonts)fonts->UnregisterFontCollectionLoader(fontLoader.Get());
        fontLoader.Reset();fonts.Reset();
        if(!fontPath.empty())DeleteFileW(fontPath.c_str());
        if(icon)DestroyIcon(icon);
    }
    D2D1_COLOR_F color(UINT rgb) {return D2D1::ColorF(rgb);}
    UINT ink() const {return light?0x111111:0xF4F4F4;}
    UINT muted() const {return light?0x75797F:0x8B8E94;}
    UINT border() const {return light?0xD9DADC:0x333639;}
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
        // CONNECT UI: reuse the current Screenshot palette, without introducing new colors.
        const UINT surface=primary?ink():(light?0xF9F9F9:0x141414);
        round(rect,8,surface);
        if(!primary)round(rect,8,border(),false);
        if(id>=0)ConnectUI::feedback(target.Get(),brush.Get(),rect,color(ink()),hovered==id,pressed==id,keyboardFocus&&focused==id);
        text(value,rect,body.Get(),primary?(light?0xF5F5F5:0x0A0A0A):ink(),0,true);
    }
    void theme() {
        BOOL dark=!light;DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        COLORREF outline=light?RGB(218,218,218):RGB(45,47,50);
        DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&outline,sizeof(outline));
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void paint() {
        PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);
        if(!target) {
            RECT rect{};GetClientRect(hwnd,&rect);
            if(FAILED(graphics->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(rect.right,rect.bottom)),&target))) {EndPaint(hwnd,&ps);return;}
            target->CreateSolidColorBrush(color(ink()),&brush);
        }
        target->SetDpi(96*scale,96*scale);target->BeginDraw();
        target->Clear(color(light?0xF5F5F5:0x0A0A0A));
        text(L"SCREENSHOT.",D2D1::RectF(28,27,400,58),title.Get(),ink(),1.8f);
        brush->SetColor(color(muted()));
        target->DrawLine(D2D1::Point2F(481,31),D2D1::Point2F(489,39),brush.Get(),1);
        target->DrawLine(D2D1::Point2F(489,31),D2D1::Point2F(481,39),brush.Get(),1);
        target->DrawLine(D2D1::Point2F(436,35),D2D1::Point2F(446,35),brush.Get(),1);
        ConnectUI::feedback(target.Get(),brush.Get(),D2D1::RectF(420,18,460,52),color(ink()),hovered==10,pressed==10,keyboardFocus&&focused==10);
        ConnectUI::feedback(target.Get(),brush.Get(),D2D1::RectF(464,18,504,52),color(ink()),hovered==11,pressed==11,keyboardFocus&&focused==11);
        line(80);
        text(tr(L"НАСТРОЙКИ",L"SETTINGS"),D2D1::RectF(28,98,296,128),title.Get(),ink(),1.8f);
        if(appearance) {
            text(tr(L"ТЕМА",L"THEME"),D2D1::RectF(28,148,492,170),section.Get(),ink(),1);
            button(D2D1::RectF(28,176,176,212),tr(L"Система",L"System"),themeMode==0,3);
            button(D2D1::RectF(184,176,332,212),tr(L"Светлая",L"Light"),themeMode==1,4);
            button(D2D1::RectF(340,176,488,212),tr(L"Тёмная",L"Dark"),themeMode==2,5);
            text(tr(L"ЯЗЫК",L"LANGUAGE"),D2D1::RectF(28,236,492,258),section.Get(),ink(),1);
            button(D2D1::RectF(28,264,176,300),L"RU",!english,6);
            button(D2D1::RectF(184,264,332,300),L"EN",english,7);
            line(314);button(D2D1::RectF(28,332,210,368),tr(L"Назад",L"Back"),false,8);
        } else if(confirmation) {
            text(L"PRINT SCREEN",D2D1::RectF(28,146,492,175),section.Get(),ink(),1);
            text(tr(L"Заменить системное действие клавиши?",L"Replace the Windows action?"),D2D1::RectF(28,186,492,221),body.Get(),ink());
            text(tr(L"Print Screen будет открывать Screenshot вместо «Ножниц». Windows может потребовать выхода из сеанса.",L"Print Screen will open Screenshot instead of Snipping Tool. Windows may require signing out."),
                D2D1::RectF(28,232,492,302),body.Get(),muted());
            line(314);
            button(D2D1::RectF(28,332,182,368),tr(L"Отмена",L"Cancel"),false,8);
            button(D2D1::RectF(316,332,492,368),tr(L"Да, заменить",L"Yes, replace"),true,1);
        } else {
            brush->SetColor(color(hovered==2||pressed==2?ink():muted()));
            drawApprovedSettingsIcon(target.Get(),brush.Get(),settingsIcon.Get(),462,104,24);
            if(keyboardFocus&&focused==2){brush->SetColor(color(ink()));target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(455,95,497,137),10,10),brush.Get(),2);}
            text(tr(L"ГОРЯЧАЯ КЛАВИША",L"KEYBOARD SHORTCUT"),D2D1::RectF(28,157,492,182),section.Get(),ink(),1);
            round(D2D1::RectF(28,190,492,226),8,light?0xF9F9F9:0x0A0A0A);
            round(D2D1::RectF(28,190,492,226),8,listening?muted():border(),false);
            ConnectUI::feedback(target.Get(),brush.Get(),D2D1::RectF(28,190,492,226),color(ink()),hovered==0,pressed==0,keyboardFocus&&focused==0);
            text(listening?tr(L"Нажмите сочетание…",L"Press a shortcut…"):shortcutName(vk,mod),
                D2D1::RectF(42,190,478,226),body.Get(),ink(),0,true);
            text(status.empty()?(listening?tr(L"Esc — отмена",L"Esc — cancel"):tr(L"Нажмите поле, затем нужную клавишу или сочетание.",L"Click the field, then press a key or shortcut.")):status,
                D2D1::RectF(28,249,492,302),hintFont.Get(),muted());
            line(314);
            button(D2D1::RectF(28,332,210,368),tr(L"Открыть папку",L"Open folder"),false,8);
            button(D2D1::RectF(340,332,492,368),tr(L"Сохранить",L"Save"),true,1);
        }
        if(!notice.empty()) {
            round(D2D1::RectF(24,94,496,372),8,light?0xF5F5F5:0x0A0A0A);
            text(tr(L"УВЕДОМЛЕНИЕ",L"NOTICE"),D2D1::RectF(28,110,492,140),title.Get(),ink());
            text(L"!",D2D1::RectF(28,164,48,192),body.Get(),ink());
            text(notice,D2D1::RectF(56,164,492,302),body.Get(),muted());
            button(D2D1::RectF(340,332,492,368),L"OK",true,1);
        }
        HRESULT result=target->EndDraw();
        if(result==D2DERR_RECREATE_TARGET){brush.Reset();target.Reset();}
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
    void save(bool confirmed=false) {
        if(preview){status=tr(L"Предпросмотр настроек",L"Settings preview");return;}
        listening=false;
        if(vk==VK_SNAPSHOT&&mod==0&&!confirmed){confirmation=true;InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(confirmed) {
            HKEY key{};
            if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Keyboard",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) {
                status=tr(L"Windows не разрешила изменить Print Screen.",L"Windows could not change Print Screen.");confirmation=false;InvalidateRect(hwnd,nullptr,FALSE);return;
            }
            DWORD zero=0;
            LONG code=RegSetValueExW(key,L"PrintScreenKeyForSnippingEnabled",0,REG_DWORD,reinterpret_cast<BYTE*>(&zero),sizeof(zero));
            RegCloseKey(key);
            if(code!=ERROR_SUCCESS){status=tr(L"Не удалось изменить действие Print Screen.",L"Could not change the Print Screen action.");confirmation=false;InvalidateRect(hwnd,nullptr,FALSE);return;}
            SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"Control Panel\\Keyboard"),SMTO_ABORTIFHUNG,300,nullptr);
        }
        // Verify first, then commit both registry fields together. Keep old runtime shortcut on failure.
        bool active=apply(vk,mod);
        if(!active&&!confirmed)status=tr(L"Это сочетание занято. Выберите другое.",L"This shortcut is in use. Choose another.");
        else if(!writeDword(L"HotkeyVK",vk)||!writeDword(L"HotkeyModifiers",mod)) {
            writeDword(L"HotkeyVK",savedVK);writeDword(L"HotkeyModifiers",savedMod);
            if(active)apply(savedVK,savedMod);
            status=tr(L"Не удалось сохранить настройки. Прежняя клавиша сохранена.",L"Could not save settings. The previous shortcut is retained.");
        } else {
            savedVK=vk;savedMod=mod;
            status=active?tr(L"Горячая клавиша сохранена.",L"Shortcut saved.") : tr(L"Print Screen сохранена. Выйдите из сеанса Windows и войдите снова.",L"Print Screen saved. Sign out of Windows and back in.");
        }
        confirmation=false;InvalidateRect(hwnd,nullptr,FALSE);
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
        vk=key;mod=modifiers;listening=false;status.clear();InvalidateRect(hwnd,nullptr,FALSE);
    }
    int hit(float x,float y) const {
        if(y>=18&&y<=52){if(x>=464)return 11;if(x>=420&&x<=460)return 10;}
        if(!notice.empty())return y>=332&&y<=368&&x>=340&&x<=492?1:-1;
        if(appearance) {
            if(y>=176&&y<=212){if(x>=28&&x<=176)return 3;if(x>=184&&x<=332)return 4;if(x>=340&&x<=488)return 5;}
            if(y>=264&&y<=300){if(x>=28&&x<=176)return 6;if(x>=184&&x<=332)return 7;}
            if(y>=332&&y<=368&&x>=28&&x<=210)return 8;
        } else {
            if(!confirmation&&x>=458&&x<=494&&y>=98&&y<=134)return 2;
            if(!confirmation&&x>=28&&x<=492&&y>=190&&y<=226)return 0;
            if(y>=332&&y<=368){if(x>=28&&x<=210)return 8;if(x>=316&&x<=492)return 1;}
        }
        return -1;
    }
    void activate(int id) {
        if(id==11){DestroyWindow(hwnd);return;}
        if(id==10){ShowWindow(hwnd,SW_MINIMIZE);return;}
        focused=id;
        if(!notice.empty()){if(id==1)notice.clear();InvalidateRect(hwnd,nullptr,FALSE);return;}
        if(appearance) {
            if(id>=3&&id<=5) {
                DWORD next=id-3;
                bool nextLight=next==0?ConnectUI::systemLight():next==1;
                if(preview||(writeDword(L"ThemeMode",next)&&writeDword(L"ThemeLight",nextLight?1:0))) {
                    themeMode=next;light=nextLight;theme();
                } else status=tr(L"Не удалось сохранить тему.",L"Could not save the theme.");
            } else if(id==6||id==7) {
                if(preview||writeDword(L"LanguageEN",id==7?1:0)){english=id==7;status.clear();}
            } else if(id==8){appearance=false;focused=2;}
        } else if(confirmation) {
            if(id==8){confirmation=false;focused=0;}
            else if(id==1)save(true);
        } else if(id==2){appearance=true;listening=false;focused=3;}
        else if(id==0){listening=true;status.clear();}
        else if(id==1)save();
        else if(id==8) {
            wchar_t profile[32768]{};
            if(GetEnvironmentVariableW(L"USERPROFILE",profile,32768)) {
                std::wstring folder=std::wstring(profile)+L"\\Pictures\\Screenshot";
                SHCreateDirectoryExW(hwnd,folder.c_str(),nullptr);
                ShellExecuteW(hwnd,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            }
        }
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void click(float x,float y) {keyboardFocus=false;activate(hit(x,y));}
    void tab(bool reverse) {
        listening=false;keyboardFocus=true;
        const int normal[]={2,0,8,1,10,11}, prefs[]={3,4,5,6,7,8,10,11}, consent[]={8,1,10,11}, modal[]={1,10,11};
        auto list=!notice.empty()?modal:(appearance?prefs:(confirmation?consent:normal));int count=!notice.empty()?3:(appearance?8:(confirmation?4:6)),index=-1;
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
    case WM_NCCALCSIZE:if(wp)return 0;break;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);if(p.y/s->scale<70&&p.x/s->scale<400)return HTCAPTION;break;}
    case WM_SETTINGCHANGE:if(s->themeMode==0){s->light=ConnectUI::systemLight();s->theme();}return 0;
    case WM_MOUSEMOVE:{int hit=s->hit(GET_X_LPARAM(lp)/s->scale,GET_Y_LPARAM(lp)/s->scale);if(hit!=s->hovered){s->hovered=hit;InvalidateRect(hwnd,nullptr,FALSE);}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);return 0;}
    case WM_MOUSELEAVE:s->hovered=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN:s->pressed=s->hit(GET_X_LPARAM(lp)/s->scale,GET_Y_LPARAM(lp)/s->scale);SetFocus(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:s->paint();return 0;
    case WM_SIZE:s->brush.Reset();s->target.Reset();InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_DPICHANGED:{s->scale=HIWORD(wp)/96.0f;auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,int(WIDTH*s->scale),int(HEIGHT*s->scale),SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    case WM_LBUTTONUP:s->pressed=-1;s->click(GET_X_LPARAM(lp)/s->scale,GET_Y_LPARAM(lp)/s->scale);return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:
        if(message==WM_SYSKEYDOWN&&wp==VK_F4)return DefWindowProcW(hwnd,message,wp,lp);
        if(wp==VK_TAB){s->tab((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
        if(s->listening)s->key(static_cast<UINT>(wp));
        else if(wp==VK_ESCAPE){if(!s->notice.empty()){s->notice.clear();InvalidateRect(hwnd,nullptr,FALSE);}else if(s->confirmation){s->confirmation=false;InvalidateRect(hwnd,nullptr,FALSE);}else if(s->appearance){s->appearance=false;s->focused=2;InvalidateRect(hwnd,nullptr,FALSE);}else DestroyWindow(hwnd);}
        else if(wp==VK_RETURN||wp==VK_SPACE){if(s->focused>=0)s->activate(s->focused);else if(!s->appearance)s->save();}
        return 0;
    case WM_KEYUP:case WM_SYSKEYUP:if(s->listening&&wp==VK_SNAPSHOT)s->key(VK_SNAPSHOT);return 0;
    case WM_DESTROY:s->hwnd=nullptr;s->brush.Reset();s->target.Reset();s->listening=false;s->confirmation=false;return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
void showScreenshotSettings(HINSTANCE instance,UINT vk,UINT modifiers,bool shortcutAvailable,
    std::function<bool(UINT,UINT)> apply,int previewTheme) {
    if(settings&&settings->hwnd){ShowWindow(settings->hwnd,SW_RESTORE);SetForegroundWindow(settings->hwnd);return;}
    if(!settings) {
        settings=std::make_unique<Settings>();settings->instance=instance;
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,settings->graphics.GetAddressOf()))||FAILED(createApprovedSettingsIcon(settings->graphics.Get(),settings->settingsIcon.GetAddressOf()))||!settings->initializeFonts()) {
            settings.reset();MessageBoxW(nullptr,L"Не удалось открыть настройки Screenshot.",L"Screenshot",MB_OK|MB_ICONERROR);return;
        }
        settings->icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,0));
        WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=settingsProc;wc.lpszClassName=L"ScreenshotSettings";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=settings->icon;RegisterClassExW(&wc);
    }
    auto& s=*settings;s.apply=std::move(apply);s.vk=s.savedVK=vk;s.mod=s.savedMod=modifiers;
    DWORD value=0;if(readDword(L"HotkeyVK",value))s.vk=s.savedVK=value;
    value=0;if(readDword(L"HotkeyModifiers",value))s.mod=s.savedMod=value;
    value=0;readDword(L"ThemeLight",value);s.light=value!=0;
    s.themeMode=s.light?1:2;readDword(L"ThemeMode",s.themeMode);if(s.themeMode>2)s.themeMode=2;
    s.light=s.themeMode==0?ConnectUI::systemLight():s.themeMode==1;
    value=0;readDword(L"LanguageEN",value);s.english=value!=0;
    s.appearance=false;s.keyboardFocus=false;s.focused=-1;s.hovered=-1;s.pressed=-1;
    s.preview=previewTheme>=0;if(s.preview)s.light=previewTheme!=0;
    s.status=shortcutAvailable?L"":s.tr(L"Горячая клавиша недоступна. Выберите другую и сохраните.",L"Shortcut unavailable. Choose another and save.");
    s.scale=GetDpiForSystem()/96.0f;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=int(WIDTH*s.scale),height=int(HEIGHT*s.scale);
    s.hwnd=CreateWindowExW(WS_EX_APPWINDOW,L"ScreenshotSettings",L"Screenshot — настройки",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,work.left+(work.right-work.left-width)/2,
        work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,&s);
    if(!s.hwnd)return;
    DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;DwmSetWindowAttribute(s.hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    MARGINS margins{1,1,1,1};DwmExtendFrameIntoClientArea(s.hwnd,&margins);s.theme();
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



