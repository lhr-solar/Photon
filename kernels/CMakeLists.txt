set(KERNEL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(KERNEL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/spirv)
file(MAKE_DIRECTORY ${KERNEL_OUTPUT_DIR})

# Add new Kernels Here
set(KERNELS
    ${KERNEL_DIR}/scene.vert
    ${KERNEL_DIR}/scene.frag
    ${KERNEL_DIR}/ui.vert
    ${KERNEL_DIR}/ui.frag
)

function(compile_shader input_file)
    get_filename_component(fname ${input_file} NAME)
    set(output_file ${KERNEL_OUTPUT_DIR}/${fname}.spv)
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND glslangValidator -V ${input_file} -o ${output_file}
        DEPENDS ${input_file}
        COMMENT "Compiling shader ${fname}"
        VERBATIM
    )
    set(${fname}_SPV ${output_file} PARENT_SCOPE)
endfunction()

set(SPV_OUTPUTS)
foreach(kernel ${KERNELS})
    compile_shader(${kernel})
    get_filename_component(fname ${kernel} NAME)
    list(APPEND SPV_OUTPUTS ${KERNEL_OUTPUT_DIR}/${fname}.spv)
endforeach()

add_custom_target(compile_kernels ALL DEPENDS ${SPV_OUTPUTS})
