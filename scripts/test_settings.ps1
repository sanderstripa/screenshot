param([Parameter(Mandatory=$true)][string]$App, [string]$Output='test-results/settings')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SettingsNative {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] private static extern IntPtr FindWindowNative(string cls,string title);
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
 public static int HitTest(IntPtr hwnd,int x,int y) {return SendMessage(hwnd,0x84,IntPtr.Zero,new IntPtr((x&65535)|((y&65535)<<16))).ToInt32();}
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
[SettingsNative]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$App=(Resolve-Path -LiteralPath $App).Path
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Output=(Resolve-Path -LiteralPath $Output).Path
$config='HKCU:\Software\SanderStripa\Screenshot'
$original=@{}
function Optional-Value([string]$path,[string]$name) {
    try {Get-ItemPropertyValue -Path $path -Name $name -ErrorAction Stop} catch {$null}
}
foreach($name in 'HotkeyVK','HotkeyModifiers','ThemeLight','ThemeMode','LanguageEN') {
    $original[$name]=Optional-Value $config $name
}
$snipping=Optional-Value 'HKCU:\Control Panel\Keyboard' 'PrintScreenKeyForSnippingEnabled'
$programs=[Environment]::GetFolderPath('Programs')
$linkFile=Join-Path $programs 'Screenshot.lnk'
if(!(Test-Path -LiteralPath $linkFile)){throw 'Start menu shortcut missing'}
$shell=New-Object -ComObject WScript.Shell
$link=$shell.CreateShortcut($linkFile)
if($link.TargetPath -ne $App -or $link.Arguments -ne '--settings' -or !$link.IconLocation.StartsWith($App)) {
    throw 'Start menu target, settings argument or icon is wrong'
}
$run=Get-ItemPropertyValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'Screenshot'
if(!$run.Contains('--background')){throw 'Login autostart would open a window'}
$reports=[Collections.Generic.List[string]]::new()
function Report([string]$message){$reports.Add("PASS $message");Write-Output "PASS $message"}
function Stop-App {
    $event=[SettingsNative]::OpenEvent(2,$false,'Local\Screenshot.SanderStripa.Exit')
    if($event -ne [IntPtr]::Zero){[SettingsNative]::SetEvent($event)|Out-Null;[SettingsNative]::CloseHandle($event)|Out-Null}
    Start-Sleep -Milliseconds 700
}
function Find-Settings {
    for($i=0;$i -lt 60;$i++){
        $hwnd=[SettingsNative]::FindWindow('ScreenshotSettings',$null)
        if($hwnd -ne [IntPtr]::Zero -and [SettingsNative]::IsWindowVisible($hwnd)){return $hwnd}
        Start-Sleep -Milliseconds 100
    }
    $found=[SettingsNative]::FindWindow('ScreenshotSettings',$null)
    $worker.Refresh()
    throw "Visible settings window not found: HWND=$found, main window=$($worker.MainWindowTitle), exited=$($worker.HasExited)"
}
function Click([IntPtr]$hwnd,[int]$x,[int]$y,[double]$scale=0) {
    if(!$scale){$scale=[SettingsNative]::GetDpiForWindow($hwnd)/96.0}
    $point=([int]($x*$scale) -band 65535) -bor (([int]($y*$scale) -band 65535) -shl 16)
    [SettingsNative]::PostMessage($hwnd,0x202,[IntPtr]::Zero,[IntPtr]$point)|Out-Null
    Start-Sleep -Milliseconds 200
}
function Key([IntPtr]$hwnd,[int]$key) {
    [SettingsNative]::PostMessage($hwnd,0x100,[IntPtr]$key,[IntPtr]::Zero)|Out-Null
    Start-Sleep -Milliseconds 150
}
function Shot([IntPtr]$hwnd,[string]$name,[double]$scale=0) {
    $rect=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($hwnd,[ref]$rect)|Out-Null
    $bitmap=[Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
    $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
    try {if(![SettingsNative]::PrintWindow($hwnd,$dc,2)){throw 'Native rendering capture failed'}}
    finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
    $bitmap.Save((Join-Path $Output "$name.png"),[Drawing.Imaging.ImageFormat]::Png)
    if(!$scale){$scale=[SettingsNative]::GetDpiForWindow($hwnd)/96.0}
    $pixel=$bitmap.GetPixel([int](20*$scale),[int](95*$scale))
    $bitmap.Dispose();return $pixel
}
function Theme([int]$mode,[double]$scale=0) {
    Click $hwnd 476 116 $scale
    Click $hwnd (102+156*$mode) 194 $scale
    Click $hwnd 100 350 $scale
}
try {
    Stop-App
    New-Item -Path $config -Force | Out-Null
    Set-ItemProperty $config HotkeyVK 0x87 -Type DWord
    Set-ItemProperty $config HotkeyModifiers 0 -Type DWord
    Set-ItemProperty $config ThemeLight 0 -Type DWord
    Set-ItemProperty $config ThemeMode 2 -Type DWord
    Set-ItemProperty $config LanguageEN 0 -Type DWord
    $worker=Start-Process -FilePath $App -ArgumentList '--background' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 800
    if($worker.HasExited){throw 'Background worker exited'}
    if([SettingsNative]::FindWindow('ScreenshotSettings',$null) -ne [IntPtr]::Zero){throw 'Autostart opened settings'}
    Report 'background launch stays invisible without a tray'
    $controller=[SettingsNative]::FindWindow('ScreenshotController','Screenshot')
    if($controller -eq [IntPtr]::Zero){throw 'IPC controller window missing'}
    $open=Start-Process -FilePath $linkFile -PassThru
    if($open) {
        $open.WaitForExit(5000)|Out-Null
        if(!$open.HasExited -or $open.ExitCode -ne 0){throw 'Start menu launch did not forward to existing process'}
    }
    $hwnd=Find-Settings
    $dark=Shot $hwnd 'settings-dark'
    if($dark.R -ne 10 -or $dark.G -ne 10 -or $dark.B -ne 10){throw 'Dark reference color mismatch'}
    Report 'Start menu settings open inside the existing background process'
    Theme 1
    $light=Shot $hwnd 'settings-light'
    if($light.R -ne 245 -or $light.G -ne 245 -or $light.B -ne 245){throw 'Light reference color mismatch'}
    if((Get-ItemPropertyValue $config ThemeLight) -ne 1){throw 'Theme not saved'}
    foreach($state in 'dark','light') {
        $bitmap=[Drawing.Bitmap]::new((Join-Path $Output "settings-$state.png"))
        try {
            $pixel=$bitmap.GetPixel([int](350*$bitmap.Width/520),[int](340*$bitmap.Width/520))
            if($pixel.R -ne $(if($state -eq 'light'){17}else{244}) -or $pixel.G -ne $pixel.R -or $pixel.B -ne $pixel.R){throw 'Primary button palette mismatch'}
        } finally {$bitmap.Dispose()}
    }
    Report 'preferences switch #0A0A0A / #F5F5F5 and save the theme'
    # Reassign to F23 while the original worker remains alive.
    Click $hwnd 130 212;Key $hwnd 0x86;Click $hwnd 410 350
    if((Get-ItemPropertyValue $config HotkeyVK) -ne 0x86){throw 'New key not saved'}
    if([SettingsNative]::RegisterHotKey([IntPtr]::Zero,980,0,0x86)) {
        [SettingsNative]::UnregisterHotKey([IntPtr]::Zero,980)|Out-Null;throw 'New shortcut not registered by the running app'
    }
    if(![SettingsNative]::RegisterHotKey([IntPtr]::Zero,981,0,0x87)){throw 'Old shortcut was not released'}
    [SettingsNative]::UnregisterHotKey([IntPtr]::Zero,981)|Out-Null
    if($worker.HasExited){throw 'Changing shortcut restarted or stopped the worker'}
    Report 'F24 -> F23 applies live: new key registered, old key released, same process'
    # Reserve F22 in this test process and prove a conflicting choice leaves F23 intact.
    if(![SettingsNative]::RegisterHotKey([IntPtr]::Zero,982,0,0x85)){throw 'Test key F22 already in use'}
    try {
        Click $hwnd 130 212;Key $hwnd 0x85;Click $hwnd 410 350
        if((Get-ItemPropertyValue $config HotkeyVK) -ne 0x86){throw 'Conflicting shortcut overwrote working configuration'}
        Shot $hwnd 'shortcut-conflict'|Out-Null
    } finally {[SettingsNative]::UnregisterHotKey([IntPtr]::Zero,982)|Out-Null}
    Report 'occupied shortcut is rejected without changing the working shortcut'
    Click $hwnd 130 212;Key $hwnd 0x2c;Click $hwnd 410 350
    Shot $hwnd 'print-screen-confirmation'|Out-Null
    if((Get-ItemPropertyValue $config HotkeyVK) -ne 0x86){throw 'Print Screen was saved before consent'}
    $now=Optional-Value 'HKCU:\Control Panel\Keyboard' 'PrintScreenKeyForSnippingEnabled'
    if($now -ne $snipping){throw 'Windows Print Screen changed before consent'}
    Click $hwnd 100 350
    Report 'Print Screen requires explicit consent; cancellation preserves Windows action and current key'
    [SettingsNative]::PostMessage($hwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    Start-Sleep -Milliseconds 350
    if($worker.HasExited){throw 'Closing settings stopped screenshot capture'}
    Start-Process -FilePath $App -ArgumentList '--settings' -WindowStyle Hidden -Wait | Out-Null
    $hwnd=Find-Settings
    $reopened=Shot $hwnd 'settings-reopened'
    if($reopened.R -ne 245){throw 'Theme not restored when reopening settings'}
    Report 'closing settings keeps the worker; Start menu reopens with saved theme and shortcut'
    Stop-App
    # A restart verifies persisted values, rather than only in-memory state.
    $worker=Start-Process -FilePath $App -ArgumentList '--background' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 650
    Start-Process -FilePath $App -ArgumentList '--settings' -WindowStyle Hidden -Wait | Out-Null
    $hwnd=Find-Settings
    if((Shot $hwnd 'settings-after-restart').R -ne 245){throw 'Theme lost after restart'}
    Report 'theme and shortcut survive a complete process restart'
    foreach($dpi in 96,120,144,192,240) {
        [SettingsNative]::ChangeDpi($hwnd,$dpi)
        Start-Sleep -Milliseconds 150
        $rect=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($hwnd,[ref]$rect)|Out-Null
        if(($rect.Right-$rect.Left) -ne [int](520*$dpi/96) -or ($rect.Bottom-$rect.Top) -ne [int][Math]::Truncate(382*$dpi/96)) {
            throw "Settings layout dimensions incorrect at DPI $dpi"
        }
        $pixel=Shot $hwnd "settings-light-dpi-$dpi" ($dpi/96.0)
        if($pixel.R -ne 245){throw "Light rendering incorrect at DPI $dpi"}
        Theme 2 ($dpi/96.0)
        $pixel=Shot $hwnd "settings-dark-dpi-$dpi" ($dpi/96.0)
        if($pixel.R -ne 10){throw "Dark rendering incorrect at DPI $dpi"}
        Theme 1 ($dpi/96.0)
        Report "both themes render correctly and remain clickable at $([int]($dpi/96*100))% scaling"
    }
    [SettingsNative]::ChangeDpi($hwnd,96)
    Start-Sleep -Milliseconds 150
    Click $hwnd 476 116
    Click $hwnd 240 282
    if((Get-ItemPropertyValue $config LanguageEN) -ne 1){throw 'English language switch did not save'}
    Shot $hwnd 'preferences-english'|Out-Null
    Click $hwnd 100 350
    Shot $hwnd 'settings-english'|Out-Null
    Key $hwnd 9
    Key $hwnd 32
    Key $hwnd 27
    Theme 0
    $expected=Optional-Value 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' 'AppsUseLightTheme'
    if($null -eq $expected){$expected=1}
    [SettingsNative]::PostMessage($hwnd,0x1a,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    Start-Sleep -Milliseconds 200
    $system=Shot $hwnd 'settings-system-theme'
    if($system.R -ne $(if($expected){245}else{10})){throw 'System theme does not follow Windows'}
    if((Get-ItemPropertyValue $config ThemeMode) -ne 0){throw 'System mode was not saved'}
    $bounds=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($hwnd,[ref]$bounds)|Out-Null
    if([SettingsNative]::HitTest($hwnd,($bounds.Left+100),($bounds.Top+35)) -ne 2){throw 'Titlebar drag hit test failed'}
    if([SettingsNative]::HitTest($hwnd,($bounds.Left+441),($bounds.Top+35)) -ne 1){throw 'Minimize was captured by drag area'}
    Click $hwnd 441 35
    Start-Sleep -Milliseconds 200
    if(![SettingsNative]::IsIconic($hwnd)){throw 'Custom minimize failed'}
    [SettingsNative]::ShowWindow($hwnd,9)|Out-Null
    if(([SettingsNative]::GetWindowLong($hwnd,-16) -band 0x10000) -ne 0){throw 'Unexpected maximize button'}
    [SettingsNative]::PostMessage($hwnd,0x104,[IntPtr]0x73,[IntPtr](1 -shl 29))|Out-Null
    Start-Sleep -Milliseconds 300
    if([SettingsNative]::FindWindow('ScreenshotSettings',$null) -ne [IntPtr]::Zero){throw 'Alt F4 did not close settings'}
    if($worker.HasExited){throw 'Alt F4 stopped the worker'}
    Report 'working English settings, keyboard focus navigation, System theme and custom minimize'
    $reports | Set-Content (Join-Path $Output 'settings-tests.txt') -Encoding utf8
}
finally {
    Stop-App
    foreach($name in $original.Keys) {
        if($null -eq $original[$name]){Remove-ItemProperty $config $name -ErrorAction SilentlyContinue}
        else {Set-ItemProperty $config $name $original[$name] -Type DWord}
    }
}
