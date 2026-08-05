<#
.SYNOPSIS
  Guarded flash of the FreeWili 2 MAIN / DISPLAY RP2350 over the on-board multiprobe.

.DESCRIPTION
  Pins the openocd CMSIS-DAP interface by target NAME, so you cannot accidentally flash the
  wrong RP2350 (the costly mistake this wraps away):
      interface 0 = DISPLAY      interface 1 = MAIN
  openocd defaults to interface 0 if unspecified, so always go through this script (or pass
  the interface yourself). Also forces forward-slash paths into openocd's Tcl, which silently
  eats backslashes on Windows.

  openocd is discovered from the Pico SDK install ($HOME/.pico-sdk/openocd/*/openocd.exe)
  unless $env:OPENOCD points at one. Setup-agnostic: no machine-specific paths baked in.

.EXAMPLE
  ./flash-rp2350.ps1 main    ../../freewilimain/build/FreeWiliMain.elf
  ./flash-rp2350.ps1 display E:/Downloads/archives/freewilimain/build/out/FW2Display.uf2
  ./flash-rp2350.ps1 main            # no firmware = connect-only sanity check (no write)
#>
param(
  [Parameter(Mandatory)][ValidateSet('main','display')][string]$Target,
  [string]$Firmware
)
$ErrorActionPreference = 'Stop'

# interface map (multiprobe debug config): 0 = DISPLAY, 1 = MAIN
$iface = if ($Target -eq 'main') { 1 } else { 0 }

# locate openocd (env override, else Pico SDK install)
$ocd = $env:OPENOCD
if (-not $ocd) {
  $ocd = (Get-ChildItem "$HOME/.pico-sdk/openocd" -Recurse -Filter openocd.exe -ErrorAction SilentlyContinue |
          Select-Object -First 1).FullName
}
if (-not $ocd -or -not (Test-Path $ocd)) {
  throw "openocd not found. Set `$env:OPENOCD or install the Pico SDK openocd."
}
$scripts = Join-Path (Split-Path $ocd) 'scripts'

$common = @(
  '-s', $scripts,
  '-f', 'interface/cmsis-dap.cfg',
  '-c', 'adapter speed 5000',
  '-c', "cmsis-dap usb interface $iface",
  '-f', 'target/rp2350.cfg'
)

if (-not $Firmware) {
  Write-Host "Connect-only check: $Target (interface $iface)" -ForegroundColor Cyan
  & $ocd @common -c 'init; exit'
  return
}

if (-not (Test-Path $Firmware)) { throw "firmware not found: $Firmware" }
# openocd Tcl: forward slashes only
$fw = (Resolve-Path $Firmware).Path -replace '\\','/'

Write-Host "Flashing $Target on multiprobe interface $iface" -ForegroundColor Cyan
Write-Host "  firmware: $fw"
& $ocd @common -c "program `"$fw`" verify reset exit"
if ($LASTEXITCODE -ne 0) { throw "openocd flash FAILED (exit $LASTEXITCODE)" }
Write-Host "OK: $Target flashed and verified." -ForegroundColor Green
