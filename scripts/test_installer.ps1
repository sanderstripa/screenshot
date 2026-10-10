param([string]$Setup,[string]$Output="test-results/installer-0.5.0")
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SettingsNative {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] private static extern IntPtr FindWindowNative(string cls,string title);
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
$Setup=(Resolve-Path $Setup).Path
foreach($dpi in 96,120,144,192,240) {
    foreach($step in 0..5) {
        $process=Start-Process -FilePath $Setup -ArgumentList "--preview $step $dpi" -WindowStyle Hidden -PassThru
        $hwnd=[IntPtr]::Zero
        try {
            for($i=0;$i -lt 100;$i++) {
                $hwnd=[SettingsNative]::FindWindow('ScreenshotPremiumSetup',$null)
                if($hwnd -ne [IntPtr]::Zero){break}
                if($process.HasExited){throw 'Installer preview exited'}
                Start-Sleep -Milliseconds 50
            }
            if($hwnd -eq [IntPtr]::Zero){throw 'Installer preview missing'}
            [SettingsNative]::ShowWindow($hwnd,9)|Out-Null
            Start-Sleep -Milliseconds 450
            $rect=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($hwnd,[ref]$rect)|Out-Null
            $w=$rect.Right-$rect.Left;$h=$rect.Bottom-$rect.Top
            if($w -ne [int](640*$dpi/96) -or $h -ne [int](400*$dpi/96)){throw 'Incorrect installer DPI dimensions'}
            $bitmap=[Drawing.Bitmap]::new($w,$h);$graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
            try {if(![SettingsNative]::PrintWindow($hwnd,$dc,2)){throw 'Installer rendering failed'}}
            finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
            $bitmap.Save((Join-Path $Output "stage-$step-dpi-$dpi.png"),[Drawing.Imaging.ImageFormat]::Png)
            $pixel=$bitmap.GetPixel([int](20*$dpi/96),[int](300*$dpi/96))
            if($pixel.R -ne 10 -or $pixel.G -ne 10 -or $pixel.B -ne 10){throw "Installer reference palette mismatch: $pixel"}
            $bitmap.Save((Join-Path $Output "stage-$step-dpi-$dpi.png"),[Drawing.Imaging.ImageFormat]::Png);$bitmap.Dispose()
            "PASS installer stage=$step dpi=$dpi size=${w}x${h}" | Tee-Object -FilePath (Join-Path $Output 'installer-tests.txt') -Append
        } finally {
            if($hwnd -ne [IntPtr]::Zero){[SettingsNative]::PostMessage($hwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
            if(!$process.WaitForExit(5000)){Stop-Process -Id $process.Id}
        }
    }
}


