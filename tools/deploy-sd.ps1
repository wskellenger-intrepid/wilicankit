<#
.SYNOPSIS
  Deploy wilicankit.uf2 to the FreeWili 2's SD card /apps folder, over MAIN's
  serial CLI -- NOT a debug-probe flash.

.DESCRIPTION
  wilicankit is loaded at runtime from the SD card's /apps folder; flashing
  the UF2 directly onto the display RP2350 (tools/flash.ps1) would overwrite
  the display bootloader that reads it from there. Use this script instead:

    1. Finds MAIN's text CLI serial port by its USB product string
       ("FW2 v01" -- see wilibsp/libs/onewili/include/onewili_binary.h).
    2. Sends the OneWili "SDCard Host Select" command (wire `h\x\k`) to hand
       the SD card to the PC as a USB mass-storage drive.
    3. Waits for the new removable drive letter to enumerate.
    4. Copies the built UF2 into <drive>:\apps (creating the folder if
       needed).
    5. Ejects the drive and hands the SD card back to MAIN (`h\x\k 0`) so the
       display CPU can see it again.

.EXAMPLE
  ./tools/deploy-sd.ps1
  ./tools/deploy-sd.ps1 -Firmware build/wilicankit.uf2 -ComPort COM7
  ./tools/deploy-sd.ps1 -NoRelease   # leave the card mounted on the PC
#>
param(
  [string]$Firmware,
  [string]$ComPort,
  [int]$Baud = 115200,
  [switch]$NoRelease
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Firmware) { $Firmware = Join-Path $root 'build/wilicankit.uf2' }
if (-not (Test-Path $Firmware)) { throw "firmware not found: $Firmware (run tools/build.ps1 first)" }
$fwName = Split-Path $Firmware -Leaf

# ---- locate MAIN's text CLI port ("FW2 v01") ----
if ($ComPort) {
  $portName = $ComPort
} else {
  $pnp = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
         Where-Object { $_.Name -match 'FW2 v01' } |
         Select-Object -First 1
  if (-not $pnp) {
    Write-Host "Available serial ports:" -ForegroundColor Yellow
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match '\(COM\d+\)' } | ForEach-Object { Write-Host "  $($_.Name)" }
    throw "MAIN CLI serial port not found (looking for a 'FW2 v01' COM port -- is the board plugged in and MAIN running stock firmware? or pass -ComPort explicitly)"
  }
  if ($pnp.Name -notmatch '\((COM\d+)\)') { throw "could not parse COM port from '$($pnp.Name)'" }
  $portName = $Matches[1]
  Write-Host "MAIN CLI port: $portName ($($pnp.Name))" -ForegroundColor Cyan
}

# ---- send a OneWili text command (0x02 reset-to-root prefix + cmd + LF),
#      wait for the closing "[... <0|1>]" response frame ----
function Send-OneWiliCommand {
  param([System.IO.Ports.SerialPort]$Port, [string]$Cmd)
  $bytes = [System.Text.Encoding]::ASCII.GetBytes([char]2 + $Cmd + "`n")
  $Port.DiscardInBuffer()
  $Port.Write($bytes, 0, $bytes.Length)
  $deadline = (Get-Date).AddSeconds(5)
  $buf = ''
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 100
    if ($Port.BytesToRead -gt 0) { $buf += $Port.ReadExisting() }
    if ($buf -match '(?s)\[(?!\*)[^\[\]]* ([01])\]') {
      return @{ Ok = ($Matches[1] -eq '1'); Response = $buf }
    }
  }
  return @{ Ok = $false; Response = $buf }
}

$port = New-Object System.IO.Ports.SerialPort $portName, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 5000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
try {
  Write-Host 'Requesting SD card mass storage mode (h\x\k 1)...' -ForegroundColor Cyan
  $result = Send-OneWiliCommand -Port $port -Cmd 'h\x\k 1'
  if (-not $result.Ok) { throw "MAIN rejected SD host-select command: $($result.Response)" }

  # Windows can keep a stale drive-letter entry (no media) after a previous
  # release, so a removable-drive-letter diff misses a same-letter remount.
  # Identify the card by its known root folders instead.
  Write-Host 'Waiting for SD card to enumerate as a drive...' -ForegroundColor Cyan
  $drive = $null
  $deadline = (Get-Date).AddSeconds(40)
  while ((Get-Date) -lt $deadline -and -not $drive) {
    Start-Sleep -Milliseconds 500
    $removable = Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=2' | Select-Object -ExpandProperty DeviceID
    foreach ($d in $removable) {
      if ((Test-Path "$d\apps") -and (Test-Path "$d\appdata")) { $drive = $d; break }
    }
  }
  if (-not $drive) { throw 'SD card drive did not appear within 40s' }
  Write-Host "SD card mounted at $drive" -ForegroundColor Green

  # ---- copy the uf2 into /apps ----
  $appsDir = Join-Path "$drive\" 'apps'
  if (-not (Test-Path $appsDir)) { New-Item -ItemType Directory -Path $appsDir | Out-Null }
  Copy-Item -Path $Firmware -Destination (Join-Path $appsDir $fwName) -Force
  Write-Host "Copied $fwName to $appsDir" -ForegroundColor Green

  if ($NoRelease) {
    Write-Host "Leaving SD card mounted as USB mass storage (-NoRelease)." -ForegroundColor Yellow
    return
  }

  # ---- eject, then hand the SD card back to MAIN ----
  try {
    $shell = New-Object -ComObject Shell.Application
    $shell.Namespace(17).ParseName("$drive\").InvokeVerb('Eject')
    Start-Sleep -Milliseconds 1500
  } catch {
    Write-Warning "Could not auto-eject $drive -- switching SD host back anyway."
  }
  Write-Host 'Returning SD card to MAIN CPU (h\x\k 0)...' -ForegroundColor Cyan
  $result = Send-OneWiliCommand -Port $port -Cmd 'h\x\k 0'
  if (-not $result.Ok) { Write-Warning "MAIN did not confirm SD host release: $($result.Response)" }
} finally {
  $port.Close()
}
Write-Host "OK: $fwName deployed to SD card /apps." -ForegroundColor Green
