set(MODEL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MODEL_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${MODEL_HEADER_OUTPUT_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB MODEL_FILES "${MODEL_DIR}/*.glb")

set(MODEL_HEADERS)
foreach(MODEL_FILE ${MODEL_FILES})
    get_filename_component(model_name_we ${MODEL_FILE} NAME_WE)
    string(REPLACE "-" "_" model_name_safe ${model_name_we})
    set(header ${MODEL_HEADER_OUTPUT_DIR}/${model_name_safe}_glb.hpp)

    add_custom_command(
        OUTPUT ${header}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/find/bin_to_header.py ${MODEL_FILE} ${header}
        DEPENDS ${MODEL_FILE} ${CMAKE_SOURCE_DIR}/find/bin_to_header.py
        COMMENT "Generating model header ${header}"
        VERBATIM
    )

    list(APPEND MODEL_HEADERS ${header})
endforeach()

add_custom_target(GenerateModelHeaders ALL DEPENDS ${MODEL_HEADERS})

add_library(ModelHeaders INTERFACE)
target_include_directories(ModelHeaders INTERFACE ${MODEL_HEADER_OUTPUT_DIR})
add_dependencies(ModelHeaders GenerateModelHeaders)
