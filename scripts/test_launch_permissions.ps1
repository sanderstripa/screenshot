param([Parameter(Mandatory=$true)][string]$Setup,[string]$Output='test-results/launch-permissions')
$ErrorActionPreference='Stop'
$Setup=(Resolve-Path -LiteralPath $Setup).Path
$Output=Join-Path $Output ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Output -Force|Out-Null
$Output=(Resolve-Path -LiteralPath $Output).Path
$helperScript=Join-Path $PSScriptRoot 'test_elevated_setup.ps1'
$source=Get-Content -LiteralPath $helperScript -Raw -Encoding UTF8
$marker="Add-Type @'"
$start=$source.IndexOf($marker)+$marker.Length
$end=$source.IndexOf("'@",$start)
Add-Type -TypeDefinition $source.Substring($start,$end-$start)
if([ElevationNative]::Elevated($PID) -ne 0){throw 'Run this driver as an ordinary user; only its helper requests UAC'}
$arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$helperScript+'" -Setup "'+$Setup+'" -Output "'+$Output+'"'
# Interactive UAC is intentional: this test reproduces elevated installation.
$helper=Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
$phase=Join-Path $Output 'phase.json'
$result=Join-Path $Output 'result.json'
for($i=0;$i -lt 120;$i++) {
    if(Test-Path $phase){break}
    if(Test-Path $result){throw ((Get-Content $result -Raw|ConvertFrom-Json).error)}
    Start-Sleep -Seconds 1
}
if(!(Test-Path $phase)){throw 'Elevated test did not start its worker'}
$link=Join-Path ([Environment]::GetFolderPath('Programs')) 'Screenshot.lnk'
Start-Process -FilePath $link -Wait|Out-Null
Start-Sleep -Milliseconds 500
$window=[ElevationNative]::FindWindow('ScreenshotSettings',$null)
if($window -eq [IntPtr]::Zero -or ![ElevationNative]::IsWindowVisible($window)) {
    'FAIL'|Set-Content (Join-Path $Output 'ordinary-launch-failed.txt')
    throw 'Start menu could not open the elevated worker'
}
'PASS'|Set-Content (Join-Path $Output 'ordinary-launch-passed.txt')
for($i=0;$i -lt 120;$i++){if(Test-Path $result){break};Start-Sleep -Seconds 1}
$helper.WaitForExit(10000)|Out-Null
if(!(Test-Path $result)){throw 'Elevated regression did not finish'}
$report=Get-Content $result -Raw|ConvertFrom-Json
if(!$report.success){throw $report.error}
$report.reports
# The helper restores the original hotkey. Resume the user's ordinary worker.
Start-Process -FilePath (Join-Path $env:LOCALAPPDATA 'Programs\Screenshot\Screenshot.exe') -ArgumentList '--background' -WindowStyle Hidden
