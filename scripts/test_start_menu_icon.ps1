param([string]$Output='test-results/start-menu-icon')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ShellIconProbe {
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] public struct Info {public IntPtr Icon;public int Index;public uint Attributes;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)]public string Name;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=80)]public string Type;}
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SHGetFileInfo(string path,uint attrs,out Info info,uint size,uint flags);
 [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon);
}
'@
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expected=Join-Path $repo 'assets/Screenshot.ico'
$hash=(Get-FileHash $expected).Hash.ToLowerInvariant()
$dir=Get-ItemPropertyValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshot' InstallLocation
$icon=Join-Path $dir "Screenshot-$($hash.Substring(0,16)).ico"
if(!(Test-Path -LiteralPath $icon) -or (Get-FileHash $icon).Hash.ToLowerInvariant() -ne $hash){throw 'Dedicated installed icon missing or stale'}
$programs=[Environment]::GetFolderPath('Programs')
$shortcut=Join-Path $programs 'Screenshot/Screenshot.lnk'
if(Test-Path -LiteralPath (Join-Path $programs 'Screenshot.lnk')){throw 'Legacy root Start menu shortcut remains'}
$shell=New-Object -ComObject WScript.Shell
foreach($link in $shortcut,(Join-Path ([Environment]::GetFolderPath('Desktop')) 'Screenshot.lnk')){
 if(!(Test-Path -LiteralPath $link)){throw "Shortcut missing: $link"}
 if($shell.CreateShortcut($link).IconLocation -ine "$icon,0"){throw "Shortcut still references cached EXE icon: $link"}
}
New-Item -ItemType Directory -Path $Output -Force|Out-Null
foreach($size in 16,32){
 $info=[ShellIconProbe+Info]::new();$flags=0x100;if($size -eq 16){$flags=$flags -bor 1}
 $result=[ShellIconProbe]::SHGetFileInfo($shortcut,0,[ref]$info,[Runtime.InteropServices.Marshal]::SizeOf($info),$flags)
 if($result -eq [IntPtr]::Zero -or $info.Icon -eq [IntPtr]::Zero){throw 'Windows shell failed to resolve Start menu icon'}
 try{$bitmap=[Drawing.Icon]::FromHandle($info.Icon).ToBitmap();$bitmap.Save((Join-Path ([IO.Path]::GetFullPath($Output)) "shell-$size.png"));$bitmap.Dispose()}finally{[ShellIconProbe]::DestroyIcon($info.Icon)|Out-Null}
}
Write-Output "PASS Start menu and desktop use $icon; old root shortcut removed; Windows shell icons captured"
