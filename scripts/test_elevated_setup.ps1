param([Parameter(Mandatory=$true)][string]$Setup,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ElevationNative {
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] private static extern IntPtr FindWindowNative(string cls,string title);
 public static IntPtr FindWindow(string cls,string title){return FindWindowNative(cls,String.IsNullOrEmpty(title)?null:title);}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 public static IntPtr ForPID(int pid){IntPtr h=IntPtr.Zero;while((h=FindWindowEx(IntPtr.Zero,h,"ScreenshotPremiumSetup",null))!=IntPtr.Zero)if(WindowPID(h)==pid)return h;return IntPtr.Zero;}
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int command);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("advapi32.dll")] public static extern bool OpenProcessToken(IntPtr process,uint access,out IntPtr token);
 [DllImport("advapi32.dll")] public static extern bool GetTokenInformation(IntPtr token,int cls,out int value,int size,out int bytes);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr OpenEvent(uint access,bool inherit,string name);
 [DllImport("kernel32.dll")] public static extern bool SetEvent(IntPtr h);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr h,int id,uint mod,uint vk);
 [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr h,int id);
 public static int Elevated(int pid) {
   IntPtr process=OpenProcess(0x1000,false,pid),token;int value=0,bytes;
   if(process==IntPtr.Zero)return -1;
   try {if(!OpenProcessToken(process,8,out token))return -1;
     try {return GetTokenInformation(token,20,out value,4,out bytes)?value:-1;}
     finally {CloseHandle(token);}
   } finally {CloseHandle(process);}
 }
 public static int WindowPID(IntPtr h){uint pid;GetWindowThreadProcessId(h,out pid);return (int)pid;}
}
'@
$Setup=(Resolve-Path -LiteralPath $Setup).Path
New-Item -ItemType Directory -Path $Output -Force|Out-Null
$Output=(Resolve-Path -LiteralPath $Output).Path
$config='HKCU:\Software\SanderStripa\Screenshot'
$original=@{}
foreach($name in 'HotkeyVK','HotkeyModifiers') {
    try {$original[$name]=Get-ItemPropertyValue $config $name -ErrorAction Stop}catch {$original[$name]=$null}
}
$app=Join-Path $env:LOCALAPPDATA 'Programs\Screenshot\Screenshot.exe'
$reports=[Collections.Generic.List[string]]::new()
function Report([string]$text){$reports.Add("PASS $text")}
function Stop-Worker {
    $controller=[ElevationNative]::FindWindow('ScreenshotController','Screenshot')
    $workerID=[ElevationNative]::WindowPID($controller)
    $event=[ElevationNative]::OpenEvent(2,$false,'Local\Screenshot.SanderStripa.Exit')
    if($event -ne [IntPtr]::Zero){[ElevationNative]::SetEvent($event)|Out-Null;[ElevationNative]::CloseHandle($event)|Out-Null}
    if($workerID){try {Get-Process -Id $workerID -ErrorAction Stop|Wait-Process -Timeout 10 -ErrorAction Stop}catch {if(Get-Process -Id $workerID -ErrorAction SilentlyContinue){throw 'Worker did not stop cooperatively'}}}
    Start-Sleep -Milliseconds 200
}
function Find-Window([string]$cls) {
    for($i=0;$i -lt 100;$i++) {
        $h=[ElevationNative]::FindWindow($cls,$null)
        if($h -ne [IntPtr]::Zero){return $h}
        Start-Sleep -Milliseconds 100
    }
    throw "Missing $cls"
}
function Click($h,[int]$x,[int]$y) {
    $scale=[ElevationNative]::GetDpiForWindow($h)/96.0
    $point=([int]($x*$scale) -band 65535) -bor (([int]($y*$scale) -band 65535) -shl 16)
    [ElevationNative]::PostMessage($h,0x202,[IntPtr]::Zero,[IntPtr]$point)|Out-Null
    Start-Sleep -Milliseconds 250
}
$wizard=$null
try {
    if([ElevationNative]::Elevated($PID) -ne 1){throw 'This regression test must run elevated'}
    Stop-Worker
    $install=Start-Process -FilePath $Setup -ArgumentList '--integration-test' -WindowStyle Hidden -Wait -PassThru
    if($install.ExitCode -ne 0){throw 'Update over elevated previous version failed'}
    Report 'update over the previous elevated installation'
    $high=Start-Process -FilePath $app -ArgumentList '--background' -WindowStyle Hidden -PassThru
    $controller=Find-Window 'ScreenshotController'
    if([ElevationNative]::Elevated($high.Id) -ne 1){throw 'Elevated worker fixture did not start elevated'}
    [pscustomobject]@{phase='ready';pid=$high.Id}|ConvertTo-Json|Set-Content (Join-Path $Output 'phase.json') -Encoding utf8
    for($i=0;$i -lt 120;$i++) {
        if(Test-Path (Join-Path $Output 'ordinary-launch-passed.txt')){break}
        if(Test-Path (Join-Path $Output 'ordinary-launch-failed.txt')){throw 'Ordinary Start menu launch failed'}
        Start-Sleep -Seconds 1
    }
    if(!(Test-Path (Join-Path $Output 'ordinary-launch-passed.txt'))){throw 'Timed out waiting for ordinary launch check'}
    $settings=Find-Window 'ScreenshotSettings'
    if(![ElevationNative]::IsWindowVisible($settings)){throw 'Elevated settings did not open'}
    Report 'ordinary Start menu request opens settings in an elevated worker (UIPI regression)'
    Stop-Worker
    $key=0
    foreach($vk in 0x87,0x86,0x85,0x84) {
        if([ElevationNative]::RegisterHotKey([IntPtr]::Zero,992,0,$vk)){[ElevationNative]::UnregisterHotKey([IntPtr]::Zero,992)|Out-Null;$key=$vk;break}
    }
    if(!$key){throw 'No free function-key fixture'}
    $wizard=Start-Process -FilePath $Setup -WindowStyle Hidden -PassThru
    # Enumerate only this newly launched wizard, never an already open user installer.
    for($i=0;$i -lt 100;$i++) {$h=[ElevationNative]::ForPID($wizard.Id);if($h -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
    if($h -eq [IntPtr]::Zero){throw 'Elevated wizard missing'}
    [ElevationNative]::ShowWindow($h,9)|Out-Null
    Click $h 320 341
    Start-Sleep -Seconds 3
    Click $h 320 340
    Click $h 120 216
    [ElevationNative]::PostMessage($h,0x100,[IntPtr]$key,[IntPtr]::Zero)|Out-Null
    Start-Sleep -Milliseconds 200
    Click $h 510 350
    $controller=Find-Window 'ScreenshotController'
    $normalID=[ElevationNative]::WindowPID($controller)
    $actualElevation=[ElevationNative]::Elevated($normalID)
    if($actualElevation -ne 0){throw "Elevated setup child PID $normalID has elevation $actualElevation"}
    Report 'administrator-run wizard launches the app with the ordinary Explorer token'
    Click $h 320 340
    if(!$wizard.WaitForExit(5000)){throw 'Finish did not close the wizard'}
    $settings=Find-Window 'ScreenshotSettings'
    if(![ElevationNative]::IsWindowVisible($settings)){throw 'Finish did not open the application'}
    Report 'finish opens the installed Screenshot settings'
    Stop-Worker
    $reports|Set-Content (Join-Path $Output 'elevated-tests.txt') -Encoding utf8
    [pscustomobject]@{success=$true;reports=$reports}|ConvertTo-Json|Set-Content (Join-Path $Output 'result.json') -Encoding utf8
} catch {
    [pscustomobject]@{success=$false;error=$_.Exception.Message}|ConvertTo-Json|Set-Content (Join-Path $Output 'result.json') -Encoding utf8
    throw
} finally {
    Stop-Worker
    if($wizard -and !$wizard.HasExited){Stop-Process -Id $wizard.Id}
    foreach($name in $original.Keys) {
        if($null -eq $original[$name]){Remove-ItemProperty $config $name -ErrorAction SilentlyContinue}
        else {Set-ItemProperty $config $name $original[$name] -Type DWord}
    }
}
