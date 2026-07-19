include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

set(KERNEL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(GLSL_KERNEL_DIR ${KERNEL_DIR}/glsl)
set(METAL_KERNEL_DIR ${KERNEL_DIR}/metal)
set(KERNEL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/spirv)
set(KERNEL_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
set(KERNEL_OBJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/kernel_objects)

if (APPLE)
    add_library(KernelHeaders INTERFACE)
    target_include_directories(KernelHeaders INTERFACE ${KERNEL_HEADER_OUTPUT_DIR})
    add_library(KernelAssets INTERFACE)
    return()
endif()

if (NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "GLSL kernel compilation is only supported on Windows and Linux.")
endif()

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

file(MAKE_DIRECTORY ${KERNEL_OUTPUT_DIR})

file(GLOB KERNEL_SOURCES CONFIGURE_DEPENDS
    ${GLSL_KERNEL_DIR}/*.frag
    ${GLSL_KERNEL_DIR}/*.vert
)

function(compile_shader input_file output_list)
    get_filename_component(fname ${input_file} NAME)
    set(output_file ${KERNEL_OUTPUT_DIR}/${fname}.spv)

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${GLSLANG_VALIDATOR} -V ${input_file} -o ${output_file}
        DEPENDS ${input_file}
        COMMENT "Compiling shader ${fname} -> ${output_file}"
        VERBATIM
    )

    set(${output_list} ${${output_list}} ${output_file} PARENT_SCOPE)
endfunction()

set(SPV_OUTPUTS)
foreach(src ${KERNEL_SOURCES})
    compile_shader(${src} SPV_OUTPUTS)
endforeach()

add_custom_target(compile_kernels ALL DEPENDS ${SPV_OUTPUTS})

photon_embed_assets(
    SPIRV
    HEADER_OUTPUT_DIR ${KERNEL_HEADER_OUTPUT_DIR}
    OBJECT_OUTPUT_DIR ${KERNEL_OBJECT_OUTPUT_DIR}
    HEADER_TARGET KernelHeaders
    ASSET_TARGET KernelAssets
    FILES ${SPV_OUTPUTS}
)
