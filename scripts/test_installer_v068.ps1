param([Parameter(Mandatory=$true)][string]$Setup,[string]$Output='test-results/installer-0.6.8')
$ErrorActionPreference='Stop'
$source=Get-Content scripts/test_installer_switches.ps1 -Raw
$a=$source.IndexOf('Add-Type -AssemblyName');$b=$source.IndexOf('$process=Start-Process');Invoke-Expression $source.Substring($a,$b-$a)
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class PathNative {
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr hwnd,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd,System.Text.StringBuilder text,int length);
}
'@
$Setup=(Resolve-Path $Setup).Path
$key='HKCU:\Software\SanderStripa\Screenshot';$uninstall='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshot';$backup=@{}
foreach($n in 'HotkeyVK','HotkeyModifiers','ThemeMode','ThemeLight','LanguageEN'){try{$backup[$n]=Get-ItemPropertyValue $key $n -ErrorAction Stop}catch{$backup[$n]=$null}}
$original=Get-ItemPropertyValue $uninstall InstallLocation
$custom=Join-Path $PWD 'test-results\custom-install\Приложение Screenshot'
New-Item -ItemType Directory -Path $custom -Force|Out-Null
Set-Content (Join-Path $custom 'keep-user-file.txt') 'preserve me'
function Find-Setup($p){for($i=0;$i -lt 100;$i++){$h=[SettingsNative]::FindForProcess('ScreenshotPremiumSetup',$p.Id);if($h -ne [IntPtr]::Zero){[SettingsNative]::ShowWindow($h,4)|Out-Null;[SettingsNative]::SetWindowPos($h,[IntPtr]1,0,0,0,0,0x13)|Out-Null;Start-Sleep -Milliseconds 200;return $h};Start-Sleep -Milliseconds 70};throw 'Missing setup window'}
function Stop-Worker{$e=[SettingsNative]::OpenEvent(2,$false,'Local\Screenshot.SanderStripa.Exit');if($e -ne [IntPtr]::Zero){[SettingsNative]::SetEvent($e)|Out-Null;[SettingsNative]::CloseHandle($e)|Out-Null};Start-Sleep -Milliseconds 600}
function Run-Install([string]$target,[bool]$dark,[bool]$en,[bool]$picker=$false){
 $p=Start-Process $Setup -PassThru -WindowStyle Hidden
 try{
  $h=Find-Setup $p;Click $h 100 370
  $edit=[PathNative]::GetDlgItem($h,1001);if($edit -eq [IntPtr]::Zero){throw 'Editable installation folder missing'}
  if($picker){
   $before=[Text.StringBuilder]::new(32768);[PathNative]::GetWindowText($edit,$before,32768)|Out-Null
   Click $h 750 202;Start-Sleep -Milliseconds 350
   $dialog=[SettingsNative]::FindForProcess('#32770',$p.Id);if($dialog -eq [IntPtr]::Zero){throw 'Browse folder dialog missing'}
   [SettingsNative]::PostMessage($dialog,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 200
   $after=[Text.StringBuilder]::new(32768);[PathNative]::GetWindowText($edit,$after,32768)|Out-Null
   if($before.ToString() -ne $after.ToString()){throw 'Cancelling browse changed path'}
   Write-Output 'PASS native folder picker opens and cancellation preserves path'
  }
  if(![PathNative]::SetWindowText($edit,$target)){throw 'Could not set folder edit text'}
  $entered=[Text.StringBuilder]::new(32768);[PathNative]::GetWindowText($edit,$entered,32768)|Out-Null
  if($entered.ToString() -ne $target){throw "Folder edit differs: $entered"}
  Click $h $(if($dark){400}else{330}) 130;Click $h $(if($en){176}else{136}) 130
  $entered.Clear()|Out-Null;[PathNative]::GetWindowText([PathNative]::GetDlgItem($h,1001),$entered,32768)|Out-Null
  if($entered.ToString() -ne $target){throw "Folder changed after theme/language: $entered"}
  Shot $h "preferences-$dark-$en"|Out-Null
  Click $h 170 380;Start-Sleep -Seconds 4
  $actual=Get-ItemPropertyValue $uninstall InstallLocation
  if($actual -ne $target -or !(Test-Path (Join-Path $target 'Screenshot.exe'))){throw "Incorrect installation target $actual"}
  if((Get-ItemPropertyValue $key ThemeMode) -ne $(if($dark){2}else{1}) -or (Get-ItemPropertyValue $key LanguageEN) -ne [int]$en){throw 'Installer appearance not inherited'}
  $run=Get-ItemPropertyValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' Screenshot
  if(!$run.StartsWith('"'+(Join-Path $target 'Screenshot.exe')+'"')){throw 'Autostart points to wrong path'}
  $shell=New-Object -ComObject WScript.Shell
  foreach($folder in 'Programs','Desktop'){$link=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath($folder)) 'Screenshot.lnk'));if($link.TargetPath -ne (Join-Path $target 'Screenshot.exe')){throw 'Shortcut points to wrong target'}}
  if(!(Get-ItemPropertyValue $uninstall UninstallString).Contains((Join-Path $target 'Uninstall.exe'))){throw 'Wrong uninstall path'}
  Click $h 420 294;Stop-Worker;Click $h 120 220;[SettingsNative]::PostMessage($h,0x100,[IntPtr]0x86,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 150;Click $h 650 350;Start-Sleep -Milliseconds 600;Click $h 420 360
  if(!$p.WaitForExit(5000)){throw 'Done failed'}
  $app=[SettingsNative]::FindWindow('ScreenshotSettings',$null);if($app -eq [IntPtr]::Zero){throw 'Application did not open from selected directory'}
  $shot=Shot $app "application-$dark-$en";if($shot.Pixel.R -ne $(if($dark){12}else{247})){throw 'Application theme differs from installer'}
  [SettingsNative]::PostMessage($app,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Write-Output "PASS install/update at $target; inherited dark=$dark English=$en; launch/autostart/shortcuts/uninstaller"
 }finally{if(!$p.HasExited){Stop-Process -Id $p.Id -ErrorAction SilentlyContinue}}
}
try {
 Run-Install $custom $true $true $true
 if(Test-Path (Join-Path $original 'Screenshot.exe')){throw 'Old executable remains after migration'}
 Run-Install $custom $true $true
 Run-Install $original $false $false
 if(!(Test-Path (Join-Path $custom 'keep-user-file.txt'))){throw 'Relocation removed unrelated files'}
 Write-Output 'PASS relocation preserves unrelated files'
 Stop-Worker
 $remove=Start-Process (Join-Path $original 'Uninstall.exe') -ArgumentList '--uninstall' -WindowStyle Hidden -Wait -PassThru
 if($remove.ExitCode -ne 0 -or (Test-Path (Join-Path $original 'Screenshot.exe'))){throw 'Uninstall failed'}
 $restore=Start-Process $Setup -ArgumentList '--integration-test' -WindowStyle Hidden -Wait -PassThru
 if($restore.ExitCode -ne 0){throw 'Restore clean install failed'}
 Write-Output 'PASS uninstall and clean reinstall'
}finally{
 Stop-Worker
 foreach($n in $backup.Keys){if($null -eq $backup[$n]){Remove-ItemProperty $key $n -ErrorAction SilentlyContinue}else{Set-ItemProperty $key $n $backup[$n] -Type DWord}}
 if(Test-Path (Join-Path $original 'Screenshot.exe')){Start-Process (Join-Path $original 'Screenshot.exe') -ArgumentList '--background' -WindowStyle Hidden}
}
