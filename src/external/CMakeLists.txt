# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Catch2
add_library(xrt-external-catch2 INTERFACE)
target_include_directories(xrt-external-catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Catch2)
add_library(xrt-external-openvr INTERFACE)
target_include_directories(
	xrt-external-openvr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/openvr_includes
	)

# cJSON
add_library(xrt-external-cjson INTERFACE)
if(XRT_HAVE_SYSTEM_CJSON)
	target_link_libraries(xrt-external-cjson INTERFACE cJSON::cJSON)
else()
	target_include_directories(
		xrt-external-cjson SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cjson
		)
	target_compile_definitions(xrt-external-cjson INTERFACE CJSON_HIDE_SYMBOLS)
endif()

# FlexKalman
add_library(xrt-external-flexkalman INTERFACE)
target_include_directories(xrt-external-flexkalman INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/flexkalman)

# Glad
add_library(xrt-external-glad INTERFACE)
target_include_directories(xrt-external-glad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include)

# Hungarian graph algorithm
add_library(xrt-external-hungarian INTERFACE)
target_include_directories(xrt-external-hungarian INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/hungarian)

# JNIPP and Android JNI wrappers
if(ANDROID)
	add_library(xrt-external-jnipp STATIC jnipp/jnipp.cpp)
	target_include_directories(xrt-external-jnipp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/jnipp)

	file(GLOB WRAP_SOURCES android-jni-wrap/wrap/*.cpp)
	add_library(xrt-external-jni-wrap STATIC ${WRAP_SOURCES})
	target_include_directories(
		xrt-external-jni-wrap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/android-jni-wrap
		)
	target_link_libraries(xrt-external-jni-wrap PUBLIC xrt-external-jnipp)
endif()

# OpenXR
add_library(xrt-external-openxr INTERFACE)
target_include_directories(
	xrt-external-openxr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/openxr_includes
	)

# External SLAM tracking
if(XRT_FEATURE_SLAM)
	add_library(xrt-external-slam STATIC slam_tracker/slam_tracker.hpp)
	set_target_properties(xrt-external-slam PROPERTIES LINKER_LANGUAGE CXX)
	target_include_directories(
		xrt-external-slam INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/slam_tracker
		)
	target_include_directories(xrt-external-slam SYSTEM INTERFACE ${SLAM_INCLUDE_DIRS})
	target_link_libraries(xrt-external-slam INTERFACE ${SLAM_LIBRARIES})
endif()

# STB
add_library(xrt-external-stb INTERFACE)
target_include_directories(xrt-external-stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# renderdoc
add_library(xrt-external-renderdoc INTERFACE)
target_include_directories(
	xrt-external-renderdoc SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/renderdoc_api
	)

# imgui
if(XRT_HAVE_OPENGL)
	add_library(
		xrt-external-imgui STATIC
		imgui/imgui/cimgui.cpp
		imgui/imgui/cimgui.h
		imgui/imgui/cimplot.cpp
		imgui/imgui/cimplot.h
		imgui/imgui/imconfig.h
		imgui/imgui/imgui.cpp
		imgui/imgui/imgui.h
		imgui/imgui/imgui_demo.cpp
		imgui/imgui/imgui_draw.cpp
		imgui/imgui/imgui_impl_opengl3.cpp
		imgui/imgui/imgui_impl_opengl3.h
		imgui/imgui/imgui_internal.h
		imgui/imgui/imgui_widgets.cpp
		imgui/imgui/implot.cpp
		imgui/imgui/implot.h
		imgui/imgui/implot_demo.cpp
		imgui/imgui/implot_internal.h
		imgui/imgui/implot_items.cpp
		imgui/imgui/imstb_rectpack.h
		imgui/imgui/imstb_textedit.h
		imgui/imgui/imstb_truetype.h
		imgui/imgui_monado/imgui_monado.cpp
		)

	target_include_directories(
		xrt-external-imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui
		)
	target_include_directories(xrt-external-imgui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
	target_compile_definitions(xrt-external-imgui PUBLIC CIMGUI_NO_EXPORT)
	set_target_properties(xrt-external-imgui PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
	target_link_libraries(xrt-external-imgui PUBLIC xrt-external-glad)

	if(XRT_HAVE_SDL2)
		add_library(
			xrt-external-imgui-sdl2 STATIC
			imgui/imgui/cimgui_sdl.cpp imgui/imgui/imgui_impl_sdl.cpp
			imgui/imgui/imgui_impl_sdl.h
			)
		target_link_libraries(
			xrt-external-imgui-sdl2
			PUBLIC xrt-external-imgui
			PUBLIC ${SDL2_LIBRARIES}
			)

		target_include_directories(xrt-external-imgui-sdl2 PUBLIC ${SDL2_INCLUDE_DIRS})
		target_include_directories(
			xrt-external-imgui-sdl2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui
			)
		set_target_properties(
			xrt-external-imgui-sdl2 PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF
			)
	endif()

endif()

# tinyceres
add_library(xrt-external-tinyceres INTERFACE)
target_include_directories(
	xrt-external-tinyceres SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tinyceres/include
	)
