# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
#[[.rst:
GenerateOpenXRRuntimeManifest
---------------

The following functions are provided by this module:

- :command:`generate_openxr_runtime_manifest_buildtree`
- :command:`generate_openxr_runtime_manifest_at_install`


.. command:: generate_openxr_runtime_manifest_buildtree

  Generates a runtime manifest suitable for use in the build tree,
  with absolute paths, at configure time::

    generate_openxr_runtime_manifest_buildtree(
        RUNTIME_TARGET <target>          # Name of your runtime target
        OUT_FILE <outfile>               # Name of the manifest file (with path) to generate
        [MANIFEST_TEMPLATE <template>]   # Optional: Specify an alternate template to use
        )


.. command:: generate_openxr_runtime_manifest_at_install

  Generates a runtime manifest at install time and installs it where desired::

    generate_openxr_runtime_manifest_buildtree(
        RUNTIME_TARGET <target>            # Name of your runtime target
        DESTINATION <dest>                 # The install-prefix-relative path to install the manifest to.
        RELATIVE_RUNTIME_DIR <dir>         # The install-prefix-relative path that the runtime library is installed to.
        [ABSOLUTE_RUNTIME_PATH|            # If present, path in generated manifest is absolute
         RUNTIME_DIR_RELATIVE_TO_MANIFEST <dir>]
                                           # If present (and ABSOLUTE_RUNTIME_PATH not present), specifies the
                                           # runtime directory relative to the manifest directory in the installed layout
        [OUT_FILENAME <outfilename>        # Optional: Alternate name of the manifest file to generate
        [MANIFEST_TEMPLATE <template>]     # Optional: Specify an alternate template to use
        )
#]]
get_filename_component(_OXR_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
set(_OXR_MANIFEST_SCRIPT
    "${_OXR_CMAKE_DIR}/GenerateOpenXRRuntimeManifestInstall.cmake.in"
    CACHE INTERNAL "" FORCE)

set(_OXR_MANIFEST_TEMPLATE
    "${_OXR_CMAKE_DIR}/openxr_monado.in.json"
    CACHE INTERNAL "" FORCE)

function(generate_openxr_runtime_manifest_buildtree)
    set(options)
    set(oneValueArgs RUNTIME_TARGET OUT_FILE MANIFEST_TEMPLATE)
    set(multiValueArgs)
    cmake_parse_arguments(_genmanifest "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN})

    if(NOT _genmanifest_RUNTIME_TARGET)
        message(FATAL_ERROR "Need RUNTIME_TARGET specified!")
    endif()
    if(NOT _genmanifest_OUT_FILE)
        message(FATAL_ERROR "Need OUT_FILE specified!")
    endif()
    if(NOT _genmanifest_MANIFEST_TEMPLATE)
        set(_genmanifest_MANIFEST_TEMPLATE "${_OXR_MANIFEST_TEMPLATE}")
    endif()

    # Set template values
    set(_genmanifest_INTERMEDIATE_MANIFEST
        ${CMAKE_CURRENT_BINARY_DIR}/intermediate_manifest_buildtree_${_genmanifest_RUNTIME_TARGET}.json
    )
    set(_genmanifest_IS_INSTALL OFF)

    set(_script
        ${CMAKE_CURRENT_BINARY_DIR}/make_build_manifest_${_genmanifest_RUNTIME_TARGET}.cmake
    )
    configure_file("${_OXR_MANIFEST_SCRIPT}" "${_script}" @ONLY)
    add_custom_command(
        TARGET ${_genmanifest_RUNTIME_TARGET}
        POST_BUILD
        BYPRODUCTS "${_genmanifest_OUT_FILE}"
        # "${_genmanifest_INTERMEDIATE_MANIFEST}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DOUT_FILE=${_genmanifest_OUT_FILE}"
            "-DRUNTIME_PATH=$<TARGET_FILE:${_genmanifest_RUNTIME_TARGET}>" -P
            "${_script}" DEPENDS "${_script}")
endfunction()

function(generate_openxr_runtime_manifest_at_install)
    set(options ABSOLUTE_RUNTIME_PATH)
    set(oneValueArgs
        RUNTIME_TARGET DESTINATION OUT_FILENAME
        RUNTIME_DIR_RELATIVE_TO_MANIFEST RELATIVE_RUNTIME_DIR MANIFEST_TEMPLATE)
    set(multiValueArgs)
    cmake_parse_arguments(_genmanifest "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN})

    if(NOT _genmanifest_RUNTIME_TARGET)
        message(FATAL_ERROR "Need RUNTIME_TARGET specified!")
    endif()
    if(NOT _genmanifest_DESTINATION)
        message(FATAL_ERROR "Need DESTINATION specified!")
    endif()
    if(NOT _genmanifest_RELATIVE_RUNTIME_DIR)
        message(FATAL_ERROR "Need RELATIVE_RUNTIME_DIR specified!")
    endif()
    if(NOT _genmanifest_OUT_FILENAME)
        set(_genmanifest_OUT_FILENAME "${_genmanifest_RUNTIME_TARGET}.json")
    endif()
    if(NOT _genmanifest_MANIFEST_TEMPLATE)
        set(_genmanifest_MANIFEST_TEMPLATE "${_OXR_MANIFEST_TEMPLATE}")
    endif()
    set(_genmanifest_INTERMEDIATE_MANIFEST
        "${CMAKE_CURRENT_BINARY_DIR}/${_genmanifest_OUT_FILENAME}")
    set(_genmanifest_IS_INSTALL ON)
    # Template value
    set(RUNTIME_FILENAME
        ${CMAKE_SHARED_MODULE_PREFIX}${_genmanifest_RUNTIME_TARGET}${CMAKE_SHARED_MODULE_SUFFIX}
    )

    set(_script
        ${CMAKE_CURRENT_BINARY_DIR}/make_manifest_${_genmanifest_RUNTIME_TARGET}.cmake
    )
    configure_file("${_OXR_MANIFEST_SCRIPT}" "${_script}" @ONLY)
    install(SCRIPT "${_script}")
endfunction()
