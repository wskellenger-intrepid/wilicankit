$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$Port = 9090
$OpenOcd = (Get-ChildItem "$sdk/openocd" -Recurse -Filter "openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$Scripts = Join-Path (Split-Path (Split-Path $OpenOcd -Parent) -Parent) "scripts"
Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force

# RP2350 SRAM is 0x20000000..0x20082000; scan the whole range for the RTT block.
$proc = Start-Process -FilePath $OpenOcd -PassThru -WindowStyle Hidden -ArgumentList @(
    "-s", $Scripts, "-f", "$root/wilibsp/tools/openocd/freewili2.cfg",
    "-c", "init", "-c", 'rtt setup 0x20000000 0x82000 "SEGGER RTT"',
    "-c", "rtt start", "-c", "rtt server start $Port 0"
)
try {
    Start-Sleep -Seconds 2
    if ($proc.HasExited) { throw "openocd exited early - is the debug probe connected?" }
    $client = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $Port)
    $stream = $client.GetStream()
    $buf = New-Object byte[] 4096
    Write-Host "--- RTT connected (port $Port); Ctrl+C to stop ---"
    while ($true) {
        $n = $stream.Read($buf, 0, $buf.Length)
        if ($n -le 0) { break }
        [Console]::Out.Write([System.Text.Encoding]::ASCII.GetString($buf, 0, $n))
    }
} finally {
    if ($client) { $client.Close() }
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
}
