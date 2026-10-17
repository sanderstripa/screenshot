param([string]$OutputFile='src/vpn_theme_icons.h')
$ErrorActionPreference='Stop';$nl=[Environment]::NewLine
$code=[Collections.Generic.List[string]]::new()
$code.Add('#pragma once')
$code.Add('#include <d2d1.h>')
$code.Add('// Generated from the exact Lucide 0.468.0 SVGs locked by CONNECT VPN.')
$code.Add('inline void drawVPNThemeIcon(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,int icon,float x,float y,float size) {')
$code.Add('    D2D1_MATRIX_3X2_F previous;target->GetTransform(&previous);')
$code.Add('    target->SetTransform(D2D1::Matrix3x2F::Scale(size/24,size/24)*D2D1::Matrix3x2F::Translation(x,y)*previous);')
$code.Add('    ID2D1Factory* factory=nullptr;target->GetFactory(&factory);ID2D1StrokeStyle* stroke=nullptr;')
$code.Add('    auto style=D2D1::StrokeStyleProperties();style.startCap=style.endCap=style.dashCap=D2D1_CAP_STYLE_ROUND;style.lineJoin=D2D1_LINE_JOIN_ROUND;')
$code.Add('    factory->CreateStrokeStyle(style,nullptr,0,&stroke);')
function F([double]$v){return $v.ToString('0.#########',[Globalization.CultureInfo]::InvariantCulture)+'.0f' -replace '(\.\d+)\.0f$','$1f'}
function N {if($script:i -ge $script:tokens.Count){throw 'Truncated SVG path'};$value=$script:tokens[$script:i];$script:i++;return [double]::Parse($value,[Globalization.CultureInfo]::InvariantCulture)}
$index=0
foreach($name in 'monitor','sun','moon') {
    $file="assets/vpn-theme-$name.svg";[xml]$svg=Get-Content $file -Raw -Encoding UTF8
    $code.Add("// $name SHA-256: $((Get-FileHash $file).Hash.ToLowerInvariant())")
    $code.Add("    $(if($index){'else '})if(icon==$index) {")
    foreach($node in $svg.DocumentElement.ChildNodes) {
        switch($node.LocalName) {
            'rect' {$left=F $node.x;$top=F $node.y;$right=F ([double]$node.x+[double]$node.width);$bottom=F ([double]$node.y+[double]$node.height);$radius=F $node.rx
                $code.Add("        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF($left,$top,$right,$bottom),$radius,$radius),brush,2,stroke);")}
            'line' {$x1=F $node.x1;$y1=F $node.y1;$x2=F $node.x2;$y2=F $node.y2
                $code.Add("        target->DrawLine(D2D1::Point2F($x1,$y1),D2D1::Point2F($x2,$y2),brush,2,stroke);")}
            'circle' {$cx=F $node.cx;$cy=F $node.cy;$r=F $node.r
                $code.Add("        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F($cx,$cy),$r,$r),brush,2,stroke);")}
            'path' {
                $code.Add('        { ID2D1PathGeometry* path=nullptr;ID2D1GeometrySink* sink=nullptr;')
                $code.Add('        if(SUCCEEDED(factory->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))) {')
                $script:tokens=@([regex]::Matches($node.d,'[A-Za-z]|[-+]?(?:\d*\.)?\d+(?:[eE][-+]?\d+)?')|ForEach-Object Value)
                $script:i=0;$px=0.0;$py=0.0;$sx=0.0;$sy=0.0;$command='';$open=$false
                while($i -lt $tokens.Count) {
                    if($tokens[$i] -match '^[A-Za-z]$'){$command=$tokens[$i];$i++}
                    $relative=$command -cmatch '^[a-z]$'
                    switch -CaseSensitive ($command.ToUpperInvariant()) {
                        'M' {$ax=N;$ay=N;if($relative){$ax+=$px;$ay+=$py};$px=$ax;$py=$ay;$sx=$px;$sy=$py;$open=$true
                            $code.Add("            sink->BeginFigure(D2D1::Point2F($(F $px),$(F $py)),D2D1_FIGURE_BEGIN_HOLLOW);")
                            $command=if($relative){'l'}else{'L'}}
                        'L' {$ax=N;$ay=N;if($relative){$ax+=$px;$ay+=$py};$px=$ax;$py=$ay
                            $code.Add("            sink->AddLine(D2D1::Point2F($(F $px),$(F $py)));")}
                        'H' {$ax=N;$px=if($relative){$px+$ax}else{$ax}
                            $code.Add("            sink->AddLine(D2D1::Point2F($(F $px),$(F $py)));")}
                        'V' {$ay=N;$py=if($relative){$py+$ay}else{$ay}
                            $code.Add("            sink->AddLine(D2D1::Point2F($(F $px),$(F $py)));")}
                        'A' {$rx=N;$ry=N;$rotation=N;$large=N;$sweep=N;$ax=N;$ay=N;if($relative){$ax+=$px;$ay+=$py}
                            if($rotation -ne 0){throw 'Unexpected rotated arc in VPN theme SVG'}
                            $lambda=[Math]::Pow(($px-$ax)/(2*$rx),2)+[Math]::Pow(($py-$ay)/(2*$ry),2)
                            if($lambda -gt 1){$factor=[Math]::Sqrt($lambda);$rx*=$factor;$ry*=$factor}
                            $code.Add("            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F($(F $ax),$(F $ay)),D2D1::SizeF($(F $rx),$(F $ry)),0,$(if($sweep){'D2D1_SWEEP_DIRECTION_CLOCKWISE'}else{'D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE'}),$(if($large){'D2D1_ARC_SIZE_LARGE'}else{'D2D1_ARC_SIZE_SMALL'})));")
                            $px=$ax;$py=$ay}
                        'Z' {$code.Add('            sink->EndFigure(D2D1_FIGURE_END_CLOSED);');$open=$false;$px=$sx;$py=$sy;$command=''}
                        default {throw "Unsupported theme SVG command $command"}
                    }
                }
                if($open){$code.Add('            sink->EndFigure(D2D1_FIGURE_END_OPEN);')}
                $code.Add('            if(SUCCEEDED(sink->Close()))target->DrawGeometry(path,brush,2,stroke);sink->Release();')
                $code.Add('        } if(path)path->Release(); }')
            }
            default {throw "Unsupported SVG element $($node.LocalName)"}
        }
    }
    $code.Add('    }');$index++
}
$code.Add('    if(stroke)stroke->Release();factory->Release();target->SetTransform(previous);')
$code.Add('}')
[IO.File]::WriteAllText((Join-Path (Get-Location) $OutputFile),($code -join $nl)+$nl,[Text.UTF8Encoding]::new($false))
