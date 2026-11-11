param([Parameter(Mandatory=$true)][string]$CoreRoot)
$ErrorActionPreference='Stop'
$adapter=Join-Path $CoreRoot 'adapters/screenshot'
$redist=Join-Path $adapter 'vc_redist.x64.exe'
Invoke-WebRequest 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $redist
$signature=Get-AuthenticodeSignature -LiteralPath $redist
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)'){throw 'Microsoft Visual C++ signature verification failed'}
Copy-Item (Join-Path $PSScriptRoot 'vc-runtime_windows.go'),(Join-Path $PSScriptRoot 'vc-runtime_windows_test.go') $adapter
$source=Join-Path $adapter 'adapter_windows.go'
$text=[IO.File]::ReadAllText($source)
$anchor='func (a *Adapter) Install(o core.Options) error {'
if(!$text.Contains($anchor)){throw 'Installer prerequisite anchor missing'}
$text=$text.Replace($anchor,$anchor+"`n`tif err := ensureVCRuntime(); err != nil { return err }")
[IO.File]::WriteAllText($source,$text,[Text.UTF8Encoding]::new($false))
