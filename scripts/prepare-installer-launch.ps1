param([Parameter(Mandatory=$true)][string]$CoreRoot)
$ErrorActionPreference='Stop'
$adapter=Join-Path $CoreRoot 'adapters/screenshot/adapter_windows.go'
$code=[IO.File]::ReadAllText($adapter)
$old=@'
return core.PowerShell(`(New-Object -ComObject Shell.Application).ShellExecute(` + core.QuotePS(filepath.Join(a.dir, "Screenshot.exe")) + `, '--background', '', 'open', 1)`)
'@
$new=@'
// Finish opens the main window; --background is reserved for login autostart.
	file := filepath.Join(a.dir, "Screenshot.exe")
	visible := func() bool {
		cls := windows.StringToUTF16Ptr("ScreenshotSettings")
		h, _, _ := user32.NewProc("FindWindowW").Call(uintptr(unsafe.Pointer(cls)), 0)
		if h == 0 { return false }
		r, _, _ := user32.NewProc("IsWindowVisible").Call(h)
		return r != 0
	}
	waitVisible := func(attempts int) bool {
		for i:=0; i<attempts; i++ { if visible() {return true}; time.Sleep(100*time.Millisecond) }
		return false
	}
	// Prefer the user's desktop Shell. It may be unavailable on some Windows
	// sessions, or may accept ShellExecute without actually starting the app.
	shellErr := core.PowerShell(`(New-Object -ComObject Shell.Application).ShellExecute(` + core.QuotePS(file) + `, '--settings', ` + core.QuotePS(a.dir) + `, 'open', 1)`)
	if shellErr == nil && waitVisible(15) { return nil }
	// Native launch passes the exact EXE and working directory, without a shell
	// command string. Single-instance IPC also repairs a hidden existing worker.
	cmd := exec.Command(file, "--settings")
	cmd.Dir = a.dir
	if err := cmd.Start(); err != nil { return fmt.Errorf("open Screenshot: %w (Shell: %v)",err,shellErr) }
	go func(){ _ = cmd.Wait() }()
	if waitVisible(100) {return nil}
	return fmt.Errorf("Screenshot did not open its main window")

'@
if(!$code.Contains($old) -or $code.IndexOf($old) -ne $code.LastIndexOf($old)){throw 'Pinned installer launch anchor differs'}
$import='"os"'
if(!$code.Contains($import)){throw 'Pinned installer import anchor differs'}
$code=$code.Replace($import, "`"os`"`n`t`"os/exec`"")
[IO.File]::WriteAllText($adapter,$code.Replace($old,$new),[Text.UTF8Encoding]::new($false))
