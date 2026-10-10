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
    $ru=Shot $hwnd 'dark-ru'
    Click $hwnd 53 356
    $en=Shot $hwnd 'dark-en'
    if($ru.Hash -eq $en.Hash){throw 'Language switch did not change the UI'}
    Click $hwnd 53 356
    $ruAgain=Shot $hwnd 'dark-ru-again'
    if($ruAgain.Hash -ne $ru.Hash){throw 'EN to RU did not restore the original UI'}
    Click $hwnd 498 27
    $light=Shot $hwnd 'light-ru'
    if($light.Pixel.R -ne 245){throw 'Theme switch failed'}
    Click $hwnd 53 356
    $lightEn=Shot $hwnd 'light-en'
    if($lightEn.Hash -eq $light.Hash){throw 'Language switch failed in light theme'}
    Click $hwnd 498 27
    $darkEn=Shot $hwnd 'dark-en-again'
    if($darkEn.Hash -ne $en.Hash){throw 'Theme switch did not preserve English'}
    Click $hwnd 498 27
    Click $hwnd 320 341
    for($i=0;$i -lt 100;$i++) {
        Start-Sleep -Milliseconds 100
        $installed=Shot $hwnd 'installed-light-en'
        # Successful completion has the centered Continue button at y=340.
        # Wait for installation to finish before advancing to the shortcut screen.
        if($i -ge 30){break}
    }
    if($installed.Pixel.R -ne 245){throw 'Installation did not preserve light theme'}
    Click $hwnd 320 340
    $hotkey=Shot $hwnd 'hotkey-light-en'
    if($hotkey.Hash -eq $installed.Hash -or $hotkey.Pixel.R -ne 245){throw 'Continue did not preserve the selected theme or advance'}
    'PASS actual update: light English installer -> installation complete -> shortcut screen'
    'PASS actual welcome screen clicks: RU -> EN -> RU, dark -> light -> dark, independent theme and language'
} finally {
    if($hwnd -ne [IntPtr]::Zero){[SettingsNative]::PostMessage($hwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
    if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}
}
