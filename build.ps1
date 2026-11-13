# Builds dist\dstorage.dll (ACEvoPerf proxy) with the installed MSVC + Windows SDK.
# -Install copies the result into the game folder named by ACEVO_GAME_DIR (development only,
# users install by drag and drop).
param([switch]$Install)
$ErrorActionPreference = "Stop"

function ConvertTo-SnapshotImageUrl([byte[]]$png) {
    $prefix = 'data:image/png;base64,'
    $urlLimit = 2048
    $image = [IO.MemoryStream]::new()
    try {
        # Omit the print-density chunk, which has no effect on the HUD.
        $image.Write($png, 0, 8)
        for ($offset = 8; $offset -lt $png.Length; $offset += $chunkSize) {
            if ($png.Length - $offset -lt 12) { throw 'Invalid snapshot PNG chunk' }
            $length = 0L
            for ($i = 0; $i -lt 4; $i++) { $length = 256 * $length + $png[$offset + $i] }
            $chunkSize = $length + 12
            if ($chunkSize -gt $png.Length - $offset) { throw 'Invalid snapshot PNG chunk' }
            $type = [Text.Encoding]::ASCII.GetString($png, $offset + 4, 4)
            if ($type -ne 'pHYs') { $image.Write($png, $offset, $chunkSize) }
        }
        $url = $prefix + [Convert]::ToBase64String($image.ToArray())
        if ($url.Length -lt $urlLimit) { return $url }

        # Gameface caps image URLs at 2048 characters. Resize an embedded copy only.
        Add-Type -AssemblyName System.Drawing
        $image.Position = 0
        $source = [Drawing.Image]::FromStream($image)
        try {
            for ($width = $source.Width - 1; $width -ge 1; $width = [Math]::Floor($width * 0.85)) {
                $height = [Math]::Max(1, [int][Math]::Round($source.Height * $width / $source.Width))
                $scaled = [Drawing.Bitmap]::new([int]$width, $height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
                try {
                    $graphics = [Drawing.Graphics]::FromImage($scaled)
                    try {
                        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                        $graphics.DrawImage($source, 0, 0, [int]$width, $height)
                    } finally { $graphics.Dispose() }
                    $output = [IO.MemoryStream]::new()
                    try {
                        $scaled.Save($output, [Drawing.Imaging.ImageFormat]::Png)
                        $url = $prefix + [Convert]::ToBase64String($output.ToArray())
                        if ($url.Length -lt $urlLimit) { return $url }
                    } finally { $output.Dispose() }
                } finally { $scaled.Dispose() }
            }
        } finally { $source.Dispose() }
        throw 'Snapshot PNG exceeds the Gameface image URL limit'
    } finally { $image.Dispose() }
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found, install Visual Studio 2022 with the C++ workload" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "no Visual Studio with the C++ toolset found" }
$msvc = Get-ChildItem "$vsPath\VC\Tools\MSVC" | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$sdkVer = (Get-ChildItem "$kits\Include" | Where-Object { Test-Path "$($_.FullName)\um\d3d12.h" } | Sort-Object Name -Descending | Select-Object -First 1).Name
Write-Host "MSVC: $($msvc.FullName)"
Write-Host "SDK : $sdkVer"
$env:INCLUDE = "$($msvc.FullName)\include;$kits\Include\$sdkVer\ucrt;$kits\Include\$sdkVer\um;$kits\Include\$sdkVer\shared;$kits\Include\$sdkVer\winrt"
$env:LIB = "$($msvc.FullName)\lib\x64;$kits\Lib\$sdkVer\ucrt\x64;$kits\Lib\$sdkVer\um\x64"
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$kits\bin\$sdkVer\x64;$env:PATH"
New-Item -ItemType Directory -Force "$root\dist" | Out-Null
New-Item -ItemType Directory -Force "$root\build" | Out-Null
Push-Location "$root\build"
try {
    $iconDefines = foreach ($state in @('inactive', 'active')) {
        $png = [IO.File]::ReadAllBytes("$root\assets\snapshot-$state.png")
        $url = ConvertTo-SnapshotImageUrl $png
        '#define ACEVO_SNAPSHOT_' + $state.ToUpperInvariant() + '_URL "' + $url + '"'
    }
    [IO.File]::WriteAllLines("$root\build\snapshot_icons.h", [string[]]$iconDefines, [Text.Encoding]::ASCII)
    & rc.exe /nologo /fo "$root\build\version.res" "$root\src\version.rc"
    if ($LASTEXITCODE -ne 0) { throw "rc.exe failed with $LASTEXITCODE" }
    $sources = Get-ChildItem "$root\src" -Recurse -Filter *.cpp | ForEach-Object { $_.FullName }
    & cl.exe /nologo /O2 /W4 /MT /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX `
        /I"$root\include" /I"$root\third_party\directstorage" /I"$root\build" /Fo"$root\build\\" $sources `
        /link /DLL /MACHINE:X64 /DEF:"$root\src\exports.def" /OUT:"$root\dist\dstorage.dll" `
        /IMPLIB:"$root\build\dstorage_proxy.lib" /PDB:"$root\build\dstorage_proxy.pdb" /DEBUG:FULL /OPT:REF /OPT:ICF `
        "$root\build\version.res" kernel32.lib user32.lib
    if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with $LASTEXITCODE" }
} finally { Pop-Location }
# the zip payload also needs Microsoft's runtime: the forwarder under the name the proxy falls back
# to, and the core itself under a name nothing else in the process asks for, because the game owns
# the name dstoragecore.dll and loads its own copy before the proxy gets a say (DEC-015)
Copy-Item "$root\third_party\directstorage\bin\x64\dstorage.dll" "$root\dist\dstorage_orig.dll" -Force
Copy-Item "$root\third_party\directstorage\bin\x64\dstoragecore.dll" "$root\dist\acevo_dstoragecore.dll" -Force
Write-Host "Built: $root\dist\dstorage.dll (+ dstorage_orig.dll, acevo_dstoragecore.dll)"

if ($Install) {
    $game = $env:ACEVO_GAME_DIR
    if (-not $game) { throw "set ACEVO_GAME_DIR to the game folder (the one with AssettoCorsaEVO.exe) to use -Install" }
    if (-not (Test-Path "$game\AssettoCorsaEVO.exe")) { throw "ACEVO_GAME_DIR=$game does not contain AssettoCorsaEVO.exe" }
    if (Get-Process AssettoCorsaEVO -ErrorAction SilentlyContinue) { throw "close the game before installing" }
    Copy-Item "$root\dist\dstorage.dll" "$game\dstorage.dll" -Force
    # both Microsoft files are copied every time: they move with the bundled runtime version, and a
    # stale one next to a fresh proxy is exactly the mismatch the log had to be taught to catch
    Copy-Item "$root\dist\dstorage_orig.dll" "$game\dstorage_orig.dll" -Force
    Copy-Item "$root\dist\acevo_dstoragecore.dll" "$game\acevo_dstoragecore.dll" -Force
    if (-not (Test-Path "$game\acevo_perf.ini")) { Copy-Item "$root\dist\acevo_perf.ini" "$game\acevo_perf.ini" }
    Write-Host "Installed into $game (existing ini kept)"
}
