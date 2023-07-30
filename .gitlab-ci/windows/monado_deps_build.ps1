# Copyright 2019-2022, Mesa contributors
# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: MIT
# Based on https://gitlab.freedesktop.org/mesa/mesa/-/blob/8396df5ad90aeb6ab2267811aba2187954562f81/.gitlab-ci/windows/mesa_deps_build.ps1

$VulkanRTVersion = "1.3.211.0"

# Download new TLS certs from Windows Update
Get-Date
Write-Host "Updating TLS certificate store"
$certdir = (New-Item -ItemType Directory -Name "_tlscerts")
certutil -syncwithWU "$certdir"
Foreach ($file in (Get-ChildItem -Path "$certdir\*" -Include "*.crt")) {
    Import-Certificate -FilePath $file -CertStoreLocation Cert:\LocalMachine\Root
}
Remove-Item -Recurse -Path $certdir

Get-Date
Write-Host "Installing runtime redistributables"
Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile "C:\vcredist_x64.exe"  -UseBasicParsing
Start-Process -NoNewWindow -Wait "C:\vcredist_x64.exe" -ArgumentList "/install /passive /norestart /log out.txt"
if (!$?) {
    Write-Host "Failed to install vc_redist"
    Exit 1
}
Remove-Item "C:\vcredist_x64.exe" -Force

Get-Date
Write-Host "Installing Vulkan runtime components"
$VulkanInstaller = "C:\VulkanRTInstaller.exe"
Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/$VulkanRTVersion/windows/VulkanRT-$VulkanRTVersion-Installer.exe" -OutFile "$VulkanInstaller"
Start-Process -NoNewWindow -Wait "$VulkanInstaller" -ArgumentList "/S"
if (!$?) {
    Write-Host "Failed to install Vulkan runtime components"
    Exit 1
}
Remove-Item "$VulkanInstaller" -Force

Get-Date
Write-Host "Installing Scoop"
Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
Invoke-WebRequest get.scoop.sh -OutFile install.ps1
.\install.ps1 -RunAsAdmin
scoop install git

Get-Date
Write-Host "Installing things from Scoop"
scoop install cmake
scoop install python
scoop install vulkan
scoop install ninja

Get-Date
Write-Host "Preparing vcpkg"
Set-Location C:\
git clone https://github.com/microsoft/vcpkg.git
Set-Location vcpkg
./bootstrap-vcpkg.bat -DisableMetrics

Get-Date
Write-Host "Installing some base deps from vcpkg"
./vcpkg.exe install cjson:x64-windows eigen3:x64-windows wil:x64-windows pthreads:x64-windows glslang:x64-windows libusb:x64-windows hidapi:x64-windows sdl2[base,vulkan]:x64-windows
Remove-Item -Recurse -Path downloads
Remove-Item -Recurse -Path buildtrees
