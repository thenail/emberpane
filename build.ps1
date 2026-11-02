# Restores NuGet packages, builds everything and runs the tests.
#
#   .\build.ps1                      Debug, x64
#   .\build.ps1 -Configuration Release -Platform ARM64
#   .\build.ps1 -Installer           Release, plus the per-user setup exe
#   .\build.ps1 -Zip                 Release, plus a zip of the app to run without installing
#   .\build.ps1 -Store               Release, plus an MSIX package for the Microsoft Store
#
# Output: build\<Platform>\<Configuration>\EmberPane\EmberPane.exe
#         build\installer\EmberPane-setup-<version>.exe (x64), EmberPane-setup-ARM64-<version>.exe
#         build\zip\EmberPane-zip-<version>.zip (x64), EmberPane-zip-ARM64-<version>.zip
#         build\store\EmberPane-store-<version>.msix (x64), EmberPane-store-ARM64-<version>.msix
#
# Needs Visual Studio 2026 with the C++ desktop workload and the Windows App
# SDK C++ component (Microsoft.VisualStudio.Component.WindowsAppSdkSupport.Cpp).
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')] [string] $Platform = 'x64',
    [switch] $SkipTests,
    [switch] $Installer,
    [switch] $Zip,
    [switch] $Store,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# A Debug build needs the debug CRT, which only developer machines have.
if ($Installer -or $Zip -or $Store) { $Configuration = 'Release' }

if (-not $SkipBuild) {
    # C++ projects restore packages.config with nuget.exe; fetch it on first use.
    $nuget = Join-Path $root 'tools\nuget.exe'
    if (-not (Test-Path $nuget)) {
        New-Item -ItemType Directory -Force (Split-Path $nuget) | Out-Null
        Invoke-WebRequest 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile $nuget
    }
    & $nuget restore (Join-Path $root 'src\EmberPane\packages.config') `
        -PackagesDirectory (Join-Path $root 'packages') -NonInteractive -Verbosity quiet

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1

    & $msbuild (Join-Path $root 'EmberPane.slnx') `
        -p:Configuration=$Configuration -p:Platform=$Platform -nologo -v:minimal -m
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    # The tests exercise the Core library, which is the same on every platform;
    # they can only run where the build's platform matches the machine's.
    if (-not $SkipTests -and $Platform -eq 'x64') {
        & (Join-Path $root "build\$Platform\$Configuration\EmberPane.Tests.exe")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

if ($Installer) {
    # Inno Setup, pinned, installed portably into tools\ on first use: no admin,
    # nothing registered on the machine.
    $innoVersion = '7.1.0'
    $inno = Join-Path $root 'tools\InnoSetup'
    $iscc = Join-Path $inno 'ISCC.exe'
    if (-not (Test-Path $iscc) -or (Get-Item $iscc).VersionInfo.ProductVersion -notlike "$innoVersion*") {
        $setup = Join-Path $env:TEMP "innosetup-$innoVersion-x64.exe"
        Invoke-WebRequest ("https://github.com/jrsoftware/issrc/releases/download/is-$($innoVersion -replace '\.', '_')/" +
            "innosetup-$innoVersion-x64.exe") -OutFile $setup
        $process = Start-Process $setup -Wait -PassThru -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES',
            '/NORESTART', '/CURRENTUSER', '/PORTABLE=1', "/DIR=`"$inno`""
        Remove-Item $setup
        if ($process.ExitCode -ne 0) { throw "Inno Setup install failed ($($process.ExitCode))" }
    }

    & $iscc /Q "/DPlatform=$Platform" `
        "/DSourceDir=$(Join-Path $root "build\$Platform\$Configuration\EmberPane")" `
        "/DOutputDir=$(Join-Path $root 'build\installer')" `
        (Join-Path $root 'installer\EmberPane.iss')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$app = Join-Path $root "build\$Platform\$Configuration\EmberPane"

# The files the installer takes (installer\EmberPane.iss): the build folder,
# less the linker's by-products and the logs a run leaves beside the exe.
function Get-AppFiles {
    $skip = 'trace.txt', 'diagnostics.txt', 'windows-probe.txt'
    Get-ChildItem $app -Recurse -File |
        Where-Object { $_.Extension -notin '.pdb', '.lib', '.exp' } |
        Where-Object { -not ($_.DirectoryName -eq $app -and $_.Name -in $skip) }
}

if ($Zip) {
    # The app's files, in an EmberPane folder, so
    # extracting gives one folder and not 150 files.
    $version = (Get-Item (Join-Path $app 'EmberPane.exe')).VersionInfo.FileVersion
    $name = if ($Platform -eq 'ARM64') { "EmberPane-zip-ARM64-$version.zip" } else { "EmberPane-zip-$version.zip" }
    $outDir = Join-Path $root 'build\zip'
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $zipPath = Join-Path $outDir $name
    Remove-Item $zipPath -ErrorAction SilentlyContinue

    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        Get-AppFiles | ForEach-Object {
            $entry = 'EmberPane/' + $_.FullName.Substring($app.Length + 1).Replace('\', '/')
            [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $_.FullName, $entry, 'Optimal')
        }
    }
    finally { $archive.Dispose() }
}

if ($Store) {
    # The same files again, with the package manifest and its logos, packed as
    # an MSIX (see packaging\store\README.md). It is the very build the
    # installer takes: the app finds out when it runs that it is packaged.
    $version = (Get-Item (Join-Path $app 'EmberPane.exe')).VersionInfo.FileVersion
    $name = if ($Platform -eq 'ARM64') { "EmberPane-store-ARM64-$version.msix" } else { "EmberPane-store-$version.msix" }
    $outDir = Join-Path $root 'build\store'
    $layout = Join-Path $outDir "layout\$Platform"
    Remove-Item $layout -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $layout | Out-Null

    Get-AppFiles | ForEach-Object {
        $target = Join-Path $layout $_.FullName.Substring($app.Length + 1)
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item $_.FullName $target
    }
    Copy-Item (Join-Path $root 'packaging\store\Images') $layout -Recurse

    # The Store wants a four-part version ending in 0, which ours always is.
    (Get-Content (Join-Path $root 'packaging\store\Package.appxmanifest') -Raw).
        Replace('$Version$', $version).Replace('$Architecture$', $Platform.ToLowerInvariant()) |
        Set-Content (Join-Path $layout 'AppxManifest.xml') -Encoding utf8 -NoNewline

    # makeappx comes with the Windows SDK build tools package the build restores.
    $makeappx = Get-ChildItem (Join-Path $root 'packages') -Recurse -Filter makeappx.exe |
        Where-Object { $_.Directory.Name -eq 'x64' } | Select-Object -First 1 -ExpandProperty FullName
    # It names every file it packs: its output is worth showing only on failure.
    $output = & $makeappx pack /o /d $layout /p (Join-Path $outDir $name)
    if ($LASTEXITCODE -ne 0) { $output; exit $LASTEXITCODE }
}
