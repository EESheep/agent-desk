param([string]$IdfRegistry = 'C:\Espressif\tools\eim_idf.json')
$ErrorActionPreference = 'Stop'
$registry = Get-Content -Raw -LiteralPath $IdfRegistry | ConvertFrom-Json
$setup = @($registry.idfInstalled | Where-Object { $_.name -eq 'v6.1' -and $_.status -eq 'finished' })
if ($setup.Count -ne 1) { throw 'Expected one finished ESP-IDF v6.1 installation.' }
$setup = $setup[0]
$env:PYTHONUTF8 = '1'
$env:IDF_COMPONENT_CACHE_PATH = Join-Path (Split-Path $PSScriptRoot -Parent) 'local\idf-cache'
$env:CCACHE_DIR = Join-Path (Split-Path $PSScriptRoot -Parent) 'local\ccache'
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'safe.directory'
$env:GIT_CONFIG_VALUE_0 = $setup.path.Replace('\','/')
. $setup.activationScript
$firmware = Join-Path (Split-Path $PSScriptRoot -Parent) 'firmware-amoled-2.41-v2'
& $setup.python (Join-Path $setup.path 'tools\idf.py') -C $firmware build
exit $LASTEXITCODE
