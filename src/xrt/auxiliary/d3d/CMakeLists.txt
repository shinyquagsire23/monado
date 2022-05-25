# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(aux_d3d STATIC d3d_dxgi_formats.h d3d_helpers.cpp d3d_helpers.hpp)
target_link_libraries(aux_d3d PUBLIC aux-includes xrt-interfaces ${DXGI_LIBRARY} WIL::WIL)

# needed for format includes
target_include_directories(aux_d3d PUBLIC ${Vulkan_INCLUDE_DIR})

if(XRT_HAVE_D3D11)
	target_sources(
		aux_d3d
		PRIVATE
			d3d_d3d11_bits.h
			d3d_d3d11_allocator.cpp
			d3d_d3d11_allocator.h
			d3d_d3d11_allocator.hpp
			d3d_d3d11_fence.cpp
			d3d_d3d11_fence.hpp
		)
	target_link_libraries(aux_d3d PUBLIC ${D3D11_LIBRARY})
endif()
