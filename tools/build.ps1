param(
    [string]$IdfRegistry = 'C:\Espressif\tools\eim_idf.json'
)
$ErrorActionPreference = 'Stop'
$registry = Get-Content -Raw -LiteralPath $IdfRegistry | ConvertFrom-Json
$setup = @($registry.idfInstalled | Where-Object { $_.name -eq 'v6.1' -and $_.status -eq 'finished' })
if ($setup.Count -ne 1) { throw 'Expected exactly one finished ESP-IDF v6.1 installation in EIM.' }
$setup = $setup[0]
$env:PYTHONUTF8 = '1'
. $setup.activationScript
$firmware = Join-Path (Split-Path $PSScriptRoot -Parent) 'firmware'
& $setup.python (Join-Path $setup.path 'tools\idf.py') -C $firmware build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Run the size tool directly in ASCII CSV mode; avoids nested CMake/Windows
# code-page conversions corrupting the Unicode table borders.
& $setup.python -m esp_idf_size --format csv (Join-Path $firmware 'build\desk_panel.map')
exit $LASTEXITCODE
