#pragma once
#include <d2d1.h>
// Generated from the exact Lucide 0.468.0 SVGs locked by CONNECT VPN.
inline void drawVPNThemeIcon(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,int icon,float x,float y,float size) {
    D2D1_MATRIX_3X2_F previous;target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Scale(size/24,size/24)*D2D1::Matrix3x2F::Translation(x,y)*previous);
    ID2D1Factory* factory=nullptr;target->GetFactory(&factory);ID2D1StrokeStyle* stroke=nullptr;
    auto style=D2D1::StrokeStyleProperties();style.startCap=style.endCap=style.dashCap=D2D1_CAP_STYLE_ROUND;style.lineJoin=D2D1_LINE_JOIN_ROUND;
    factory->CreateStrokeStyle(style,nullptr,0,&stroke);
// monitor SHA-256: d1a443233345724859de8e4fb48968ba91e885e12ddbd710701548bd91b39428
    if(icon==0) {
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2.0f,3.0f,22.0f,17.0f),2.0f,2.0f),brush,2,stroke);
        target->DrawLine(D2D1::Point2F(8.0f,21.0f),D2D1::Point2F(16.0f,21.0f),brush,2,stroke);
        target->DrawLine(D2D1::Point2F(12.0f,17.0f),D2D1::Point2F(12.0f,21.0f),brush,2,stroke);
    }
// sun SHA-256: a3955c8b0425fcc5c9ba12d2bdd77fe53b2cd5dd05e7742a56f342e996fb2ef8
    else if(icon==1) {
        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12.0f,12.0f),4.0f,4.0f),brush,2,stroke);
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(12.0f,2.0f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(12.0f,4.0f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(12.0f,20.0f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(12.0f,22.0f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(4.93f,4.93f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(6.34f,6.34f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(17.66f,17.66f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(19.07f,19.07f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(2.0f,12.0f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(4.0f,12.0f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(20.0f,12.0f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(22.0f,12.0f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(6.34f,17.66f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(4.93f,19.07f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(19.07f,4.93f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(17.66f,6.34f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
    }
// moon SHA-256: 47503477c9c6848eeaff01c324093e1a04a7d264220f580662d92f229eaa428e
    else if(icon==2) {
        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(12.0f,3.0f),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(21.0f,12.0f),D2D1::SizeF(6.363961031f,6.363961031f),0,D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(12.0f,3.0f),D2D1::SizeF(9.0f,9.0f),0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_LARGE));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();
        } if(path)path->Release(); }
    }
    if(stroke)stroke->Release();factory->Release();target->SetTransform(previous);
}
