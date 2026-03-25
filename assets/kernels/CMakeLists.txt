include(${CMAKE_SOURCE_DIR}/find/embed_assets.cmake)

set(KERNEL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(KERNEL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/spirv)
set(KERNEL_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
set(KERNEL_OBJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/kernel_objects)

file(MAKE_DIRECTORY ${KERNEL_OUTPUT_DIR})

set(KERNEL_SOURCES
    ${KERNEL_DIR}/scene.vert
    ${KERNEL_DIR}/scene.frag
    ${KERNEL_DIR}/ui.vert
    ${KERNEL_DIR}/ui.frag
    ${KERNEL_DIR}/custom_shader.vert
    ${KERNEL_DIR}/custom_shader.frag
    ${KERNEL_DIR}/background.vert
    ${KERNEL_DIR}/background.frag
    ${KERNEL_DIR}/triangle.vert
    ${KERNEL_DIR}/triangle.frag
    ${KERNEL_DIR}/viking.vert
    ${KERNEL_DIR}/viking.frag
    ${KERNEL_DIR}/gltf.vert
    ${KERNEL_DIR}/gltf.frag
    ${KERNEL_DIR}/postProcess.vert
    ${KERNEL_DIR}/postProcess.frag
)

function(compile_shader input_file output_list)
    get_filename_component(fname ${input_file} NAME)
    set(output_file ${KERNEL_OUTPUT_DIR}/${fname}.spv)

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND glslangValidator -V ${input_file} -o ${output_file}
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
