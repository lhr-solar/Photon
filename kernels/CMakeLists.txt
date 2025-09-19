set(KERNEL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(KERNEL_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/spirv)
set(KERNEL_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${KERNEL_OUTPUT_DIR})
file(MAKE_DIRECTORY ${KERNEL_HEADER_OUTPUT_DIR})

# Add new kernels here
set(KERNEL_SOURCES
    ${KERNEL_DIR}/scene.vert
    ${KERNEL_DIR}/scene.frag
    ${KERNEL_DIR}/ui.vert
    ${KERNEL_DIR}/ui.frag
    ${KERNEL_DIR}/custom_shader.vert
    ${KERNEL_DIR}/custom_shader.frag
)

# convert GLSL to SPIR-V
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


# SPIR-V to headers
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(KERNEL_HEADERS)
foreach(SPV ${SPV_OUTPUTS})
    get_filename_component(fname ${SPV} NAME)
    string(REPLACE "." "_" VAR ${fname})
    set(header ${KERNEL_HEADER_OUTPUT_DIR}/${VAR}.hpp)

    add_custom_command(
        OUTPUT ${header}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/find/spv_to_header.py ${SPV} ${header}
        DEPENDS ${SPV}
        COMMENT "Generating header ${header} from ${SPV}"
    )

    list(APPEND KERNEL_HEADERS ${header})
endforeach()

add_custom_target(GenerateKernelHeaders ALL DEPENDS ${KERNEL_HEADERS})

add_library(KernelHeaders INTERFACE)
target_include_directories(KernelHeaders INTERFACE ${KERNEL_HEADER_OUTPUT_DIR})
add_dependencies(KernelHeaders GenerateKernelHeaders)
