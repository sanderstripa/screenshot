param([Parameter(Mandatory=$true)][string]$App)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PositionNative {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls,string title);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint action,uint param,out Rect r,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr OpenEvent(uint access,bool inherit,string name);
 [DllImport("kernel32.dll")] public static extern bool SetEvent(IntPtr h);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@
[PositionNative]::SetProcessDpiAwarenessContext([IntPtr](-4))|Out-Null
$config='HKCU:\Software\SanderStripa\Screenshot'
$original=$null
try {$original=Get-ItemPropertyValue $config WindowPosition -ErrorAction Stop} catch {}
function Stop-Worker {
 $event=[PositionNative]::OpenEvent(2,$false,'Local\Screenshot.SanderStripa.Exit')
 if($event -ne [IntPtr]::Zero){[PositionNative]::SetEvent($event)|Out-Null;[PositionNative]::CloseHandle($event)|Out-Null}
 Start-Sleep -Milliseconds 700
}
function Open-Window {
 Start-Process -FilePath $App -ArgumentList '--settings' -WindowStyle Hidden
 for($i=0;$i -lt 60;$i++){
  $h=[PositionNative]::FindWindowW('ScreenshotSettings',$null)
  if($h -ne [IntPtr]::Zero){Start-Sleep -Milliseconds 300;return $h}
  Start-Sleep -Milliseconds 100
 }
 throw 'Settings window did not open'
}
function Rect($h){$r=[PositionNative+Rect]::new();[PositionNative]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function Assert-Position($h,$x,$y,$label){$r=Rect $h;if($r.Left -ne $x -or $r.Top -ne $y){throw "$label expected $x,$y got $($r.Left),$($r.Top)"};Write-Output "PASS $label"}
try {
 Stop-Worker
 $h=Open-Window
 $work=[PositionNative+Rect]::new();[PositionNative]::SystemParametersInfo(48,0,[ref]$work,0)|Out-Null
 $x=$work.Left+73;$y=$work.Top+91
 [PositionNative]::SetWindowPos($h,[IntPtr]1,$x,$y,0,0,0x11)|Out-Null
 Start-Sleep -Milliseconds 300
 Assert-Position $h $x $y 'move'
 $saved=Get-ItemPropertyValue $config WindowPosition
 if([BitConverter]::ToInt32($saved,0) -ne $x -or [BitConverter]::ToInt32($saved,4) -ne $y){throw 'Move was not persisted immediately'}
 [PositionNative]::PostMessage($h,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 300
 $h=Open-Window;Assert-Position $h $x $y 'close and reopen in running worker'
 [PositionNative]::ShowWindow($h,6)|Out-Null;Start-Sleep -Milliseconds 200
 Stop-Worker
 $h=Open-Window;Assert-Position $h $x $y 'full process restart after minimize'
 Stop-Worker
 $bytes=[BitConverter]::GetBytes([int]-30000)+[BitConverter]::GetBytes([int]-30000)
 Set-ItemProperty $config WindowPosition -Value ([byte[]]$bytes)
 $h=Open-Window;$r=Rect $h
 if($r.Left -le -29000 -or $r.Top -le -29000){throw 'Disconnected monitor position was not recovered'}
 Write-Output 'PASS unavailable monitor recovers window onto visible work area'
} finally {
 Stop-Worker
 if($null -eq $original){Remove-ItemProperty $config WindowPosition -ErrorAction SilentlyContinue}else{Set-ItemProperty $config WindowPosition -Value $original}
 Start-Process -FilePath $App -ArgumentList '--background' -WindowStyle Hidden
}
