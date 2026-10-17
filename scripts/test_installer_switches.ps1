param([string]$Setup,[string]$Output="test-results/installer-switches-0.5.1")
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SettingsNative {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] private static extern IntPtr FindWindowNative(string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 public static IntPtr FindForProcess(string cls,int expected) {
   IntPtr hwnd=IntPtr.Zero;
   while((hwnd=FindWindowEx(IntPtr.Zero,hwnd,cls,null))!=IntPtr.Zero) {
     uint pid;GetWindowThreadProcessId(hwnd,out pid);if(pid==(uint)expected)return hwnd;
   }
   return IntPtr.Zero;
 }
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
 [DllImport("user32.dll",EntryPoint="GetWindowLongW")] public static extern int GetWindowLong(IntPtr hwnd,int index);
 public static IntPtr FindWindow(string cls,string title) { return FindWindowNative(String.IsNullOrEmpty(cls)?null:cls,String.IsNullOrEmpty(title)?null:title); }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Rect rect);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int cmd);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr hwnd,int id,uint mod,uint key);
 [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr hwnd,int id);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
 public static void ChangeDpi(IntPtr hwnd,int dpi) {
   Rect rect;GetWindowRect(hwnd,out rect);IntPtr memory=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(Rect)));
   try {Marshal.StructureToPtr(rect,memory,false);SendMessage(hwnd,0x2e0,new IntPtr(dpi|(dpi<<16)),memory);}
   finally {Marshal.FreeHGlobal(memory);}
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr OpenEvent(uint access,bool inherit,string name);
 [DllImport("kernel32.dll")] public static extern bool SetEvent(IntPtr handle);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
[SettingsNative]::SetProcessDpiAwarenessContext([IntPtr](-4))|Out-Null
New-Item -ItemType Directory -Path $Output -Force|Out-Null
$Output=(Resolve-Path $Output).Path
function Click($hwnd,$x,$y) {
    $scale=[SettingsNative]::GetDpiForWindow($hwnd)/96.0
    $point=([int]($x*$scale) -band 65535) -bor (([int]($y*$scale) -band 65535) -shl 16)
    [SettingsNative]::PostMessage($hwnd,0x202,[IntPtr]::Zero,[IntPtr]$point)|Out-Null
    Start-Sleep -Milliseconds 250
}
function Shot($hwnd,$name) {
    $rect=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($hwnd,[ref]$rect)|Out-Null
    $bitmap=[Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
    $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
    try {[SettingsNative]::PrintWindow($hwnd,$dc,2)|Out-Null}finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
    $path=Join-Path $Output "$name.png";$bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png)
    $pixel=$bitmap.GetPixel(20,300);$bitmap.Dispose()
    return @{Hash=(Get-FileHash $path).Hash;Pixel=$pixel}
}
$process=Start-Process -FilePath (Resolve-Path $Setup).Path -WindowStyle Hidden -PassThru
$hwnd=[IntPtr]::Zero
try {
    for($i=0;$i -lt 100;$i++) {
        $hwnd=[SettingsNative]::FindForProcess('ScreenshotPremiumSetup',$process.Id)
        if($hwnd -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 50
    }
    if($hwnd -eq [IntPtr]::Zero){throw 'Installer window missing'}
    [SettingsNative]::ShowWindow($hwnd,9)|Out-Null;Start-Sleep -Milliseconds 400
    $welcome=Shot $hwnd 'welcome-dark-ru'
    Click $hwnd 80 356
    $ru=Shot $hwnd 'preferences-dark-ru'
    Click $hwnd 300 278
    $en=Shot $hwnd 'preferences-dark-en'
    if($ru.Hash -eq $en.Hash){throw 'Language switch did not change the UI'}
    Click $hwnd 100 278
    $ruAgain=Shot $hwnd 'preferences-dark-ru-again'
    # Focus feedback is expected to differ from the initial unselected state.
    Click $hwnd 300 186
    $light=Shot $hwnd 'preferences-light-ru'
    if($light.Pixel.R -ne 245){throw 'Theme switch failed'}
    Click $hwnd 300 278
    $lightEn=Shot $hwnd 'preferences-light-en'
    Click $hwnd 500 186
    $darkEn=Shot $hwnd 'preferences-dark-en-again'
    if($darkEn.Pixel.R -ne 10){throw 'Dark selection failed'}
    Click $hwnd 100 350
    Shot $hwnd 'welcome-dark-en'|Out-Null
    Click $hwnd 80 356
    Click $hwnd 100 186
    Shot $hwnd 'preferences-system-en'|Out-Null
    Click $hwnd 300 186
    Click $hwnd 100 350
    Click $hwnd 320 341
    Start-Sleep -Seconds 4
    $installed=Shot $hwnd 'installed-light-en'
    if($installed.Pixel.R -ne 245){throw 'Installation did not preserve light theme'}
    Click $hwnd 320 340
    $hotkey=Shot $hwnd 'hotkey-light-en'
    if($hotkey.Hash -eq $installed.Hash -or $hotkey.Pixel.R -ne 245){throw 'Continue failed'}
    Click $hwnd 120 216
    [SettingsNative]::PostMessage($hwnd,0x100,[IntPtr]0x2c,[IntPtr]::Zero)|Out-Null
    Start-Sleep -Milliseconds 200
    Click $hwnd 510 350
    Shot $hwnd 'confirmation-light-en'|Out-Null
    Click $hwnd 100 346
    Shot $hwnd 'cancelled-confirmation-light-en'|Out-Null
    if(![SettingsNative]::RegisterHotKey([IntPtr]::Zero,992,0,0x85)){throw 'F22 fixture occupied'}
    try {
        Click $hwnd 120 216
        [SettingsNative]::PostMessage($hwnd,0x100,[IntPtr]0x85,[IntPtr]::Zero)|Out-Null
        Start-Sleep -Milliseconds 150
        Click $hwnd 510 350
        Shot $hwnd 'custom-shortcut-conflict-notice'|Out-Null
        [SettingsNative]::PostMessage($hwnd,0x100,[IntPtr]13,[IntPtr]::Zero)|Out-Null
        Start-Sleep -Milliseconds 200
        Shot $hwnd 'notice-dismissed'|Out-Null
    } finally {[SettingsNative]::UnregisterHotKey([IntPtr]::Zero,992)|Out-Null}
    [SettingsNative]::PostMessage($hwnd,0x100,[IntPtr]9,[IntPtr]::Zero)|Out-Null
    Start-Sleep -Milliseconds 200
    Shot $hwnd 'keyboard-focus'|Out-Null
    if(([SettingsNative]::GetWindowLong($hwnd,-16) -band 0x10000) -ne 0){throw 'Unexpected maximize button'}
    Click $hwnd 550 26
    if(![SettingsNative]::IsIconic($hwnd)){throw 'Installer minimize failed'}
    [SettingsNative]::ShowWindow($hwnd,9)|Out-Null
    [SettingsNative]::PostMessage($hwnd,0x104,[IntPtr]0x73,[IntPtr](1 -shl 29))|Out-Null
    if(!$process.WaitForExit(3000)){throw 'Alt F4 did not close installer'}
    'PASS actual update: preferences RU EN, System Light Dark, light English wizard and confirmation cancellation'
} finally {
    if($hwnd -ne [IntPtr]::Zero){[SettingsNative]::PostMessage($hwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
    if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}
}
