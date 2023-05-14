# Copyright 2018-2023, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


# Target used for applying more aggressive optimizations to math-heavy code
add_library(xrt-optimized-math INTERFACE)

if(MSVC)
	target_compile_options(xrt-optimized-math INTERFACE $<IF:$<CONFIG:Debug>,/O2,/O3>)
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
	set(CMAKE_C_FLAGS
	    "${CMAKE_C_FLAGS} -Werror-implicit-function-declaration -Werror=incompatible-pointer-types"
		)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=int-conversion")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")

	# Use effectively ubiquitous SSE2 instead of x87 floating point
	# for increased reliability/consistency
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "[xX]86" AND XRT_FEATURE_SSE2)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 -mfpmath=sse")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 -mfpmath=sse")
	endif()

	target_compile_options(xrt-optimized-math INTERFACE $<IF:$<CONFIG:Debug>,-O2,-O3>)
endif()

if(NOT WIN32)
	# Even clang's gnu-style driver on windows doesn't accept this argument.

	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-undefined")
endif()
