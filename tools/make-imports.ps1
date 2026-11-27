param([string]$SDRangel='C:\Program Files\SDRangel',[string]$Output=(Join-Path $PSScriptRoot '..\build\imports'))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
foreach($name in @('sdrbase','sdrgui')) {
    $lines = & dumpbin /nologo /exports (Join-Path $SDRangel "$name.dll")
    if($LASTEXITCODE -ne 0){throw "Cannot inspect $name exports"}
    $exports = foreach($line in $lines){if($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)'){ $Matches[1] }}
    if($exports.Count -lt 10){throw "No exports found in $name"}
    $def=Join-Path $Output "$name.def"
    @("LIBRARY $name.dll",'EXPORTS')+$exports | Set-Content -LiteralPath $def -Encoding ascii
    & lib /nologo /machine:x64 "/def:$def" "/out:$(Join-Path $Output "$name.lib")"
    if($LASTEXITCODE -ne 0){throw "Cannot generate $name import library"}
}
