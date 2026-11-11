//go:build windows
package screenshot
import("errors";"testing")
func TestRuntimeAlreadyInstalled(t *testing.T){if err:=ensureRuntime(func()bool{return true},func()error{t.Fatal("existing runtime must not be reinstalled");return nil});err!=nil{t.Fatal(err)}}
func TestMissingRuntimeInstalledAndVerified(t *testing.T){ready:=false;calls:=0;if err:=ensureRuntime(func()bool{return ready},func()error{calls++;ready=true;return nil});err!=nil||calls!=1{t.Fatal(err,calls)}}
func TestRuntimeFailureStopsInstallation(t *testing.T){expected:=errors.New("UAC cancelled");if err:=ensureRuntime(func()bool{return false},func()error{return expected});err!=expected{t.Fatal(err)};if err:=ensureRuntime(func()bool{return false},func()error{return nil});err==nil{t.Fatal("unverified installation accepted")}}
