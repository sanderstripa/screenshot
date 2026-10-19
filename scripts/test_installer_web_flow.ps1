param([Parameter(Mandatory=$true)][string]$Setup)
$ErrorActionPreference='Stop'
$Output='test-results/installer-0.7.1'
$source=Get-Content scripts/test_installer_switches.ps1 -Raw
$a=$source.IndexOf('Add-Type -AssemblyName');$b=$source.IndexOf('$process=Start-Process');Invoke-Expression $source.Substring($a,$b-$a)
$key='HKCU:\Software\SanderStripa\Screenshot';$backup=@{}
foreach($n in 'HotkeyVK','HotkeyModifiers','ThemeMode','ThemeLight','LanguageEN'){try{$backup[$n]=Get-ItemPropertyValue $key $n -ErrorAction Stop}catch{$backup[$n]=$null}}
$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS='--remote-debugging-port=9229'
$env:CDP_PORT='9229'
$p=Start-Process (Resolve-Path $Setup) -PassThru -WindowStyle Hidden
try {
 for($i=0;$i -lt 60;$i++){$h=[SettingsNative]::FindForProcess('ScreenshotPremiumSetup',$p.Id);if($h -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 if($h -eq [IntPtr]::Zero){throw 'Installer window missing'}
 [SettingsNative]::ShowWindow($h,9)|Out-Null;[SettingsNative]::SetWindowPos($h,[IntPtr]1,200,100,0,0,0x41)|Out-Null
 Start-Sleep -Seconds 2
 & 'C:/Users/Sander Stripa/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe' scripts/test_installer_web_flow.cjs
 if($LASTEXITCODE -ne 0){throw 'Installer UI flow failed'}
 if(!$p.WaitForExit(10000)){throw 'Done failed to close installer'}
 if((Get-ItemPropertyValue $key ThemeMode) -ne 2 -or (Get-ItemPropertyValue $key LanguageEN) -ne 1){throw 'Appearance did not transfer to application'}
 $app=[IntPtr]::Zero
 for($i=0;$i -lt 50;$i++){$app=[SettingsNative]::FindWindow('ScreenshotSettings',$null);if($app -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 if($app -eq [IntPtr]::Zero){throw 'Application did not open after Done'}
 Shot $app 'application-inherited-dark-en'|Out-Null
 [SettingsNative]::PostMessage($app,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Write-Output 'PASS actual application opened; dark theme and English persisted'
}finally{
 if(!$p.HasExited){Stop-Process -Id $p.Id -ErrorAction SilentlyContinue}
 $e=[SettingsNative]::OpenEvent(2,$false,'Local\Screenshot.SanderStripa.Exit');if($e -ne [IntPtr]::Zero){[SettingsNative]::SetEvent($e)|Out-Null;[SettingsNative]::CloseHandle($e)|Out-Null;Start-Sleep -Milliseconds 500}
 foreach($n in $backup.Keys){if($null -eq $backup[$n]){Remove-ItemProperty $key $n -ErrorAction SilentlyContinue}else{Set-ItemProperty $key $n $backup[$n] -Type DWord}}
 Remove-Item Env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue
 $app=Join-Path (Get-ItemPropertyValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshot' InstallLocation) 'Screenshot.exe'
 Start-Process $app -ArgumentList '--background' -WindowStyle Hidden
}
