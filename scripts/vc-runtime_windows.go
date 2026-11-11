//go:build windows

package screenshot

import (
 "connect-installer.local/core/core"
 _ "embed"
 "fmt"
 "os"
 "path/filepath"
 "golang.org/x/sys/windows/registry"
)

// The build downloads this official package and verifies Microsoft's signature.
//go:embed vc_redist.x64.exe
var vcRedist []byte

func hasVCRuntime() bool {
 for _,view:=range []uint32{registry.WOW64_32KEY,registry.WOW64_64KEY} {
  k,e:=registry.OpenKey(registry.LOCAL_MACHINE,`SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`,registry.QUERY_VALUE|view)
  if e!=nil{continue}
  installed,_,_:=k.GetIntegerValue("Installed");major,_,_:=k.GetIntegerValue("Major");minor,_,_:=k.GetIntegerValue("Minor");k.Close()
  if installed==1&&(major>14||(major==14&&minor>=44)){return true}
 };return false
}
func ensureRuntime(check func()bool,install func()error) error {
 if check(){return nil};if err:=install();err!=nil{return err};if !check(){return fmt.Errorf("Microsoft Visual C++ x64 Runtime installation could not be verified")};return nil
}
func ensureVCRuntime() error {
 return ensureRuntime(hasVCRuntime,func()error{
  dir,err:=os.MkdirTemp("","Screenshot-VC-Runtime-");if err!=nil{return err};defer os.RemoveAll(dir)
  file:=filepath.Join(dir,"vc_redist.x64.exe");if err=os.WriteFile(file,vcRedist,0600);err!=nil{return err}
  // Only the Microsoft dependency is elevated; Screenshot remains per-user.
  script:=`$ErrorActionPreference='Stop';$file=`+core.QuotePS(file)+`;`+`
$sig=Get-AuthenticodeSignature -LiteralPath $file
if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)'){throw 'Invalid Microsoft Visual C++ package signature'}
$p=Start-Process -FilePath $file -ArgumentList '/install','/quiet','/norestart' -Verb RunAs -Wait -PassThru
if($p.ExitCode -notin @(0,3010,1638)){throw "Microsoft Visual C++ installation failed: $($p.ExitCode)"}
`
  return core.PowerShell(script)
 })
}
