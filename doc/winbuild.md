# Building on Windows {#winbuild}

<!--
Copyright 2022, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

[TOC]

Monado has a work-in-progress port to Windows. While it's not ready for
widespread usage due to some rough edges and lack of drivers, it does build and
can serve as a base for further development.

## System Dependencies

Most dependencies for the Windows build on Monado are handled by [vcpkg][].
However, some are not installable or usable (in the way we want) through there,
so they require separate installation. For each, the command line to install
with "winget" (built in to at least Windows 11, and possibly newer builds of
Windows 10) and/or [scoop][] are provided below. Use whichever one you are more
comfortable with. (Scoop commands may require adding the "extras" bucket.)

[vcpkg]: https://vcpkg.io
[scoop]: https://github.com/ScoopInstaller/Scoop

- CMake
  - `winget install Kitware.CMake`
  - `scoop install cmake`
  - Recently, bundled with Visual Studio in a path like
    `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
    which gets added to your PATH in a Visual Studio command prompt/PowerShell.
- Python 3.x
  - `winget install Python.Python.3`
  - `scoop install python`
- Vulkan SDK
  - `winget install KhronosGroup.VulkanSDK`
  - `scoop install vulkan`
- Ninja (build tool, recommended but not strictly required)
  - Not available from winget
  - `scoop install ninja`
  - Recently, bundled with Visual Studio in a path like
    `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
    which gets added to your PATH in a Visual Studio command prompt/PowerShell.

You will also need Visual Studio. Most current development is happening with
2022, though it should also build with 2019. (Work on Monado might meet the
requirements for Microsoft's no-charge
[Visual Studio Community](https://visualstudio.microsoft.com/vs/community/)
license - see the
[VS Community License Terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)
and talk to your lawyer if unsure, this is not legal advice.)

We should be able to build using LLVM/Clang using libc++ instead of the MSVC
standard library, but this hasn't been widely tested.

## Configuring the build tree

The main points to note are that:

- If building against a normal clone/checkout of [vcpkg][], make sure that
  `CMAKE_TOOLCHAIN_FILE` is set to
  `yourVcpkgDir\scripts\buildsystems\vcpkg.cmake` (substituting `yourVcpkgDir`
  as appropriate). This will put vcpkg in "manifest mode", and it will build and
  install the dependencies in `vcpkg.json` (in the source tree) into a directory
  in the build tree automatically.
  - If you have run `.\vcpkg integrate install` in your vcpkg directory, this
    toolchain is added automatically when you "Open Folder" in Visual Studio for
    a folder containing CMake build scripts. So, in this case, just open the
    Monado source directory and everything will be set up automatically for you.
  - On the CMake command line, this means passing something like
    `"-DCMAKE_TOOLCHAIN_FILE=yourVcpkgDir\scripts\buildsystems\vcpkg.cmake"`
    (quotation marks possibly required depending on your shell).
- If building against "exported" dependencies from a vcpkg install (which can be
  used to share a build environment easily and reduce build times), you will
  also need to set `CMAKE_TOOLCHAIN_FILE` is set to
  `yourVcpkgDir\scripts\buildsystems\vcpkg.cmake` (where `yourVcpkgDir` here is
  the exported directory you extract), **and also** set `VCPKG_MANIFEST_MODE` to
  `OFF`. Because exported dependencies from vcpkg do not include the vcpkg tool
  binary itself (or the port files, etc), we can't use manifest mode and must
  disable it. Instead, Monado will build against the dependencies installed in
  your exported tree. Open a Visual Studio Developer PowerShell terminal to your
  source dir before following one of the following two sections to generate your
  build tree.
  - Without the vcpkg command-line tool, there's no "integrate" to let VS
    automatically know to take these steps, so you'll have to manually set the
    CMake variables in `CMakeSettings.json`. (VS has a GUI editor for it.)
  - On the CMake command line, this means passing something like
    `"-DCMAKE_TOOLCHAIN_FILE=yourVcpkgDir\scripts\buildsystems\vcpkg.cmake" -DVCPKG_MANIFEST_MODE=OFF`
    (quotation marks possibly required depending on your shell).

For either of them, you may choose to add
`"-DCMAKE_INSTALL_PREFIX=w:\someplace\else"` (with a path of your choice) to set
where to "install" to, if you don't want to run out of the build tree.

### Sample batch file for build

This assumes that you have a full clone of vcpkg in `w:\vcpkg` and want to build
on the command line, using the Visual Studio 2019 build tools.

```bat
@setlocal

@rem Set up environment for build
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

@rem Change current directory
w:
cd "w:\src\monado"

cmake -S . ^
  -B build
  -G Ninja ^
  -DCMAKE_BUILD_TYPE="Release" ^
  -DCMAKE_TOOLCHAIN_FILE="w:\vcpkg\scripts\buildsystems\vcpkg.cmake"

ninja -C build
```

If you want to build the `outOfProcess` version of Monado, please add extra
build parameter `-DXRT_FEATURE_SERVICE=ON`.

## Using

### Run Monado service

If you build the `outOfProcess` version of Monado, you need to start
the `monado-service.exe` first with the following command in `cmd.exe`
command prompt before running OpenXR clients:

```bat
monado-service.exe
```

or the following in PowerShell:

```pwsh
.\monado-service.exe
```

If you build the `inProcess` version of Monado, you don't need the above
steps, and you can jump to the next section to run OpenXR clients directly.

### Run hello_xr

Proper install of a runtime in Windows involves registry modifications. However,
the easiest way to test is just to set the `XR_RUNTIME_JSON` environment
variable (in a command prompt/powershell where you will launch the app) to the
generated JSON manifest file. Assuming you have a terminal open to the directory
where `hello_xr.exe` is, you can run the following in `cmd.exe` command prompt,
changing path as required:

```bat
@rem May have the build type as an additional directory if using a multi-config generator
set XR_RUNTIME_JSON=w:\src\monado\build\openxr_monado-dev.json
hello_xr.exe -G Vulkan
```

or the following in PowerShell:

```pwsh
$env:XR_RUNTIME_JSON="w:\src\monado\build\openxr_monado-dev.json"
.\hello_xr.exe -G Vulkan
```

## Limitations

Note that there are current limitations in the Windows build.
The main one currently is no actual headset drivers yet, partially because
some USB stuff needs porting, and partially because direct mode on Windows
is more complicated.
