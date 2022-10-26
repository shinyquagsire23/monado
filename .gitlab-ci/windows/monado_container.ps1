# Copyright 2019-2022, Mesa contributors
# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: MIT
# Based on https://gitlab.freedesktop.org/mesa/mesa/-/blob/8396df5ad90aeb6ab2267811aba2187954562f81/.gitlab-ci/windows/mesa_container.ps1

# Implements the equivalent of ci-templates container-ifnot-exists, using
# Docker directly as we don't have buildah/podman/skopeo available under
# Windows, nor can we execute Docker-in-Docker
[CmdletBinding()]
param (
    # Address for container registry
    [Parameter()]
    [string]
    $RegistryUri,

    # Username for container registry
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]
    $RegistryUsername,

    # The path of the image for this user's fork
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]
    $UserImage,

    # The path of the image in the upstream registry
    [Parameter()]
    [string]
    $UpstreamImage,

    # Dockerfile to build
    [Parameter()]
    [string]
    $Dockerfile = "Dockerfile",

    # Base image to use for this container, if any
    [Parameter()]
    [string]
    $BaseImage,

    # Base image to use for this container, from the upstream repo, if any
    [Parameter()]
    [string]
    $BaseUpstreamImage
)

$RegistryPassword = "$env:CI_REGISTRY_PASSWORD"

$CommonDockerArgs = @(
    "--config"
    "windows-docker.conf"
)

$ErrorActionPreference = 'Stop'

# Returns $true on a zero error code
# If $AllowFailure is not set, throws on a nonzero exit code
function Start-Docker {
    param (
        # Should we just return the exit code on failure instead of throwing?
        [Parameter()]
        [switch]
        $AllowFailure = $false,

        # Should we try to log out before throwing in case of an error?
        [Parameter()]
        [switch]
        $LogoutOnFailure = $false,

        # What arguments should be passed to docker (besides the config)
        [Parameter(Mandatory = $true)]
        [string[]]
        $ArgumentList
    )
    $DockerArgs = $CommonDockerArgs + $ArgumentList
    Write-Verbose ("Will run docker " + ($DockerArgs -join " "))
    $proc = Start-Process -FilePath "docker" -ArgumentList $DockerArgs -NoNewWindow -PassThru -WorkingDirectory "$PSScriptRoot" -Wait

    if ($proc.ExitCode -eq 0) {
        Write-Verbose "Success!"
        return $true
    }
    if (!$AllowFailure) {
        Write-Error ($ArgumentList[0] + " failed")
        if ($LogoutOnFailure) {
            Write-Host "Logging out"
            Start-Process -FilePath "docker" -ArgumentList ($CommonDockerArgs + @("logout", "$RegistryUri")) `
                -NoNewWindow -PassThru -WorkingDirectory "$PSScriptRoot" -Wait
        }
        throw ("docker " + $ArgumentList[0] + " invocation failed")
    }

    return $false
}

# Returns $true if the $Image exists (whether or not we had to copy $UpstreamImage)
function Test-Image {
    param (
        # Image to look for
        [Parameter(Mandatory = $true)]
        [ValidateNotNullOrEmpty()]
        [string]
        $Image,

        # Equivalent image from the upstream repo, if any
        [Parameter()]
        [string]
        $UpstreamImage
    )

    # if the image already exists, great
    Write-Verbose "Looking for $Image"
    # $pullResult = Start-Docker -AllowFailure -ArgumentList ("pull", "$Image")
    docker @CommonDockerArgs pull "$Image"
    if ($?) {
        Write-Host "Image $UserImage exists"
        return $true
    }
    if (!$UpstreamImage) {
        Write-Host "Cannot find $Image"
        return $false
    }
    # if it's only upstream, copy it
    Write-Host "Cannot find $Image, looking for upstream $UpstreamImage"
    docker @CommonDockerArgs pull "$UpstreamImage"
    if ($?) {
        Write-Host "Found upstream image, copying image from upstream $UpstreamImage to user $Image"
        Start-Docker -LogoutOnFailure -ArgumentList ("tag", "$UpstreamImage", "$Image")
        Start-Docker -LogoutOnFailure -ArgumentList ("push", "$Image")
        return $true
    }
    Write-Host "Cannot find $Image nor $UpstreamImage"
    return $false
}

if ($BaseImage -and (!$BaseUpstreamImage)) {
    $BaseUpstreamImage = $BaseImage
}

Write-Host "Will log in to $RegistryUri as $RegistryUsername"
Write-Host "Will check for image $UserImage - if it does not exist but $UpstreamImage does, we copy that one, otherwise we need to build it."
if ($BaseImage) {
    Write-Host "This image builds on $BaseImage so we will check for it."
    if ($BaseUpstreamImage) {
        Write-Host "If it is missing but $BaseUpstreamImage exists, we copy that one. If both are missing, we error out."
    }
    else {
        Write-Host "If it is missing, we error out."
    }
}

if ($RegistryPassword) {
    # Start-Docker -ArgumentList ("login", "-u", "$RegistryUsername", "--password-stdin", "$RegistryPassword", "$RegistryUri")
    $loginProc = Start-Process -FilePath "docker" -ArgumentList ($CommonDockerArgs + @("login", "-u", "$RegistryUsername", "--password", "$RegistryPassword", "$RegistryUri")) `
        -NoNewWindow -PassThru -WorkingDirectory "$PSScriptRoot" -Wait
    if ($loginProc.ExitCode -ne 0) {
        throw "docker login failed"
    }
}
else {
    Write-Host "Skipping docker login, password not available"
}

# if the image already exists, don't rebuild it
$imageResult = Test-Image -Image $UserImage -UpstreamImage $UpstreamImage
if ($imageResult) {
    Write-Host "User image $UserImage already exists; not rebuilding"
    Start-Docker -ArgumentList ("logout", "$RegistryUri")
    Exit 0
}


# do we need a base image?
if ($BaseImage) {
    $baseImageResult = Test-Image -Image "$BaseImage" -UpstreamImage "$BaseUpstreamImage"
    if (!$baseImageResult) {
        throw "Could not find base image: neither '$BaseImage' nor '$BaseUpstreamImage' exist."
    }
}

Write-Host "No image found at $UserImage or $UpstreamImage; rebuilding, this may take a while"
$DockerBuildArgs = @(
    "build"
    "--no-cache"
    "-t"
    "$UserImage"
    "-f"
    "$Dockerfile"
)

if ($BaseImage) {
    $DockerBuildArgs += @(
        "--build-arg"
        "base_image=$BaseImage"
    )
}

$DockerBuildArgs += "."
Start-Docker -LogoutOnFailure -ArgumentList (, $DockerBuildArgs)

Get-Date

Write-Host "Done building image, now pushing $UserImage"
Start-Docker -LogoutOnFailure -ArgumentList ("push", "$UserImage")

if ($RegistryPassword) {
    Start-Docker -ArgumentList ("logout", "$RegistryUri")
}
else {
    Write-Host "Skipping docker logout, password not available so we did not login"
}
