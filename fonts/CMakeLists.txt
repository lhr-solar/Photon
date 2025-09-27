set(FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(FONT_HEADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${FONT_HEADER_OUTPUT_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FONT_FILES
    ${FONT_DIR}/Satoshi-Medium.ttf
)

set(FONT_HEADERS)
foreach(FONT ${FONT_FILES})
    get_filename_component(font_name_we ${FONT} NAME_WE)
    string(REPLACE "-" "_" font_name_safe ${font_name_we})
    set(header ${FONT_HEADER_OUTPUT_DIR}/${font_name_safe}_ttf.hpp)

    add_custom_command(
        OUTPUT ${header}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/find/ttf_to_header.py ${FONT} ${header}
        DEPENDS ${FONT} ${CMAKE_SOURCE_DIR}/find/ttf_to_header.py
        COMMENT "Generating font header ${header}"
        VERBATIM
    )

    list(APPEND FONT_HEADERS ${header})
endforeach()

add_custom_target(GenerateFontHeaders ALL DEPENDS ${FONT_HEADERS})

add_library(FontHeaders INTERFACE)
target_include_directories(FontHeaders INTERFACE ${FONT_HEADER_OUTPUT_DIR})
add_dependencies(FontHeaders GenerateFontHeaders)
