$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$OpenOcd = (Get-ChildItem "$sdk/openocd" -Recurse -Filter "openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$Scripts = Join-Path (Split-Path (Split-Path $OpenOcd -Parent) -Parent) "scripts"
Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force
& $OpenOcd -s $Scripts -f "$root/wilibsp/tools/openocd/freewili2.cfg" `
    -c "program {$root/build/wilicankit.elf} verify reset exit"
