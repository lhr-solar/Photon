set(DBC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DBC_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${DBC_HEADER_OUTPUT_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB DBC_FILES "${DBC_DIR}/*.dbc")

set(DBC_HEADERS)
foreach(DBC_FILE ${DBC_FILES})
    get_filename_component(dbc_name_we ${DBC_FILE} NAME_WE)
    string(REPLACE "-" "_" dbc_name_safe ${dbc_name_we})
    set(header ${DBC_HEADER_OUTPUT_DIR}/${dbc_name_safe}_dbc.hpp)

    add_custom_command(
        OUTPUT ${header}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/find/ttf_to_header.py ${DBC_FILE} ${header}
        DEPENDS ${DBC_FILE} ${CMAKE_SOURCE_DIR}/find/ttf_to_header.py
        COMMENT "Generating DBC header ${header}"
        VERBATIM
    )

    list(APPEND DBC_HEADERS ${header})
endforeach()

add_custom_target(GenerateDbcHeaders ALL DEPENDS ${DBC_HEADERS})

add_library(DbcHeaders INTERFACE)
target_include_directories(DbcHeaders INTERFACE ${DBC_HEADER_OUTPUT_DIR})
add_dependencies(DbcHeaders GenerateDbcHeaders)
