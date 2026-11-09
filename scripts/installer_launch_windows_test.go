//go:build windows

package screenshot

import (
    "connect-installer.local/core/core"
    "golang.org/x/sys/windows"
    "golang.org/x/sys/windows/registry"
    "os"
    "testing"
    "time"
    "unsafe"
)

// Exercise the same Open method used by the real installer's checked Finish action,
// with the embedded release payload in a path containing spaces and Cyrillic.
func TestFinishOpensVisibleScreenshot(t *testing.T) {
    if os.Getenv("GITHUB_ACTIONS") != "true" || os.Getenv("CONNECT_INSTALLER_CI_TEST") != "1" { t.Skip("disposable Windows runner required") }
    a:=New();dir:=t.TempDir()+`\Screenshot тест folder`
    if err:=a.Install(core.Options{Path:dir,Language:"ru",Theme:"light"});err!=nil {t.Fatal(err)}
    k,_,err:=registry.CreateKey(registry.CURRENT_USER,settingsKey,registry.QUERY_VALUE|registry.SET_VALUE)
    if err!=nil {t.Fatal(err)}
    t.Cleanup(func(){k.Close()})
    for _,name:=range []string{"HotkeyVK","HotkeyModifiers"} {
        name:=name;old,_,readErr:=k.GetIntegerValue(name)
        t.Cleanup(func(){if readErr==nil{k.SetDWordValue(name,uint32(old))}else{k.DeleteValue(name)}})
    }
    // Keep a harmless binding without changing Windows Snipping.
    k.SetDWordValue("HotkeyVK",0x87);k.SetDWordValue("HotkeyModifiers",0)
    t.Cleanup(func(){a.stop();if e:=a.Remove();e!=nil{t.Error(e)}})
    a.hotkeyDone=true
    cls:=windows.StringToUTF16Ptr("ScreenshotSettings")
    find:=func()uintptr{r,_,_:=user32.NewProc("FindWindowW").Call(uintptr(unsafe.Pointer(cls)),0);return r}
    visible:=func()bool {h:=find();if h==0{return false};r,_,_:=user32.NewProc("IsWindowVisible").Call(h);return r!=0}
    if err=a.Open();err!=nil{t.Fatal("checked finish on cold launch:",err)}
    if !visible(){t.Fatal("finish launch did not show main window")}
    h:=find();user32.NewProc("PostMessageW").Call(h,0x10,0,0)
    for i:=0;i<50&&find()!=0;i++{time.Sleep(100*time.Millisecond)}
    if find()!=0{t.Fatal("main window did not close")}
    if err=a.Open();err!=nil{t.Fatal("finish launch with existing background worker:",err)}
    if !visible(){t.Fatal("IPC launch did not reopen main window")}
    t.Log("finish launch opens visible main window on cold start and existing worker, with spaces/Cyrillic path")
}
