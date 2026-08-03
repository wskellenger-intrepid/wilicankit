$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$gcc  = "C:/msys64/mingw64/bin/gcc.exe"
cmake -G Ninja -B "$root/build-tests" -S "$root/test" -DCMAKE_C_COMPILER="$gcc"
cmake --build "$root/build-tests"
ctest --test-dir "$root/build-tests" --output-on-failure
