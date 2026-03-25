include_guard(GLOBAL)

function(photon_setup_embedded_asset_platform)
    if (DEFINED PHOTON_EMBED_OBJCOPY)
        return()
    endif()

    set(_photon_objcopy_names)
    if (CMAKE_OBJCOPY)
        list(APPEND _photon_objcopy_names ${CMAKE_OBJCOPY})
    endif()
    list(APPEND _photon_objcopy_names llvm-objcopy objcopy)
    find_program(PHOTON_EMBED_OBJCOPY NAMES ${_photon_objcopy_names} REQUIRED)

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            set(PHOTON_EMBED_OBJ_FORMAT elf64-x86-64 CACHE INTERNAL "")
            set(PHOTON_EMBED_OBJ_ARCH i386:x86-64 CACHE INTERNAL "")
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
            set(PHOTON_EMBED_OBJ_FORMAT elf32-i386 CACHE INTERNAL "")
            set(PHOTON_EMBED_OBJ_ARCH i386 CACHE INTERNAL "")
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(PHOTON_EMBED_OBJ_FORMAT elf64-littleaarch64 CACHE INTERNAL "")
            set(PHOTON_EMBED_OBJ_ARCH aarch64 CACHE INTERNAL "")
        else()
            message(FATAL_ERROR "Unsupported Linux architecture for embedded asset objects: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        set(PHOTON_EMBED_OBJ_EXT o CACHE INTERNAL "")
    elseif (WIN32)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            set(PHOTON_EMBED_OBJ_FORMAT pei-x86-64 CACHE INTERNAL "")
            set(PHOTON_EMBED_OBJ_ARCH i386:x86-64 CACHE INTERNAL "")
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
            set(PHOTON_EMBED_OBJ_FORMAT pe-i386 CACHE INTERNAL "")
            set(PHOTON_EMBED_OBJ_ARCH i386 CACHE INTERNAL "")
        else()
            message(FATAL_ERROR "Unsupported Windows architecture for embedded asset objects: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        set(PHOTON_EMBED_OBJ_EXT obj CACHE INTERNAL "")
    elseif (APPLE)
        message(FATAL_ERROR
            "Embedded asset objects are not implemented for macOS yet.\n"
            "TODO: use `ld -r -sectcreate __DATA __photon_asset_<name> <file>` per asset to emit a relocatable Mach-O object,\n"
            "then generate the same tiny declaration headers used on Linux/Windows so the C++ interface stays identical.")
    else()
        message(FATAL_ERROR "Unsupported platform for embedded asset objects: ${CMAKE_SYSTEM_NAME}")
    endif()

    set(PHOTON_EMBED_OBJCOPY ${PHOTON_EMBED_OBJCOPY} CACHE INTERNAL "")
endfunction()

function(photon_embed_assets)
    set(options SPIRV)
    set(oneValueArgs HEADER_OUTPUT_DIR OBJECT_OUTPUT_DIR HEADER_TARGET ASSET_TARGET)
    set(multiValueArgs FILES)
    cmake_parse_arguments(PARSE_ARGV 0 PEA "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if (NOT PEA_HEADER_OUTPUT_DIR OR NOT PEA_OBJECT_OUTPUT_DIR OR NOT PEA_HEADER_TARGET OR NOT PEA_ASSET_TARGET)
        message(FATAL_ERROR "photon_embed_assets requires HEADER_OUTPUT_DIR, OBJECT_OUTPUT_DIR, HEADER_TARGET, and ASSET_TARGET")
    endif()

    photon_setup_embedded_asset_platform()

    file(MAKE_DIRECTORY ${PEA_HEADER_OUTPUT_DIR})
    file(MAKE_DIRECTORY ${PEA_OBJECT_OUTPUT_DIR})

    set(_photon_headers)
    set(_photon_objects)

    foreach(_photon_file IN LISTS PEA_FILES)
        get_filename_component(_photon_filename ${_photon_file} NAME)
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _photon_symbol ${_photon_filename})

        set(_photon_staged ${PEA_OBJECT_OUTPUT_DIR}/${_photon_symbol})
        set(_photon_object ${PEA_OBJECT_OUTPUT_DIR}/${_photon_symbol}.${PHOTON_EMBED_OBJ_EXT})
        set(_photon_header ${PEA_HEADER_OUTPUT_DIR}/${_photon_symbol}.hpp)

        add_custom_command(
            OUTPUT ${_photon_staged}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_photon_file} ${_photon_staged}
            DEPENDS ${_photon_file}
            COMMENT "Staging embedded asset ${_photon_staged}"
            VERBATIM
        )

        add_custom_command(
            OUTPUT ${_photon_object}
            COMMAND ${PHOTON_EMBED_OBJCOPY}
                --input-target=binary
                --output-target=${PHOTON_EMBED_OBJ_FORMAT}
                --binary-architecture=${PHOTON_EMBED_OBJ_ARCH}
                --set-section-alignment .data=16
                ${_photon_symbol}
                ${_photon_object}
            DEPENDS ${_photon_staged}
            WORKING_DIRECTORY ${PEA_OBJECT_OUTPUT_DIR}
            COMMENT "Embedding asset object ${_photon_object}"
            VERBATIM
        )

        if (PEA_SPIRV)
            file(GENERATE
                OUTPUT ${_photon_header}
                CONTENT
"#pragma once
#include <cstddef>
#include <cstdint>

extern const unsigned char _binary_${_photon_symbol}_start[];
extern const unsigned char _binary_${_photon_symbol}_end[];

inline const uint32_t* const ${_photon_symbol} = reinterpret_cast<const uint32_t*>(_binary_${_photon_symbol}_start);
inline const std::size_t ${_photon_symbol}_size =
    static_cast<std::size_t>(_binary_${_photon_symbol}_end - _binary_${_photon_symbol}_start);
")
        else()
            file(GENERATE
                OUTPUT ${_photon_header}
                CONTENT
"#pragma once
#include <cstddef>

extern const unsigned char _binary_${_photon_symbol}_start[];
extern const unsigned char _binary_${_photon_symbol}_end[];

inline const unsigned char* const ${_photon_symbol} = _binary_${_photon_symbol}_start;
inline const std::size_t ${_photon_symbol}_size =
    static_cast<std::size_t>(_binary_${_photon_symbol}_end - _binary_${_photon_symbol}_start);
")
        endif()

        list(APPEND _photon_headers ${_photon_header})
        list(APPEND _photon_objects ${_photon_object})
    endforeach()

    add_custom_target(Generate${PEA_HEADER_TARGET} ALL DEPENDS ${_photon_headers})
    add_custom_target(Generate${PEA_ASSET_TARGET} ALL DEPENDS ${_photon_objects})

    add_library(${PEA_HEADER_TARGET} INTERFACE)
    target_include_directories(${PEA_HEADER_TARGET} INTERFACE ${PEA_HEADER_OUTPUT_DIR})
    add_dependencies(${PEA_HEADER_TARGET} Generate${PEA_HEADER_TARGET})

    set_source_files_properties(${_photon_objects} PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    add_library(${PEA_ASSET_TARGET} STATIC ${_photon_objects})
    set_target_properties(${PEA_ASSET_TARGET} PROPERTIES LINKER_LANGUAGE CXX)
    add_dependencies(${PEA_ASSET_TARGET} Generate${PEA_ASSET_TARGET})
endfunction()
