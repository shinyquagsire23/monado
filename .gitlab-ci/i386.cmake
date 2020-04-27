# Toolchain file for 32-bit x86 linux build on 64-bit x86 linux
# Developed for use with Debian and its derivatives
#
# Copyright 2019-2020 Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(CMAKE_SYSTEM_NAME Linux)

set(TARGET i686-linux-gnu)
set(PREFIX ${TARGET}-)
set(SUFFIX) # required for
set(CMAKE_C_COMPILER ${PREFIX}gcc${SUFFIX})
set(CMAKE_CXX_COMPILER ${PREFIX}g++${SUFFIX})

set(CMAKE_C_COMPILER_AR ${PREFIX}gcc-ar${SUFFIX})
set(CMAKE_CXX_COMPILER_AR ${PREFIX}gcc-ar${SUFFIX})
set(CMAKE_C_COMPILER_RANLIB ${PREFIX}gcc-ranlib${SUFFIX})
set(CMAKE_CXX_COMPILER_RANLIB ${PREFIX}gcc-ranlib${SUFFIX})
set(CMAKE_NM ${PREFIX}gcc-nm${SUFFIX})
set(CMAKE_OBJCOPY ${PREFIX}objcopy)
set(CMAKE_OBJDUMP ${PREFIX}objdump)
set(CMAKE_RANLIB ${PREFIX}ranlib)
set(CMAKE_STRIP ${PREFIX}strip)

set(PKG_CONFIG_EXECUTABLE ${PREFIX}pkg-config)

if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX /usr/${TARGET})
endif()

set(CMAKE_SYSTEM_LIBRARY_PATH /usr/lib/i386-linux-gnu;/usr/lib32;/usr/${TARGET}/lib)

set(CMAKE_FIND_ROOT_PATH /usr/${TARGET})

# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
