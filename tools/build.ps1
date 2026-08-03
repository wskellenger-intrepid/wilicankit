$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$SdkPath     = (Get-ChildItem "$sdk/sdk" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$Toolchain   = (Get-ChildItem "$sdk/toolchain" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$PicotoolDir = (Get-ChildItem "$sdk/picotool" -Recurse -Filter "picotool" -Directory | Select-Object -First 1).FullName
if ($args -contains "-Clean") {
    Remove-Item "$root/build" -Recurse -Force -ErrorAction SilentlyContinue
}
cmake -G Ninja -B "$root/build" -S "$root" -DCMAKE_BUILD_TYPE=RelWithDebInfo `
    -DPICO_SDK_PATH="$SdkPath" -DPICO_TOOLCHAIN_PATH="$Toolchain" `
    -DPICO_PLATFORM=rp2350 -Dpicotool_DIR="$PicotoolDir"
cmake --build "$root/build"
