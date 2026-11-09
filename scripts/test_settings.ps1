param([Parameter(Mandatory=$true)][string]$App, [string]$Output='test-results/settings')
$ErrorActionPreference='Stop'
python (Join-Path $PSScriptRoot 'test_settings_render.py') $App $Output
if($LASTEXITCODE){throw 'Single-window Screenshot regression failed'}
