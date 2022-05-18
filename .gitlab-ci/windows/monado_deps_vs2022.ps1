# Copyright 2019-2022, Mesa contributors
# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: MIT
# Based on https://gitlab.freedesktop.org/mesa/mesa/-/blob/8396df5ad90aeb6ab2267811aba2187954562f81/.gitlab-ci/windows/mesa_deps_vs2019.ps1

# we want more secure TLS 1.2 for most things
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

# VS17.x is 2022
$msvc_url = 'https://aka.ms/vs/17/release/vs_buildtools.exe'

Get-Date
Write-Host "Downloading Visual Studio 2022 build tools"
Invoke-WebRequest -Uri $msvc_url -OutFile C:\vs_buildtools.exe -UseBasicParsing

Get-Date
Write-Host "Installing Visual Studio"
$vsInstallerArgs = @(
    "--wait"
    "--quiet"
    "--norestart"
    "--nocache"
    "--installPath"
    "C:\BuildTools"
    "--add"
    "Microsoft.VisualStudio.Component.VC.CoreBuildTools"
    "--add"
    "Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core"
    "--add"
    "Microsoft.VisualStudio.Component.Windows10SDK"
    "--add"
    "Microsoft.VisualStudio.Component.Windows11SDK.22000"
    "--add"
    "Component.Microsoft.Windows.CppWinRT"
    "--add"
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
)
Start-Process -NoNewWindow -Wait C:\vs_buildtools.exe -ArgumentList $vsInstallerArgs
if (!$?) {
    Write-Host "Failed to install Visual Studio tools"
    Exit 1
}
Remove-Item C:\vs_buildtools.exe -Force
