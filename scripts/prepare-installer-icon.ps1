param([Parameter(Mandatory=$true)][string]$CoreRoot,[Parameter(Mandatory=$true)][string]$IconPath)
$ErrorActionPreference='Stop'
$adapter=Join-Path $CoreRoot 'adapters/screenshot/adapter_windows.go'
$code=[IO.File]::ReadAllText($adapter)
$hash=(Get-FileHash -LiteralPath $IconPath -Algorithm SHA256).Hash.ToLowerInvariant().Substring(0,16)
$iconName="Screenshot-$hash.ico"
function Replace-Once([string]$Old,[string]$New){
 if(!$script:code.Contains($Old) -or $script:code.IndexOf($Old) -ne $script:code.LastIndexOf($Old)){throw "Pinned installer icon anchor differs: $Old"}
 $script:code=$script:code.Replace($Old,$New)
}
Replace-Once '//go:embed Screenshot.exe ribbon.png' '//go:embed Screenshot.exe ribbon.png Screenshot.ico'
Replace-Once 'const uninstallKey =' ("const installedIcon = `"$iconName`"`n`nconst uninstallKey =")
Replace-Once 'a.progress.Store(55)' @'
icon, err := assets.ReadFile("Screenshot.ico")
	if err != nil { return err }
	if _, err = core.OwnedPath(dir, installedIcon); err != nil { return err }
	if err = core.AtomicWrite(filepath.Join(dir, installedIcon), icon); err != nil { return err }
	a.progress.Store(55)
'@
Replace-Once '"DisplayIcon": exe,' '"DisplayIcon": filepath.Join(dir, installedIcon) + ",0",'
Replace-Once 'core.Shortcuts("Screenshot", dir, "Screenshot.exe", "Screenshot.exe", "--settings", o.Desktop, o.StartMenu)' 'core.Shortcuts("Screenshot", dir, "Screenshot.exe", installedIcon, "--settings", o.Desktop, o.StartMenu)'
Replace-Once 'if !existing {' @'
// Remove the owned root shortcut left by native installers; shared core owns the nested one.
	if err = core.PowerShell(`$folder=[Environment]::GetFolderPath('Programs');$old=Join-Path $folder 'Screenshot.lnk';if(Test-Path -LiteralPath $old){$shell=New-Object -ComObject Shell.Application;$item=$shell.NameSpace($folder).ParseName('Screenshot.lnk');if($item.GetLink.Path -ieq ` + core.QuotePS(exe) + `){Remove-Item -LiteralPath $old -Force}}`); err != nil { return err }
// Shortcut IconLocation has a content-derived filename, bypassing cached EXE icons.
	windows.NewLazySystemDLL("shell32.dll").NewProc("SHChangeNotify").Call(0x08000000, 0x1000, 0, 0)
	if !existing {
'@
Replace-Once 'if err = core.Shortcuts("Screenshot", a.dir, "Screenshot.exe", "Screenshot.exe", "--settings", false, false); err != nil {' @'
iconPath, iconErr := core.OwnedPath(a.dir, installedIcon)
	if iconErr != nil { return iconErr }
	if iconErr = os.Remove(iconPath); iconErr != nil && !os.IsNotExist(iconErr) { return iconErr }
	if err = core.Shortcuts("Screenshot", a.dir, "Screenshot.exe", installedIcon, "--settings", false, false); err != nil {
'@
[IO.File]::WriteAllText($adapter,$code,[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $IconPath -Destination (Join-Path $CoreRoot 'adapters/screenshot/Screenshot.ico') -Force
