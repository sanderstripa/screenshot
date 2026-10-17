param([string]$InputFile='assets/CONNECT-settings-08.svg',[string]$OutputFile='src/approved_settings_icon.h')
$ErrorActionPreference='Stop';$nl=[Environment]::NewLine
[xml]$svg=Get-Content -LiteralPath $InputFile -Raw -Encoding UTF8
$ns=[Xml.XmlNamespaceManager]::new($svg.NameTable);$ns.AddNamespace('s','http://www.w3.org/2000/svg')
$path=$svg.SelectSingleNode('/s:svg/s:path',$ns)
if($path.fill -ne 'currentColor' -or $path.'fill-rule' -ne 'evenodd'){throw 'Unexpected approved SVG fill'}
$tokens=[regex]::Matches($path.d,'[A-Za-z]|[-+]?(?:\d*\.)?\d+(?:[eE][-+]?\d+)?')|ForEach-Object Value
$commands=[Collections.Generic.List[string]]::new()
$i=0
function Number {if($script:i -ge $tokens.Count){throw 'Truncated SVG path'};$v=$tokens[$script:i];$script:i++;if($v -notmatch '^[-+]?\d'){throw 'Expected SVG number'};return $v+'f'}
while($i -lt $tokens.Count) {
    $command=$tokens[$i];$i++
    switch($command) {
        'M' {$x=Number;$y=Number;$commands.Add("    sink->BeginFigure(D2D1::Point2F($x,$y),D2D1_FIGURE_BEGIN_FILLED);")}
        'C' {$x1=Number;$y1=Number;$x2=Number;$y2=Number;$x=Number;$y=Number;$commands.Add("    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F($x1,$y1),D2D1::Point2F($x2,$y2),D2D1::Point2F($x,$y)));")}
        'A' {$rx=Number;$ry=Number;$angle=Number;$large=Number;$sweep=Number;$x=Number;$y=Number
            $direction=if($sweep -eq '1f'){'D2D1_SWEEP_DIRECTION_CLOCKWISE'}else{'D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE'}
            $arc=if($large -eq '1f'){'D2D1_ARC_SIZE_LARGE'}else{'D2D1_ARC_SIZE_SMALL'}
            $commands.Add("    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F($x,$y),D2D1::SizeF($rx,$ry),$angle,$direction,$arc));")}
        'Z' {$commands.Add('    sink->EndFigure(D2D1_FIGURE_END_CLOSED);')}
        default {throw "Unsupported command in approved SVG: $command"}
    }
}
$circle=$svg.SelectSingleNode('/s:svg/s:circle',$ns)
if($svg.DocumentElement.viewBox -ne '5 0 85 85'){throw 'Approved SVG viewBox changed'}
$hash=(Get-FileHash -LiteralPath $InputFile -Algorithm SHA256).Hash.ToLowerInvariant()
$header=@'
#pragma once
#include <d2d1.h>
// Generated directly from assets/CONNECT-settings-08.svg. Do not hand-edit geometry.
'@
$header+=$nl+"// SVG SHA-256: $hash"+$nl
$header+=@'
inline HRESULT createApprovedSettingsIcon(ID2D1Factory* factory,ID2D1PathGeometry** output) {
    ID2D1PathGeometry* geometry=nullptr;
    HRESULT result=factory->CreatePathGeometry(&geometry);if(FAILED(result))return result;
    ID2D1GeometrySink* sink=nullptr;result=geometry->Open(&sink);
    if(FAILED(result)){geometry->Release();return result;}
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
'@
$header+=$nl+($commands -join $nl)+$nl
$header+=@'
    result=sink->Close();sink->Release();
    if(FAILED(result)){geometry->Release();return result;}
    *output=geometry;return S_OK;
}
inline void drawApprovedSettingsIcon(ID2D1RenderTarget* target,ID2D1Brush* brush,
    ID2D1Geometry* geometry,float x,float y,float size) {
    if(!geometry)return;
    D2D1_MATRIX_3X2_F previous;target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Translation(-5,0)*
        D2D1::Matrix3x2F::Scale(size/85,size/85)*D2D1::Matrix3x2F::Translation(x,y)*previous);
    target->FillGeometry(geometry,brush);
'@
$header+=$nl+"    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F($($circle.cx)f,$($circle.cy)f),$($circle.r)f,$($circle.r)f),brush);"+$nl
$header+="    target->SetTransform(previous);"+$nl+"}"+$nl
$header=[regex]::Replace($header,'(?<![\w.])(-?\d+)f\b','$1.0f')
[IO.File]::WriteAllText((Join-Path (Get-Location) $OutputFile),$header,[Text.UTF8Encoding]::new($false))
