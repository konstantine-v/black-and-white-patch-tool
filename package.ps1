# Builds the release files in dist\: the setup program, a zip of the same files for installing by hand, and their
# SHA-256 sums. Only the decompilation's own files go in; the game's code and data never do.
#
#   .\package.ps1             # build.ps1 (Release build, tests, hook verification), then package
#   .\package.ps1 -SkipBuild  # package the existing build\Release output
param([switch] $SkipBuild)
$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1") -Config Release
}

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(BlackWhiteDecomp VERSION ([0-9.]+)').
    Matches[0].Groups[1].Value
$build = Join-Path $root "build\Release"
$setup = Join-Path $build "BWDecompSetup.exe"
if (-not (Test-Path $setup)) { throw "${setup} is missing: build first" }
$built = (Get-Item $setup).VersionInfo.FileVersion
if ($built -ne $version) { throw "build\Release is version $built, CMakeLists.txt says ${version}: build again" }

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$name = "BWDecomp-$version"

# The zip has the layout setup installs (src/setup/Payload.cpp), without the uninstaller.
$files = [ordered]@{
    "BWLaunch.exe"                  = Join-Path $build "BWLaunch.exe"
    "BWDecomp.dll"                  = Join-Path $build "BWDecomp.dll"
    "BWDecomp.ini"                  = Join-Path $root "BWDecomp.ini"
    "BWDecomp/BWDecomp.default.ini" = Join-Path $root "BWDecomp.ini"
    "BWDecomp/README.txt"           = Join-Path $root "build\generated\release\README.txt"
    "BWDecomp/CHANGELOG.md"         = Join-Path $root "CHANGELOG.md"
    "BWDecomp/FIXES.md"             = Join-Path $root "docs\FIXES.md"
    "BWDecomp/RENDERER.md"          = Join-Path $root "docs\RENDERER.md"
    "BWDecomp/TEXTURES.md"          = Join-Path $root "docs\TEXTURES.md"
}
$zip = Join-Path $dist "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $files.GetEnumerator()) {
        # Entry names with "/": Compress-Archive in Windows PowerShell 5.1 writes "\", which other tools misread.
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $entry.Value, "$name/$($entry.Key)", [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally {
    $archive.Dispose()
}

$setupRelease = Join-Path $dist "$name-Setup.exe"
Copy-Item $setup $setupRelease -Force

$sums = foreach ($file in @($setupRelease, $zip)) {
    "$((Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()) *$(Split-Path $file -Leaf)"
}
$sumsFile = Join-Path $dist "$name-SHA256SUMS.txt"
[System.IO.File]::WriteAllLines($sumsFile, [string[]] $sums)

# A release should be built from a clean, tagged commit.
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    $dirty = & git -C $root status --porcelain
    if ($dirty) { Write-Warning "The working tree has uncommitted changes" }
    $tag = & git -C $root tag --points-at HEAD
    if ($tag -notcontains "v$version") { Write-Warning "HEAD is not tagged v$version" }
}

Get-ChildItem $setupRelease, $zip, $sumsFile | Format-Table Name, Length -AutoSize
