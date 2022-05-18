# Native Windows GitLab CI builds

<!--
# Copyright 2019-2022, Mesa contributors
# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: MIT

Based on https://gitlab.freedesktop.org/mesa/mesa/-/blob/8396df5ad90aeb6ab2267811aba2187954562f81/.gitlab-ci/windows/README.md
-->

We are using the same basic approach to Windows CI building as Mesa, just as we
do on Linux. See
<https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/.gitlab-ci/windows> for
the details there. The following is the Mesa readme, lightly modified to fit
Monado.

Unlike Linux, Windows cannot reuse the freedesktop ci-templates as they exist
as we do not have Podman, Skopeo, or even Docker-in-Docker builds available
under Windows.

We still reuse the same model: build a base container with the core operating
system and infrequently-changed build dependencies, then execute Monado builds
only inside that base container. This is open-coded in PowerShell scripts.

## Base container build

The base container build jobs execute the `monado_container.ps1` script which
reproduces the ci-templates behaviour. It looks for the registry image in
the user's namespace, and exits if found. If not found, it tries to copy
the same image tag from the upstream Monado repository. If that is not found,
the image is rebuilt inside the user's namespace.

The rebuild executes `docker build` which calls `monado_deps_*.ps1` inside the
container to fetch and install all build dependencies. This includes Visual
Studio Build Tools (downloaded from Microsoft, under the license which
allows use by open-source projects), and other build tools from Scoop.
(These are done as two separate jobs to allow "resuming from the middle".)

This job is executed inside a Windows shell environment directly inside the
host, without Docker.

## Monado build

The Monado build runs inside the base container, executing `mesa_build.ps1`.
This simply compiles Monado using CMake and Ninja, executing the build and
unit tests.

## Local testing

To try these scripts locally, you need this done once, rebooting after they are complete:

```pwsh
scoop install sudo
sudo Add-MpPreference -ExclusionProcess dockerd.exe
sudo Add-MpPreference -ExclusionProcess docker.exe
winget install stevedore
sudo Add-MpPreference -ExclusionPath c:\ProgramData\docker
```

then this, done when you want to test:

```pwsh
docker context use desktop-windows
```

before doing your normal `docker build .`, etc. (It may still be very slow
despite the virus scanning exclusions.)

If you're having issues accessing the network, see this comment's instructions:
<https://github.com/docker/for-win/issues/9847#issuecomment-832674649>
