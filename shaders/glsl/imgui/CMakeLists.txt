# List your shader sources
set(IMGUI_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(IMGUI_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(IMGUI_SHADERS
    ${IMGUI_SHADER_DIR}/scene.vert
    ${IMGUI_SHADER_DIR}/scene.frag
    ${IMGUI_SHADER_DIR}/ui.vert
    ${IMGUI_SHADER_DIR}/ui.frag
)

# Function to compile a shader
function(compile_shader input_file)
    get_filename_component(fname ${input_file} NAME)
    set(output_file ${IMGUI_SHADER_OUTPUT_DIR}/${fname}.spv)
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
foreach(shader ${IMGUI_SHADERS})
    compile_shader(${shader})
    get_filename_component(fname ${shader} NAME)
    list(APPEND SPV_OUTPUTS ${IMGUI_SHADER_OUTPUT_DIR}/${fname}.spv)
endforeach()

# Add a custom target to build all shaders
add_custom_target(compile_imgui_shaders ALL DEPENDS ${SPV_OUTPUTS})