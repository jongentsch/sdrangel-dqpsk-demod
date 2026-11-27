param(
    [string]$SDRangel='C:\Program Files\SDRangel',
    [string]$Plugin=(Join-Path $PSScriptRoot '..\build\demoddqpsk.dll'),
    [string]$Destination=(Join-Path $env:LOCALAPPDATA 'SDRangel-DQPSK'),
    [switch]$CreateShortcut
)
$ErrorActionPreference='Stop'
$sourceRoot=(Resolve-Path -LiteralPath $SDRangel).Path
$pluginPath=(Resolve-Path -LiteralPath $Plugin).Path
if(-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'sdrangel.exe'))){throw 'SDRangel executable not found'}
$targetRoot=[IO.Path]::GetFullPath($Destination)
if($targetRoot.TrimEnd('\') -eq $sourceRoot.TrimEnd('\')){throw 'Use a separate user-local destination'}
New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null
foreach($item in Get-ChildItem -LiteralPath $sourceRoot){
    if($item.Name -in @('plugins','Uninstall.exe')){continue}
    $target=Join-Path $targetRoot $item.Name
    if(Test-Path -LiteralPath $target){continue}
    if($item.PSIsContainer){New-Item -ItemType Junction -Path $target -Target $item.FullName | Out-Null}
    elseif($item.Extension -ne '.pdb') {
        try {New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null}
        catch {Copy-Item -LiteralPath $item.FullName -Destination $target}
    }
}
$pluginDirectory=Join-Path $targetRoot 'plugins'
New-Item -ItemType Directory -Force -Path $pluginDirectory | Out-Null
foreach($item in Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'plugins')){
    $target=Join-Path $pluginDirectory $item.Name
    if(Test-Path -LiteralPath $target){continue}
    if($item.PSIsContainer){New-Item -ItemType Junction -Path $target -Target $item.FullName | Out-Null}
    elseif($item.Extension -ne '.pdb') {
        try {New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null}
        catch {Copy-Item -LiteralPath $item.FullName -Destination $target}
    }
}
$installed=Join-Path $pluginDirectory 'demoddqpsk.dll'
if(Test-Path -LiteralPath $installed){
    Copy-Item -LiteralPath $installed -Destination ($installed+'.backup-'+(Get-Date -Format 'yyyyMMddTHHmmss'))
}
Copy-Item -LiteralPath $pluginPath -Destination $installed
if($CreateShortcut){
    $shortcutDirectory=Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs'
    New-Item -ItemType Directory -Force -Path $shortcutDirectory | Out-Null
    $shell=New-Object -ComObject WScript.Shell
    $shortcut=$shell.CreateShortcut((Join-Path $shortcutDirectory 'SDRangel with Symbol Demodulator.lnk'))
    $shortcut.TargetPath=Join-Path $targetRoot 'sdrangel.exe'
    $shortcut.WorkingDirectory=$targetRoot
    $shortcut.IconLocation=$shortcut.TargetPath
    $shortcut.Description='SDRangel with native receive-only DQPSK and H-DQPSK symbol demodulation'
    $shortcut.Save()
}
Write-Output "Installed plugin: $installed"
Write-Output "Launch: $(Join-Path $targetRoot 'sdrangel.exe')"
Get-FileHash -LiteralPath $installed -Algorithm SHA256 | Select-Object Hash
