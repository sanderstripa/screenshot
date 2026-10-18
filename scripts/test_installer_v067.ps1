param([Parameter(Mandatory=$true)][string]$Setup,[string]$Output='test-results/installer-0.6.7')
$ErrorActionPreference='Stop'
$source=Get-Content scripts/test_installer_switches.ps1 -Raw
$a=$source.IndexOf("Add-Type -AssemblyName");$b=$source.IndexOf('$process=Start-Process')
Invoke-Expression $source.Substring($a,$b-$a)
$Setup=(Resolve-Path $Setup).Path
function Find-Setup($p){for($i=0;$i -lt 100;$i++){$h=[SettingsNative]::FindForProcess('ScreenshotPremiumSetup',$p.Id);if($h -ne [IntPtr]::Zero){[SettingsNative]::ShowWindow($h,4)|Out-Null;[SettingsNative]::SetWindowPos($h,[IntPtr]1,0,0,0,0,0x13)|Out-Null;Start-Sleep -Milliseconds 200;return $h};Start-Sleep -Milliseconds 70};throw 'Missing setup window'}
function Stop-Worker{$e=[SettingsNative]::OpenEvent(2,$false,'Local\Screenshot.SanderStripa.Exit');if($e -ne [IntPtr]::Zero){[SettingsNative]::SetEvent($e)|Out-Null;[SettingsNative]::CloseHandle($e)|Out-Null};Start-Sleep -Milliseconds 600}
$cfg='HKCU:\Software\SanderStripa\Screenshot';$backup=@{}
foreach($n in 'HotkeyVK','HotkeyModifiers'){try{$backup[$n]=Get-ItemPropertyValue $cfg $n -ErrorAction Stop}catch{$backup[$n]=$null}}
$desktop=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Screenshot.lnk';$menu=Join-Path ([Environment]::GetFolderPath('Programs')) 'Screenshot.lnk'
try {
 foreach($dpi in 96,144,192){foreach($theme in 'light','dark'){foreach($step in 0..5){
  $p=Start-Process $Setup -ArgumentList "--preview $step $dpi $theme" -PassThru -WindowStyle Hidden
  try{$h=Find-Setup $p;Shot $h "preview-$step-$theme-$dpi"|Out-Null;$r=[SettingsNative+Rect]::new();[SettingsNative]::GetWindowRect($h,[ref]$r)|Out-Null;if($r.Right-$r.Left -ne 840*$dpi/96 -or $r.Bottom-$r.Top -ne 540*$dpi/96){throw 'Wrong DPI frame'}}finally{Stop-Process -Id $p.Id -ErrorAction SilentlyContinue}
 }}}
 Write-Output 'PASS six native stages, both themes at 100/150/200 percent DPI'
 foreach($mask in 0..3){
  $p=Start-Process $Setup -PassThru -WindowStyle Hidden
  try{
   $h=Find-Setup $p;Click $h 100 370
   if(!($mask-band 1)){Click $h 60 191};if(!($mask-band 2)){Click $h 60 229}
   $ru=Shot $h "settings-$mask-ru";Click $h 152 130;$en=Shot $h "settings-$mask-en";if($ru.Hash -eq $en.Hash){throw 'Language did not switch'}
   Click $h 400 130;$dark=Shot $h "settings-$mask-dark";if($dark.Pixel.R -ne 32){throw 'Dark palette failed'}
   Click $h 330 130;Click $h 170 350;Start-Sleep -Seconds 4
   if((Test-Path $desktop) -ne [bool]($mask-band 1) -or (Test-Path $menu) -ne [bool]($mask-band 2)){throw "Shortcut selection failed mask $mask"}
   Shot $h "installed-$mask"|Out-Null;Click $h 420 294;Shot $h "hotkey-$mask"|Out-Null
   if($mask -eq 0){Click $h 120 220;[SettingsNative]::PostMessage($h,0x100,[IntPtr]0x2c,[IntPtr]::Zero)|Out-Null;Click $h 650 350;Shot $h 'print-screen-consent'|Out-Null;Click $h 300 320;Shot $h 'consent-cancelled'|Out-Null}
   Stop-Worker;Click $h 120 220;[SettingsNative]::PostMessage($h,0x100,[IntPtr]0x86,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 150
   Click $h 650 350;Start-Sleep -Milliseconds 700;Shot $h "ready-$mask"|Out-Null
   if($mask -lt 3){Click $h 300 418}
   Click $h 420 360;if(!$p.WaitForExit(5000)){throw 'Done did not close installer'}
   $appWindow=[SettingsNative]::FindWindow('ScreenshotSettings',$null)
   if($mask -lt 3 -and $appWindow -ne [IntPtr]::Zero){throw 'Launch off opened application'}
   if($mask -eq 3 -and $appWindow -eq [IntPtr]::Zero){throw 'Launch on failed to open application'}
   if($appWindow -ne [IntPtr]::Zero){[SettingsNative]::PostMessage($appWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
   Write-Output "PASS actual update mask $mask, Desktop/Start menu choices and final launch choice"
  }finally{if(!$p.HasExited){Stop-Process -Id $p.Id -ErrorAction SilentlyContinue}}
 }
 foreach($path in $desktop,$menu){$shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($path);if($link.Arguments -ne '--settings' -or $link.TargetPath -notlike '*Screenshot.exe'){throw 'Invalid shortcut'}}
 Write-Output 'PASS shortcut targets and arguments'
}finally{
 Stop-Worker
 foreach($n in $backup.Keys){if($null -eq $backup[$n]){Remove-ItemProperty $cfg $n -ErrorAction SilentlyContinue}else{Set-ItemProperty $cfg $n $backup[$n] -Type DWord}}
 Start-Process "$env:LOCALAPPDATA/Programs/Screenshot/Screenshot.exe" -ArgumentList '--background' -WindowStyle Hidden
}
