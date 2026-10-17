#pragma once
#include <windows.h>
#include <d2d1.h>
namespace ConnectUI {
constexpr float Radius=8, ControlHeight=36;
inline bool systemLight() {
    DWORD value=1,bytes=sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&value,&bytes);
    return value!=0;
}
inline void feedback(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,D2D1_RECT_F rect,D2D1_COLOR_F ink,bool hover,bool pressed,bool focused) {
    if(hover||pressed) {
        auto tint=ink;tint.a=pressed?.12f:.06f;brush->SetColor(tint);
        target->FillRoundedRectangle(D2D1::RoundedRect(rect,Radius,Radius),brush);
    }
    if(focused) {
        brush->SetColor(ink);rect.left-=3;rect.top-=3;rect.right+=3;rect.bottom+=3;
        target->DrawRoundedRectangle(D2D1::RoundedRect(rect,Radius+2,Radius+2),brush,2);
    }
    // Immediate feedback also respects reduced-motion preferences.
}
}
