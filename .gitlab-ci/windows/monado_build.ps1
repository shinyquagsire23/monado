# Copyright 2019-2022, Mesa contributors
# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: MIT
# Based on https://gitlab.freedesktop.org/mesa/mesa/-/blob/8396df5ad90aeb6ab2267811aba2187954562f81/.gitlab-ci/windows/mesa_build.ps1

[CmdletBinding()]
param (
    # Should we install the project?
    [Parameter()]
    [switch]
    $Install = $false,

    # Should we package the project?
    [Parameter()]
    [switch]
    $Package = $false,

    # Should we run the test suite?
    [Parameter()]
    [switch]
    $RunTests = $false
)
$ErrorActionPreference = 'Stop'

$env:PYTHONUTF8 = 1

Get-Date
Write-Host "Compiling Monado"
$sourcedir = (Resolve-Path "$PSScriptRoot/../..")
$builddir = Join-Path $sourcedir "build"
$installdir = Join-Path $sourcedir "install"
$vcpkgdir = "c:\vcpkg"
$toolchainfile = Join-Path $vcpkgdir "scripts/buildsystems/vcpkg.cmake"

Remove-Item -Recurse -Force $installdir -ErrorAction SilentlyContinue

Write-Output "builddir:$builddir"
Write-Output "installdir:$installdir"
Write-Output "sourcedir:$sourcedir"
Write-Output "toolchainfile:$toolchainfile"

# $installPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version 17  -property installationpath
# Write-Information "vswhere.exe installPath: $installPath"
# if (!$installPath) {
#     throw "Could not find VS2022 using vswhere!"
# }
$installPath = "C:\BuildTools"
Write-Output "installPath: $installPath"

# We have to clear this because some characters in a commit message may confuse cmd/Enter-VsDevShell.
$env:CI_COMMIT_DESCRIPTION = ""
$env:CI_COMMIT_MESSAGE = ""

Import-Module (Join-Path $installPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -no_logo -host_arch=amd64'

$ErrorActionPreference = 'Stop'
Push-Location $sourcedir

$cmakeArgs = @(
    "-S"
    "."
    "-B"
    "$builddir"
    "-GNinja"
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainfile"
    "-DCMAKE_INSTALL_PREFIX=$installdir"
    "-DX_VCPKG_APPLOCAL_DEPS_INSTALL=ON"
)
cmake @cmakeArgs
if (!$?) {
    throw "cmake generate failed!"
}

Write-Information "Building"
cmake --build $builddir
if (!$?) {
    throw "cmake build failed!"
}

if ($RunTests) {
    Write-Information "Running tests"
    cmake --build $builddir --target test

}

if ($Install) {
    Write-Information "Installing"
    cmake --build $builddir --target install
}


if ($Package) {
    Write-Information "Packaging"
    cmake --build $builddir --target package
}
