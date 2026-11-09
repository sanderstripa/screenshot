param([Parameter(Mandatory=$true)][string]$CoreRoot)
$ErrorActionPreference='Stop'
$adapter=Join-Path $CoreRoot 'adapters/screenshot/adapter_windows.go'
$code=[IO.File]::ReadAllText($adapter)
$old=@'
return core.PowerShell(`(New-Object -ComObject Shell.Application).ShellExecute(` + core.QuotePS(filepath.Join(a.dir, "Screenshot.exe")) + `, '--background', '', 'open', 1)`)
'@
$new=@'
// The finish checkbox means open the app, not login autostart. Explorer keeps
	// its normal user context, and an existing worker receives --settings via IPC.
	// Check the visible result before the installer reports a successful launch.
	return core.PowerShell(`Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class ScreenshotLaunchWindow { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title); [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd); }'; (New-Object -ComObject Shell.Application).ShellExecute(` + core.QuotePS(filepath.Join(a.dir, "Screenshot.exe")) + `, '--settings', ` + core.QuotePS(a.dir) + `, 'open', 1); $visible=$false; for($i=0;$i -lt 120;$i++){ $h=[ScreenshotLaunchWindow]::FindWindowW('ScreenshotSettings',$null); if($h -ne [IntPtr]::Zero -and [ScreenshotLaunchWindow]::IsWindowVisible($h)){$visible=$true;break}; Start-Sleep -Milliseconds 100 }; if(!$visible){throw 'Screenshot main window did not open'}`)
'@
if(!$code.Contains($old) -or $code.IndexOf($old) -ne $code.LastIndexOf($old)){throw 'Pinned installer launch anchor differs'}
[IO.File]::WriteAllText($adapter,$code.Replace($old,$new),[Text.UTF8Encoding]::new($false))
