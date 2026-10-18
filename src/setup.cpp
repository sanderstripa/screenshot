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
#include <dwrite_1.h>
#include "bundled_font.h"
#include "connect_ui.h"
#include "approved_settings_icon.h"
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cwchar>
#include <vector>
#include <wincodec.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlguid.h>
#include <servprov.h>
#include <shobjidl.h>
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"ole32.lib")
using Microsoft::WRL::ComPtr;

namespace {
constexpr int W = 840, H = 540;
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
    if(s==Welcome)return D2D1::RectF(44,288,272,337);
    if(s==Hotkey)return D2D1::RectF(566,335,794,384);
    if(s==Confirm)return D2D1::RectF(391,295,619,344);
    if(s==Ready)return D2D1::RectF(306,335,534,384);
    return D2D1::RectF(306,270,534,319);
}
D2D1_RECT_F secondaryRect(){return D2D1::RectF(247,295,377,344);}
D2D1_RECT_F inputRect(){return D2D1::RectF(44,202,794,251);}
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
    bool light = true, english = false, preferences = false, keyboardFocus = false;
    int hovered=-1,pressed=-1,focused=-1; DWORD themeMode=1;std::wstring notice;
    void showNotice(const wchar_t* message) {notice=message;focused=20;redraw();}
    const wchar_t* tr(const wchar_t* ru,const wchar_t* en) const {return english?en:ru;}
    D2D1_COLOR_F ink() const {return D2D1::ColorF((light?0x202624:0xEDEDE9));}
    D2D1_COLOR_F mutedInk() const {return D2D1::ColorF((light?0x687071:0xA3A6A5));}
    D2D1_COLOR_F edge() const {return D2D1::ColorF((light?0xD9D8D2:0x414344));}
    D2D1_COLOR_F surface() const {return D2D1::ColorF((light?0xF2F0EB:0x202122));}
    void theme() {BOOL dark=!light;DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));COLORREF border=light?RGB(218,218,218):RGB(45,47,50);DwmSetWindowAttribute(hwnd,DWMWA_BORDER_COLOR,&border,sizeof(border));redraw();}
    bool needsRelogin = false;
    bool desktopShortcut=true,startMenuShortcut=true,openAfter=true;
    std::wstring destination;
    HWND pathEdit{};HFONT pathFont{};HBRUSH pathBrush{};int pathDpi=-1;bool pathLight=false;
    void syncPathEdit() {
        if(!hwnd)return;
        if(!pathEdit){
            if(!fontPath.empty())AddFontResourceExW(fontPath.c_str(),FR_PRIVATE,nullptr);
            pathEdit=CreateWindowExW(0,L"EDIT",installDirectory().c_str(),WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,
                0,0,0,0,hwnd,reinterpret_cast<HMENU>(1001),instance,nullptr);
            SendMessageW(pathEdit,EM_SETLIMITTEXT,230,0);
        }
        if(pathDpi!=int(scale*96)){if(pathFont)DeleteObject(pathFont);
        pathFont=CreateFontW(-int(12*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Montserrat");
        SendMessageW(pathEdit,WM_SETFONT,reinterpret_cast<WPARAM>(pathFont),TRUE);pathDpi=int(scale*96);}
        if(!pathBrush||pathLight!=light){if(pathBrush)DeleteObject(pathBrush);pathBrush=CreateSolidBrush(light?RGB(235,233,228):RGB(41,43,44));pathLight=light;}
        SetWindowPos(pathEdit,nullptr,int(57*scale),int(193*scale),int(620*scale),int(20*scale),SWP_NOZORDER|SWP_NOACTIVATE);
        ShowWindow(pathEdit,preferences&&notice.empty()?SW_SHOWNOACTIVATE:SW_HIDE);InvalidateRect(pathEdit,nullptr,TRUE);
    }
    bool acceptPath() {
        wchar_t raw[32768]{},expanded[32768]{},full[32768]{};
        if(pathEdit)GetWindowTextW(pathEdit,raw,32768);else wcscpy_s(raw,installDirectory().c_str());
        const DWORD length=ExpandEnvironmentStringsW(raw,expanded,32768);
        if(!length||length>32768||!std::filesystem::path(expanded).is_absolute()||wcslen(expanded)>230||wcspbrk(expanded,L"\"<>|*?")){
            showNotice(tr(L"Укажите полный путь к папке установки.",L"Enter an absolute installation folder path."));return false;
        }
        DWORD n=GetFullPathNameW(expanded,32768,full,nullptr);if(!n||n>=32768)return false;
        std::filesystem::path path(full);path=path.lexically_normal();
        if(path==path.root_path()){showNotice(tr(L"Выберите отдельную папку для Screenshot.",L"Choose a dedicated folder for Screenshot."));return false;}
        destination=path.wstring();while(!destination.empty()&&(destination.back()==L'\\'||destination.back()==L'/'))destination.pop_back();
        const DWORD attributes=GetFileAttributesW(destination.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY)){showNotice(tr(L"Этот путь указывает на файл. Выберите папку.",L"This path is a file. Choose a folder."));return false;}
        return true;
    }
    void browsePath() {
        ComPtr<IFileOpenDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
        DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
        dialog->SetTitle(tr(L"Папка установки Screenshot",L"Screenshot installation folder"));
        ComPtr<IShellItem> initial;wchar_t current[32768]{};GetWindowTextW(pathEdit,current,32768);
        if(SUCCEEDED(SHCreateItemFromParsingName(current,nullptr,IID_PPV_ARGS(&initial))))dialog->SetFolder(initial.Get());
        if(SUCCEEDED(dialog->Show(hwnd))){ComPtr<IShellItem> result;PWSTR path=nullptr;
            if(SUCCEEDED(dialog->GetResult(&result))&&SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(pathEdit,path);CoTaskMemFree(path);}}
    }
    bool saveAppearance() {
        HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\SanderStripa\\Screenshot",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
        bool ok=writeDWORD(key,L"ThemeMode",themeMode)&&writeDWORD(key,L"ThemeLight",light?1:0)&&writeDWORD(key,L"LanguageEN",english?1:0);RegCloseKey(key);return ok;
    }
    bool hoverPrimary = false, hoverSecondary = false, hoverInput = false;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Bitmap> artwork;
    ComPtr<ID2D1PathGeometry> settingsIcon;
    std::thread worker;
    ComPtr<IDWriteFactory> fonts;
    ComPtr<IDWriteTextFormat> regular, medium, smallText, headline, actionFont, detailFont, eyebrowFont;
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
        return create(14,DWRITE_FONT_WEIGHT_NORMAL,regular)&&create(23,DWRITE_FONT_WEIGHT_MEDIUM,medium)
            &&create(10,DWRITE_FONT_WEIGHT_NORMAL,smallText)&&create(42,DWRITE_FONT_WEIGHT_MEDIUM,headline)
            &&create(14,DWRITE_FONT_WEIGHT_SEMI_BOLD,actionFont)&&create(12,DWRITE_FONT_WEIGHT_NORMAL,detailFont)
            &&create(12,DWRITE_FONT_WEIGHT_SEMI_BOLD,eyebrowFont);
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
        if(font==headline.Get()||y>=510) {
            ComPtr<IDWriteTextLayout> layout;ComPtr<IDWriteTextLayout1> tracking;
            if(SUCCEEDED(fonts->CreateTextLayout(value.c_str(),static_cast<UINT32>(value.size()),font,width,height,&layout))&&SUCCEEDED(layout.As(&tracking))) {
                const float spacing=font==headline.Get()?-.9f:.5f;
                tracking->SetCharacterSpacing(spacing,spacing,0,DWRITE_TEXT_RANGE{0,static_cast<UINT32>(value.size())});
                target->DrawTextLayout(D2D1::Point2F(x,y),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);return;
            }
        }
        target->DrawTextW(value.c_str(),(UINT32)value.size(),font,
            D2D1::RectF(x,y,x+width,y+height),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void background() {
        target->Clear(surface());
    }
    void header() {
        brush->SetColor(hovered==10?ink():mutedInk());
        target->DrawLine(D2D1::Point2F(734,36),D2D1::Point2F(745,36),brush.Get(),1);
        brush->SetColor(hovered==11?ink():mutedInk());
        target->DrawLine(D2D1::Point2F(777,32),D2D1::Point2F(785,40),brush.Get(),1);
        target->DrawLine(D2D1::Point2F(785,32),D2D1::Point2F(777,40),brush.Get(),1);
        if(keyboardFocus&&(focused==10||focused==11))outline(D2D1::RectF(focused==10?725.0f:767.0f,20,focused==10?753.0f:795.0f,50),5,mutedInk());
        brush->SetColor(edge());target->DrawLine(D2D1::Point2F(44,490),D2D1::Point2F(794,490),brush.Get());
        label(L"by CONNECT.",44,510,250,20,smallText.Get(),mutedInk());
        label(tr(L"Версия 0.6.8",L"Version 0.6.8"),544,510,250,20,smallText.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    void button(D2D1_RECT_F rect,std::wstring text,bool active=true) {
        rounded(rect,10,D2D1::ColorF(light?0x242A28:0xE3E4DF));
        if(!active)outline(rect,8,edge());
        ConnectUI::feedback(target.Get(),brush.Get(),rect,ink(),hovered==(notice.empty()?1:20),pressed==(notice.empty()?1:20),focused==(notice.empty()?1:20));
        actionFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        label(text,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,actionFont.Get(),D2D1::ColorF(light?0xFFFFFF:0x202122),DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    void secondaryButton(D2D1_RECT_F rect,std::wstring text) {
        rounded(rect,8,D2D1::ColorF(ConnectUI::hover(light)));
        outline(rect,8,edge());
        ConnectUI::feedback(target.Get(),brush.Get(),rect,ink(),hovered==2,pressed==2,focused==2);
        regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        label(text,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,regular.Get(),
            ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
        regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    void choice(D2D1_RECT_F rect,const wchar_t* value,bool selected,int id,int =-1) {
        if(selected){rounded(rect,5,D2D1::ColorF(light?0xEBE9E4:0x292B2C));outline(rect,5,mutedInk());}
        detailFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        label(value,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,detailFont.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
        detailFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(keyboardFocus&&focused==id)outline(rect,5,mutedInk(),2);
    }
    void toggle(float x,float y,const wchar_t* text,bool checked,int id,bool compact=false) {
        rounded(D2D1::RectF(x,y,x+38,y+22),11,checked?D2D1::ColorF(light?0x242A28:0xE3E4DF):surface());
        if(!checked)outline(D2D1::RectF(x,y,x+38,y+22),11,mutedInk());
        brush->SetColor(checked?D2D1::ColorF(light?0xFFFFFF:0x202122):mutedInk());
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+(checked?27:11),y+11),7,7),brush.Get());
        label(text,x+51,y+2,450,24,compact?detailFont.Get():regular.Get(),ink());
        if(keyboardFocus&&focused==id)outline(D2D1::RectF(x-3,y-3,x+41,y+25),14,mutedInk(),2);
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
        if(preferences) {
            label(tr(L"Настройки установки",L"Installation settings"),44,68,650,38,medium.Get(),ink());
            label(tr(L"Назад",L"Back"),730,75,64,24,detailFont.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            label(tr(L"Язык",L"Language"),44,122,50,24,detailFont.Get(),ink());
            choice(D2D1::RectF(94,116,130,144),L"RU",!english,6);choice(D2D1::RectF(134,116,170,144),L"EN",english,7);
            label(tr(L"Тема",L"Theme"),250,122,50,24,detailFont.Get(),ink());
            choice(D2D1::RectF(297,116,363,144),tr(L"Светлая",L"Light"),themeMode==1,4);
            choice(D2D1::RectF(367,116,433,144),tr(L"Тёмная",L"Dark"),themeMode==2,5);
            choice(D2D1::RectF(437,116,510,144),tr(L"Система",L"System"),themeMode==0,3);
            label(tr(L"Папка установки",L"Installation folder"),44,162,650,24,detailFont.Get(),mutedInk());
            rounded(D2D1::RectF(44,185,692,220),6,D2D1::ColorF(light?0xEBE9E4:0x292B2C));outline(D2D1::RectF(44,185,692,220),6,edge());
            outline(D2D1::RectF(702,185,794,220),6,edge());
            label(tr(L"Выбрать…",L"Browse…"),702,194,92,24,detailFont.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            toggle(44,242,tr(L"Ярлык на рабочем столе",L"Desktop shortcut"),desktopShortcut,12);
            toggle(44,280,tr(L"Ярлык в меню «Пуск»",L"Start menu shortcut"),startMenuShortcut,13);
            label(tr(L"Язык и оформление будут применены к приложению.",L"Language and appearance will also apply to the application."),44,318,750,25,smallText.Get(),mutedInk());
            button(D2D1::RectF(44,355,304,404),tr(L"Принять и установить",L"Accept and install"));return;
        }
        if(step==Welcome) {
            label(tr(L"У С Т А Н О В К А",L"I N S T A L L A T I O N"),44,174,365,25,eyebrowFont.Get(),ink());
            label(L"Screenshot",44,207,365,60,headline.Get(),ink());button(primaryRect(step),tr(L"Установить",L"Install"));
            label(tr(L"Настройки установки",L"Installation settings"),44,359,300,28,regular.Get(),mutedInk());
        } else if(step==Installing) {
            rounded(D2D1::RectF(44,436,794,440),2,edge());float f=std::clamp(progress.load()/100.0f,0.0f,1.0f);
            if(f>0)rounded(D2D1::RectF(44,436,44+750*f,440),2,ink());
            label(tr(L"Установка…",L"Installing…"),44,451,750,25,detailFont.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
            if(result.load()>=1){fill(D2D1::RectF(44,330,794,482),surface());label(failureDetail,44,334,750,44,detailFont.Get(),mutedInk());button(D2D1::RectF(306,395,534,444),tr(L"Повторить",L"Retry"));}
        } else if(step==Installed) {
            label(L"Screenshot",44,170,750,60,headline.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"Установка завершена",L"Installation complete"),44,235,750,30,regular.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),tr(L"Продолжить",L"Continue"));label(tr(L"Далее — выбор горячей клавиши.",L"Next, choose your keyboard shortcut."),44,340,750,24,smallText.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
        } else if(step==Hotkey) {
            label(tr(L"Горячая клавиша",L"Keyboard shortcut"),44,92,750,40,medium.Get(),ink());
            label(tr(L"Нажмите поле, затем клавишу для создания скриншота.",L"Click the field, then press a key to capture screenshots."),44,145,750,32,regular.Get(),mutedInk());
            rounded(inputRect(),6,D2D1::ColorF(light?0xEBE9E4:0x292B2C));outline(inputRect(),6,listening?mutedInk():edge());
            regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            label(listening?tr(L"Нажмите клавишу…",L"Press a key…"):config.title,44,202,750,49,regular.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            regular->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            label(tr(L"Вы можете изменить сочетание позже в приложении.",L"You can change the shortcut later in the application."),44,270,750,26,smallText.Get(),mutedInk());button(primaryRect(step),tr(L"Далее",L"Next"));
        } else if(step==Confirm) {
            label(L"PRINT SCREEN",44,158,750,24,eyebrowFont.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"Заменить системное действие?",L"Replace the Windows action?"),44,193,750,42,medium.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"Назначить Print Screen приложению Screenshot\nвместо стандартного инструмента Windows?",L"Assign Print Screen to Screenshot\ninstead of the default Windows capture tool?"),44,245,750,48,regular.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
            secondaryButton(secondaryRect(),tr(L"Отмена",L"Cancel"));button(primaryRect(step),tr(L"Да, заменить",L"Yes, replace"));
        } else if(step==Ready) {
            label(L"Screenshot",44,111,750,60,headline.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"Всё готово",L"All set"),44,176,750,30,regular.Get(),ink(),DWRITE_TEXT_ALIGNMENT_CENTER);
            label(needsRelogin?tr(L"Для активации клавиши перезапустите сеанс Windows.",L"Sign out and back in to activate the key."):std::wstring(tr(L"Горячая клавиша: ",L"Keyboard shortcut: "))+config.title,44,239,750,40,regular.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
            label(tr(L"PNG → Pictures\\Screenshot + буфер обмена",L"PNG to Pictures\\Screenshot + clipboard"),44,294,750,25,smallText.Get(),mutedInk(),DWRITE_TEXT_ALIGNMENT_CENTER);
            button(primaryRect(step),tr(L"Готово",L"Done"));toggle(284,407,tr(L"Запустить Screenshot после установки",L"Launch Screenshot after installation"),openAfter,14,true);
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
            HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(203),RT_RCDATA);
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
        if(artwork&&!preferences&&notice.empty()) {
            if(step==Welcome)target->DrawBitmap(artwork.Get(),D2D1::RectF(378,96,818,389));
            if(step==Installing)target->DrawBitmap(artwork.Get(),D2D1::RectF(185,68,655,381));
        }
        if(!notice.empty()) {
            fill(D2D1::RectF(0,60,W,H),surface());
            rounded(D2D1::RectF(44,130,794,425),8,surface());outline(D2D1::RectF(44,130,794,425),8,edge());
            label(tr(L"УВЕДОМЛЕНИЕ",L"NOTICE"),68,152,702,40,medium.Get(),ink());
            label(notice,68,212,702,112,regular.Get(),mutedInk());
            button(D2D1::RectF(566,350,770,399),L"OK");
        }
        HRESULT hr=target->EndDraw();
        if(hr==D2DERR_RECREATE_TARGET){artwork.Reset();brush.Reset();target.Reset();}
        EndPaint(hwnd,&ps);
    }
    void redraw() { syncPathEdit();InvalidateRect(hwnd,nullptr,FALSE); }
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
    bool launchBackground(bool settings=false) {
        std::wstring file=installDirectory()+L"\\Screenshot.exe";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd=L"\""+file+(settings?L"\" --settings":L"\" --background");
        // An administrator-run setup must not turn the ordinary background app
        // into an administrator process. Reuse this user's Explorer token.
        HANDLE processToken=nullptr;TOKEN_ELEVATION elevation{};DWORD bytes=0;
        bool elevated=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&processToken)&&
            GetTokenInformation(processToken,TokenElevation,&elevation,sizeof(elevation),&bytes)&&elevation.TokenIsElevated;
        if(processToken)CloseHandle(processToken);
        if(elevated) {
            // Ask the desktop Shell to execute as its ordinary user. ShellWindows
            // resolves the existing Explorer instead of creating an elevated COM server.
            ComPtr<IShellWindows> windows;
            ComPtr<IDispatch> desktopDispatch,background,application;
            ComPtr<IServiceProvider> provider;
            ComPtr<IShellBrowser> browser;
            ComPtr<IShellView> view;
            ComPtr<IShellFolderViewDual> folder;
            ComPtr<IShellDispatch2> shell;
            VARIANT location{},root{};location.vt=VT_I4;location.lVal=CSIDL_DESKTOP;
            long desktopWindow=0;
            if(SUCCEEDED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows)))&&
                SUCCEEDED(windows->FindWindowSW(&location,&root,SWC_DESKTOP,&desktopWindow,SWFO_NEEDDISPATCH,&desktopDispatch))&&desktopDispatch&&
                SUCCEEDED(desktopDispatch.As(&provider))&&
                SUCCEEDED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser)))&&
                SUCCEEDED(browser->QueryActiveShellView(&view))&&
                SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&background)))&&
                SUCCEEDED(background.As(&folder))&&SUCCEEDED(folder->get_Application(&application))&&
                SUCCEEDED(application.As(&shell))) {
                BSTR executable=SysAllocString(file.c_str());
                VARIANT arguments{},directory{},operation{},show{};
                arguments.vt=VT_BSTR;arguments.bstrVal=SysAllocString(settings?L"--settings":L"--background");
                operation.vt=VT_BSTR;operation.bstrVal=SysAllocString(L"open");
                show.vt=VT_I4;show.lVal=SW_SHOWNORMAL;
                HRESULT launched=(executable&&arguments.bstrVal&&operation.bstrVal)?
                    shell->ShellExecute(executable,arguments,directory,operation,show):E_OUTOFMEMORY;
                SysFreeString(executable);VariantClear(&arguments);VariantClear(&operation);
                if(SUCCEEDED(launched))return true;
            }
            DWORD desktopPID=0;GetWindowThreadProcessId(GetShellWindow(),&desktopPID);
            HANDLE desktop=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,desktopPID);
            HANDLE desktopToken=nullptr,primary=nullptr;
            bool started=false;
            if(desktop&&OpenProcessToken(desktop,TOKEN_QUERY|TOKEN_DUPLICATE,&desktopToken)&&
                DuplicateTokenEx(desktopToken,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY,
                    nullptr,SecurityImpersonation,TokenPrimary,&primary)) {
                started=CreateProcessWithTokenW(primary,0,file.c_str(),cmd.data(),0,nullptr,nullptr,&si,&pi)!=FALSE;
            }
            if(primary)CloseHandle(primary);
            if(desktopToken)CloseHandle(desktopToken);
            if(desktop)CloseHandle(desktop);
            if(started){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;}
            // IPC still supports the fallback elevated worker via its single
            // explicitly permitted open-settings message.
            cmd=L"\""+file+(settings?L"\" --settings":L"\" --background");
        }
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
            showNotice(tr(L"Не удалось сохранить клавишу.",L"Could not save the shortcut."));
            return;
        }
        if(config.vk==VK_SNAPSHOT && config.mod==0) {
            if(!disableSnippingBinding()) {
                showNotice(tr(L"Windows не разрешила изменить системную привязку Print Screen.\nОтключите её вручную в Параметры → Специальные возможности → Клавиатура.",L"Windows could not change the Print Screen binding.\nDisable it in Settings > Accessibility > Keyboard."));
                return;
            }
        }
        needsRelogin=!verifyHotkey();
        if(!launchBackground()) {
            showNotice(tr(L"Не удалось запустить Screenshot. Повторите установку.",L"Could not start Screenshot. Please reinstall."));
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
    std::wstring registeredDirectory() {
        wchar_t value[32768]{};DWORD size=sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screenshot",L"InstallLocation",RRF_RT_REG_SZ,nullptr,value,&size)==ERROR_SUCCESS?value:L"";
    }
    std::wstring installDirectory() {
        if(!destination.empty())return destination;
        wchar_t registered[32768]{};DWORD length=sizeof(registered);
        if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screenshot",L"InstallLocation",RRF_RT_REG_SZ,nullptr,registered,&length)==ERROR_SUCCESS&&registered[0])return registered;
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
            writeString(key,L"DisplayVersion",L"0.6.8") &&
            writeString(key,L"Publisher",L"Sander Stripa") &&
            writeString(key,L"InstallLocation",dir) &&
            writeString(key,L"DisplayIcon",exe) &&
            writeString(key,L"UninstallString",L"\""+uninstaller+L"\" --uninstall") &&
            writeDWORD(key,L"NoModify",1) &&
            writeDWORD(key,L"NoRepair",1);
        RegCloseKey(key);
        return ok&&createStartMenuShortcut(dir)&&createDesktopShortcut(dir);
    }
    std::wstring shortcutPath(int folder=CSIDL_PROGRAMS) {
        wchar_t programs[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,folder,nullptr,0,programs)))return {};
        return std::wstring(programs)+L"\\Screenshot.lnk";
    }
    bool createStartMenuShortcut(const std::wstring& dir) { return createShortcut(dir,CSIDL_PROGRAMS,startMenuShortcut); }
    bool createDesktopShortcut(const std::wstring& dir) { return createShortcut(dir,CSIDL_DESKTOPDIRECTORY,desktopShortcut); }
    bool createShortcut(const std::wstring& dir,int folder,bool enabled) {
        const auto destination=shortcutPath(folder);
        if(!enabled)return destination.empty()?false:(DeleteFileW(destination.c_str())||GetLastError()==ERROR_FILE_NOT_FOUND);
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
        bool ok=false;
        {
            ComPtr<IShellLinkW> link;
            ComPtr<IPersistFile> file;
            const auto path=destination;const auto exe=dir+L"\\Screenshot.exe";
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
        wchar_t selfPath[32768]{};if(!GetModuleFileNameW(nullptr,selfPath,32768))return 2;
        std::wstring dir=std::filesystem::path(selfPath).parent_path().wstring();
        if(_wcsicmp(std::filesystem::path(selfPath).filename().c_str(),L"Uninstall.exe")!=0)dir=installDirectory();
        if(dir.empty())return 2;
        const std::wstring exe=dir+L"\\Screenshot.exe";
        stopOldScreenshot(exe);
        RegDeleteKeyValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"Screenshot");
        RegDeleteTreeW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screenshot");
        DeleteFileW(exe.c_str());
        for(int folder:{CSIDL_PROGRAMS,CSIDL_DESKTOPDIRECTORY}){const auto shortcut=shortcutPath(folder);if(!shortcut.empty())DeleteFileW(shortcut.c_str());}
        std::wstring self=dir+L"\\Uninstall.exe";
        MoveFileExW(self.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);
        RemoveDirectoryW(dir.c_str());
        return 0;
    }
    bool installPayload() {
        const std::wstring previous=registeredDirectory();
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
        if(!previous.empty()&&_wcsicmp(previous.c_str(),dir.c_str())!=0&&!stopOldScreenshot(previous+L"\\Screenshot.exe")){
            DeleteFileW(temp.c_str());error(tr(L"Не удалось закрыть прежнюю версию Screenshot",L"Could not close the previous Screenshot version"),ERROR_SHARING_VIOLATION);return false;
        }
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
        if(!saveAppearance()){error(tr(L"Не удалось сохранить тему и язык.",L"Could not save theme and language."),GetLastError());return false;}
        if(!previous.empty()&&_wcsicmp(previous.c_str(),dir.c_str())!=0){
            DeleteFileW((previous+L"\\Screenshot.exe").c_str());DeleteFileW((previous+L"\\Uninstall.exe").c_str());RemoveDirectoryW(previous.c_str());
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
        if(!acceptPath())return;
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
    int hit(POINT p) const {
        if(p.y<60&&p.x>720)return p.x>760?11:10;
        if(!notice.empty())return inside(p,D2D1::RectF(566,350,770,399))?20:-1;
        if(preferences){
            if(inside(p,D2D1::RectF(702,185,794,220)))return 16;
            if(inside(p,D2D1::RectF(44,185,692,220)))return 15;
            if(inside(p,D2D1::RectF(94,116,130,144)))return 6;if(inside(p,D2D1::RectF(134,116,170,144)))return 7;
            if(inside(p,D2D1::RectF(297,116,363,144)))return 4;if(inside(p,D2D1::RectF(367,116,433,144)))return 5;if(inside(p,D2D1::RectF(437,116,510,144)))return 3;
            if(inside(p,D2D1::RectF(44,236,600,270)))return 12;if(inside(p,D2D1::RectF(44,274,600,308)))return 13;
            if(inside(p,D2D1::RectF(44,355,304,404)))return 1;return inside(p,D2D1::RectF(720,68,794,104))?2:-1;
        }
        if(step==Welcome&&inside(p,D2D1::RectF(44,350,344,390)))return 8;
        if(step==Ready&&inside(p,D2D1::RectF(284,400,660,440)))return 14;
        if(step==Hotkey&&inside(p,inputRect()))return 0;if(step==Confirm&&inside(p,secondaryRect()))return 2;
        if(step==Installing)return result.load()==1&&inside(p,D2D1::RectF(306,395,534,444))?1:-1;
        return inside(p,primaryRect(step))?1:-1;
    }
    void tab(bool reverse) {
        listening=false;keyboardFocus=true;
        std::vector<int> ids;
        if(!notice.empty())ids={20,10,11};
        else if(preferences)ids={6,7,4,5,3,15,16,12,13,1,2,10,11};
        else if(step==Welcome)ids={1,8,10,11};
        else if(step==Hotkey)ids={0,1,10,11};
        else if(step==Confirm)ids={2,1,10,11};
        else if(step==Installing&&result.load()!=1)ids={10,11};
        else if(step==Ready)ids={1,14,10,11};else ids={1,10,11};
        int index=-1;for(int i=0;i<static_cast<int>(ids.size());i++)if(ids[i]==focused)index=i;
        focused=ids[(index+(reverse?static_cast<int>(ids.size())-1:1)+static_cast<int>(ids.size()))%ids.size()];redraw();
    }
    void activateFocused() {
        POINT p{};
        if(focused==10)p={740,36};else if(focused==11)p={780,36};else if(focused==20)p={660,374};
        else if(preferences){switch(focused){case 6:p={112,130};break;case 7:p={152,130};break;case 4:p={330,130};break;case 5:p={400,130};break;case 3:p={470,130};break;case 15:SetFocus(pathEdit);return;case 16:p={750,202};break;case 12:p={60,253};break;case 13:p={60,291};break;case 1:p={170,380};break;default:p={760,85};}}
        else if(focused==14)p={300,418};else if(focused==0)p={120,220};else if(focused==8)p={100,370};
        else{auto rect=focused==2?secondaryRect():primaryRect(step);p={int((rect.left+rect.right)/2),int((rect.top+rect.bottom)/2)};}nextClick(p);
    }
    void nextClick(POINT p) {
        focused=hit(p);
        if(p.y<60&&p.x>720) {
            if(p.x>760)SendMessageW(hwnd,WM_CLOSE,0,0);else ShowWindow(hwnd,SW_MINIMIZE);return;
        }
        if(!notice.empty()){if(inside(p,D2D1::RectF(566,350,770,399))){notice.clear();focused=-1;redraw();}return;}
        if(preferences) {
            if(focused>=3&&focused<=5){themeMode=focused-3;light=themeMode==0?ConnectUI::systemLight():themeMode==1;theme();}
            else if(focused==6||focused==7){english=focused==7;redraw();}
            else if(focused==16){browsePath();redraw();}
            else if(focused==15){SetFocus(pathEdit);}
            else if(focused==12){desktopShortcut=!desktopShortcut;redraw();}
            else if(focused==13){startMenuShortcut=!startMenuShortcut;redraw();}
            else if(focused==1){preferences=false;startInstallation();}
            else if(focused==2){preferences=false;redraw();}
            return;
        }
        if(step==Welcome&&focused==8){preferences=true;focused=3;redraw();return;}
        if(p.y<60 && p.x>760) { SendMessageW(hwnd,WM_CLOSE,0,0);return; }
        if(p.y<60 && p.x>720) { ShowWindow(hwnd,SW_MINIMIZE);return; }
        if(step==Ready&&focused==14){openAfter=!openAfter;redraw();return;}
        if(step==Hotkey&&inside(p,inputRect())) {listening=true;redraw();return;}
        if(step==Confirm&&inside(p,secondaryRect())) {step=Hotkey;redraw();return;}
        if(step==Installing && result.load()==1) {
            if(inside(p,D2D1::RectF(306,395,534,444))) {
                working=false;startInstallation();
            }
            return;
        }
        if(!inside(p,primaryRect(step)))return;
        switch(step) {
            case Welcome:startInstallation();break;
            case Installing:if(result.load()==1 && inside(p,D2D1::RectF(306,395,534,444))){working=false;startInstallation();}break;
            case Installed:step=Hotkey;redraw();break;
            case Hotkey:
                if(config.vk==VK_SNAPSHOT && config.mod==0) {step=Confirm;redraw();}
                else if(verifyHotkey()) activateHotkey();
                else showNotice(tr(L"Эта клавиша занята другим приложением.\nВыберите другую клавишу.",L"This shortcut is used by another app.\nChoose another key."));
                break;
            case Confirm:activateHotkey();break;
            case Ready:if(!openAfter||launchBackground(true))DestroyWindow(hwnd);else showNotice(tr(L"Не удалось открыть Screenshot. Запустите его из меню Пуск.",L"Could not open Screenshot. Open it from the Start menu."));break;
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
            if(p.y<60&&p.x>=0&&p.x<720)return HTCAPTION;
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
        case WM_CTLCOLOREDIT:if(reinterpret_cast<HWND>(l)==s->pathEdit){SetTextColor(reinterpret_cast<HDC>(w),s->light?RGB(32,38,36):RGB(237,237,233));SetBkColor(reinterpret_cast<HDC>(w),s->light?RGB(235,233,228):RGB(41,43,44));return reinterpret_cast<LRESULT>(s->pathBrush);}break;
        case WM_SETTINGCHANGE:if(s->themeMode==0){s->light=ConnectUI::systemLight();s->theme();}return 0;
        case WM_SIZE:
            s->artwork.Reset();s->brush.Reset();s->target.Reset();s->redraw();return 0;
        case WM_PAINT:s->paint();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_INSTALL_COMPLETE:
            if(s->result.load()==0)s->step=Installed;
            s->working=false;s->redraw();return 0;
        case WM_APP+11:s->redraw();return 0;
        case WM_MOUSEMOVE:{POINT p{int(GET_X_LPARAM(l)/s->scale),int(GET_Y_LPARAM(l)/s->scale)};int hit=s->hit(p);if(hit!=s->hovered){s->hovered=hit;s->redraw();}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);return 0;}
        case WM_MOUSELEAVE:s->hovered=-1;s->redraw();return 0;
        case WM_LBUTTONDOWN:s->pressed=s->hit(POINT{int(GET_X_LPARAM(l)/s->scale),int(GET_Y_LPARAM(l)/s->scale)});SetFocus(hwnd);s->redraw();return 0;
        case WM_LBUTTONUP:{
            s->pressed=-1;s->keyboardFocus=false;
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
            if(msg==WM_SYSKEYDOWN&&w==VK_F4)return DefWindowProcW(hwnd,msg,w,l);
            if(w==VK_TAB){s->tab((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
            if(s->step==Hotkey&&s->listening) {
                if(w==VK_RETURN||w==VK_SPACE){if(s->focused>=0)s->activateFocused();return 0;}
            if(w==VK_ESCAPE){s->listening=false;s->redraw();return 0;}
                UINT mod=0;
                if(GetKeyState(VK_CONTROL)&0x8000)mod|=MOD_CONTROL;
                if(GetKeyState(VK_MENU)&0x8000)mod|=MOD_ALT;
                if(GetKeyState(VK_SHIFT)&0x8000)mod|=MOD_SHIFT;
                s->choose(static_cast<UINT>(w),mod);return 0;
            }
            if(w==VK_RETURN||w==VK_SPACE){if(s->focused>=0)s->activateFocused();return 0;}
            if(w==VK_ESCAPE){if(!s->notice.empty()){s->notice.clear();s->redraw();return 0;}if(s->preferences){s->preferences=false;s->redraw();return 0;}if(s->step!=Installing){DestroyWindow(hwnd);return 0;}}
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
        const std::wstring installed=probe.installDirectory()+L"\\Screenshot.exe";
        DWORD attrs=GetFileAttributesW(installed.c_str());
        int testResult=(probe.result.load()==0 && attrs!=INVALID_FILE_ATTRIBUTES)?0:12;
        CoUninitialize();
        return testResult;
    }
    Installer setup;setup.instance=instance;
    setup.scale=std::clamp(GetDpiForSystem()/96.0f,1.0f,2.5f);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,setup.factory.GetAddressOf());
    if(!setup.factory||FAILED(createApprovedSettingsIcon(setup.factory.Get(),setup.settingsIcon.GetAddressOf()))||!setup.initializeFonts()){CoUninitialize();return 3;}
    setup.medium->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Preview mode exercises the actual native UI without installing anything.
    if(commandLine && wcsstr(commandLine,L"--preview")) {
        int step=0,dpi=96;
        swscanf_s(commandLine,L"--preview %d %d",&step,&dpi);
        setup.step=static_cast<Step>(std::clamp(step,0,5));
        setup.scale=std::clamp(dpi/96.0f,1.0f,3.0f);
        setup.progress=62;
        setup.light=wcsstr(commandLine,L"light")!=nullptr;setup.themeMode=setup.light?1:2;
        setup.preferences=wcsstr(commandLine,L"preferences")!=nullptr;
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
    if(commandLine&&wcsstr(commandLine,L"system")){setup.themeMode=0;setup.light=ConnectUI::systemLight();}
    BOOL dark=!setup.light;
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    setup.theme();
    MARGINS margins{1,1,1,1};DwmExtendFrameIntoClientArea(hwnd,&margins);
    SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    ShowWindow(hwnd,show==SW_HIDE?SW_SHOWNORMAL:show);
    UpdateWindow(hwnd);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(message.hwnd==setup.pathEdit&&message.message==WM_KEYDOWN&&(message.wParam==VK_TAB||message.wParam==VK_RETURN||message.wParam==VK_ESCAPE)){SetFocus(hwnd);if(message.wParam==VK_TAB){setup.focused=15;setup.tab((GetKeyState(VK_SHIFT)&0x8000)!=0);}else if(message.wParam==VK_RETURN){setup.focused=1;setup.activateFocused();}else{setup.preferences=false;setup.redraw();}continue;}
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    // Do not free Installer before the background worker exits.
    if(setup.worker.joinable())setup.worker.join();
    setup.brush.Reset();setup.target.Reset();
    if(setup.icon)DestroyIcon(setup.icon);
    if(setup.largeIcon)DestroyIcon(setup.largeIcon);
    setup.icon=nullptr;
    setup.regular.Reset();setup.medium.Reset();setup.smallText.Reset();setup.headline.Reset();
    setup.actionFont.Reset();setup.detailFont.Reset();setup.eyebrowFont.Reset();setup.collection.Reset();
    if(setup.fontLoader)setup.fonts->UnregisterFontCollectionLoader(setup.fontLoader.Get());
    setup.fontLoader.Reset();setup.fonts.Reset();setup.factory.Reset();
    if(setup.pathFont)DeleteObject(setup.pathFont);if(setup.pathBrush)DeleteObject(setup.pathBrush);
    if(!setup.fontPath.empty()){RemoveFontResourceExW(setup.fontPath.c_str(),FR_PRIVATE,nullptr);DeleteFileW(setup.fontPath.c_str());}
    CoUninitialize();
    return 0;
}
















