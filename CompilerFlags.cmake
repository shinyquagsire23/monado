# Copyright 2018-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

if(NOT MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
	set(CMAKE_C_FLAGS
	    "${CMAKE_C_FLAGS} -Werror-implicit-function-declaration -Werror=incompatible-pointer-types"
		)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=int-conversion")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
endif()

if(NOT WIN32)
	# Even clang's gnu-style driver on windows doesn't accept this argument.

	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-undefined")
endif()
