param([Parameter(Mandatory=$true)][string]$Setup,[string]$Output='test-results/installer-0.6.9')
$ErrorActionPreference='Stop'
$source=Get-Content scripts/test_installer_v068.ps1 -Raw
$a=$source.IndexOf('$source=Get-Content');$b=$source.IndexOf('$key=');Invoke-Expression $source.Substring($a,$b-$a)
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class StableEdit {
 [DllImport("user32.dll",EntryPoint="SendMessageW")] public static extern IntPtr Message(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetFocus(IntPtr hwnd);
}
'@
foreach($light in $true,$false){foreach($en in $false,$true){
 $p=Start-Process $Setup -PassThru -WindowStyle Hidden
 try{
  $h=[IntPtr]::Zero;for($i=0;$i -lt 80;$i++){$h=[SettingsNative]::FindForProcess('ScreenshotPremiumSetup',$p.Id);if($h -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
  if($h -eq [IntPtr]::Zero){throw 'Missing installer'}
  [SettingsNative]::ShowWindow($h,4)|Out-Null;Click $h 100 370
  if(!$light){Click $h 400 130};if($en){Click $h 152 130}
  $edit=[PathNative]::GetDlgItem($h,1001)
  $r=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($edit,[ref]$r)|Out-Null
  $text=[Text.StringBuilder]::new(32768);[PathNative]::GetWindowText($edit,$text,32768)|Out-Null
  [PathNative]::SetWindowText($edit,('C:\Screenshot stable path\'+('a'*90)))|Out-Null
  [StableEdit]::Message($edit,0xb1,[IntPtr]31,[IntPtr]31)|Out-Null
  $selection=[StableEdit]::Message($edit,0xb0,[IntPtr]::Zero,[IntPtr]::Zero)
  for($i=0;$i -lt 100;$i++){$x=310+($i%200);$y=120+($i%70);[SettingsNative]::PostMessage($h,0x200,[IntPtr]::Zero,[IntPtr]($x-bor($y-shl 16)))|Out-Null}
  Start-Sleep -Milliseconds 400
  $after=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($edit,[ref]$after)|Out-Null
  if($r.Left-ne$after.Left-or$r.Top-ne$after.Top-or$r.Right-ne$after.Right-or$r.Bottom-ne$after.Bottom){throw 'Path moved during mouse motion'}
  if([StableEdit]::Message($edit,0xb0,[IntPtr]::Zero,[IntPtr]::Zero) -ne $selection){throw 'Path selection jumped'}
  [PathNative]::SetWindowText($edit,$text.ToString())|Out-Null
  Shot $h "settings-light-$light-English-$en"|Out-Null
  Write-Output "PASS stable edit and settings light=$light English=$en"
 }finally{if(!$p.HasExited){Stop-Process -Id $p.Id}}
}}
