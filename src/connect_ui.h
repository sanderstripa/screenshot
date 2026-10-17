#pragma once
#include <windows.h>
#include <d2d1.h>
namespace ConnectUI {
constexpr float Radius=8, ControlHeight=36;
// CONNECT VPN frontend/src/style.css. Palette adoption explicitly requested.
inline UINT background(bool light){return light?0xF7F7F5:0x0C0C0D;}
inline UINT ink(bool light){return light?0x171A1D:0xEDEDEE;}
inline UINT muted(bool light){return light?0x80858B:0x81858C;}
inline UINT line(bool light){return light?0xDADBDC:0x303236;}
inline UINT hover(bool light){return light?0xEEEEEE:0x1A1B1E;}
inline UINT accent(bool light){return light?0x171A1D:0xE6E7E9;}
inline UINT inverse(bool light){return light?0xFFFFFF:0x121315;}
// Monitor, Sun, Moon, Palette and ArrowLeft: same Lucide icons as CONNECT VPN.
enum class Icon {Monitor,Sun,Moon,Palette,Back};
inline void drawIcon(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,Icon icon,float x,float y,float size) {
    D2D1_MATRIX_3X2_F previous;target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Scale(size/24,size/24)*D2D1::Matrix3x2F::Translation(x,y)*previous);
    ID2D1Factory* factory=nullptr;target->GetFactory(&factory);
    ID2D1StrokeStyle* stroke=nullptr;
    auto style=D2D1::StrokeStyleProperties();style.startCap=style.endCap=style.dashCap=D2D1_CAP_STYLE_ROUND;style.lineJoin=D2D1_LINE_JOIN_ROUND;
    factory->CreateStrokeStyle(style,nullptr,0,&stroke);
    auto line=[&](float ax,float ay,float bx,float by){target->DrawLine(D2D1::Point2F(ax,ay),D2D1::Point2F(bx,by),brush,2,stroke);};
    if(icon==Icon::Monitor) {
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2,3,22,17),2,2),brush,2,stroke);
        line(8,21,16,21);line(12,17,12,21);
    } else if(icon==Icon::Sun) {
        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12,12),4,4),brush,2,stroke);
        line(12,2,12,4);line(12,20,12,22);line(4.93f,4.93f,6.34f,6.34f);
        line(17.66f,17.66f,19.07f,19.07f);line(2,12,4,12);line(20,12,22,12);
        line(6.34f,17.66f,4.93f,19.07f);line(19.07f,4.93f,17.66f,6.34f);
    } else if(icon==Icon::Back) {
        line(12,19,5,12);line(5,12,12,5);line(19,12,5,12);
    } else {
        ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            auto arc=[&](float px,float py,float rx,float ry,bool clockwise,bool large=false){
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(px,py),D2D1::SizeF(rx,ry),0,
                    clockwise?D2D1_SWEEP_DIRECTION_CLOCKWISE:D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                    large?D2D1_ARC_SIZE_LARGE:D2D1_ARC_SIZE_SMALL));};
            if(icon==Icon::Moon) {
                sink->BeginFigure(D2D1::Point2F(20.985f,12.486f),D2D1_FIGURE_BEGIN_HOLLOW);
                arc(11.512f,3.014f,9,9,true,true);
                sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(11.917f,2.992f),D2D1::Point2F(12.129f,3.474f),D2D1::Point2F(11.914f,3.817f)));
                arc(20.182f,12.085f,6,6,false);
                sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(20.526f,11.870f),D2D1::Point2F(21.007f,12.081f),D2D1::Point2F(20.985f,12.486f)));
            } else {
                sink->BeginFigure(D2D1::Point2F(12,22),D2D1_FIGURE_BEGIN_HOLLOW);
                arc(12,2,10,10,true);arc(22,11,10,9,true);arc(17,16,5,5,true);
                sink->AddLine(D2D1::Point2F(14.75f,16));arc(13.35f,18.8f,1.75f,1.75f,false);
                sink->AddLine(D2D1::Point2F(13.65f,19.2f));arc(12.25f,22,1.75f,1.75f,true);
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);
            sink->Release();
        }
        if(path)path->Release();
        if(icon==Icon::Palette)for(auto point:{D2D1::Point2F(13.5f,6.5f),D2D1::Point2F(17.5f,10.5f),D2D1::Point2F(6.5f,12.5f),D2D1::Point2F(8.5f,7.5f)})
            target->FillEllipse(D2D1::Ellipse(point,1.5f,1.5f),brush);
    }
    if(stroke)stroke->Release();factory->Release();target->SetTransform(previous);
}
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
