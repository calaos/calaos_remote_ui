# Include shared source definitions
include(${CMAKE_CURRENT_SOURCE_DIR}/../main/fonts/fonts.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sources.cmake)

# For ESP-IDF, we need to adjust paths since we're in the main/ subdirectory
set(SRCS_FILES "")
foreach(source ${ALL_SOURCES})
    if(${source} MATCHES "^main/")
        # Remove main/ prefix for sources in main directory
        string(REPLACE "main/" "" adjusted_source ${source})
        list(APPEND SRCS_FILES ${adjusted_source})
    else()
        # Add ../ prefix for sources outside main directory
        list(APPEND SRCS_FILES ../${source})
    endif()
endforeach()

# Convert common include dirs to ESP-IDF component format
set(COMPONENT_ADD_INCLUDEDIRS "")
foreach(include_dir ${COMMON_INCLUDE_DIRS})
    if(${include_dir} STREQUAL "main")
        list(APPEND COMPONENT_ADD_INCLUDEDIRS ".")
    else()
        list(APPEND COMPONENT_ADD_INCLUDEDIRS "../${include_dir}")
    endif()
endforeach()

# Image conversion for ESP-IDF
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(IMAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/images")
    set(CONVERTED_DIR "${CMAKE_BINARY_DIR}/images_build")
    file(MAKE_DIRECTORY ${CONVERTED_DIR})
    set(IMG_HEADER_FILE "${CONVERTED_DIR}/images_generated.h")
    file(WRITE ${IMG_HEADER_FILE} "#pragma once\n\n#include \"lvgl.h\"\n\n")
    set(LVGL_IMAGE_CONVERTER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/LVGLImage.py")

    file(GLOB IMAGE_FILES CONFIGURE_DEPENDS "${IMAGE_DIR}/*.png")

    foreach(png_file ${IMAGE_FILES})
        get_filename_component(file_name ${png_file} NAME_WE)
        set(c_file "${CONVERTED_DIR}/${file_name}.c")

        add_custom_command(
            OUTPUT ${c_file}
            COMMAND ${Python3_EXECUTABLE} ${LVGL_IMAGE_CONVERTER_SCRIPT}
                --ofmt C
                --cf RGB565A8
                --output ${CONVERTED_DIR}
                ${png_file}
            DEPENDS ${png_file} ${LVGL_IMAGE_CONVERTER_SCRIPT}
            COMMENT "Converting ${png_file} to ${c_file}"
        )

        string(REPLACE "-" "_" var_name ${file_name})
        file(APPEND ${IMG_HEADER_FILE} "extern const lv_image_dsc_t ${var_name};\n")

        list(APPEND CONVERTED_IMAGE_FILES ${c_file})
    endforeach()
endif()

idf_component_register(
    SRCS ${SRCS_FILES} ${CONVERTED_IMAGE_FILES}
    INCLUDE_DIRS "." ${COMPONENT_ADD_INCLUDEDIRS}
    LDFRAGMENTS "linker.lf"
)

# Add images_build directory to include paths for images_generated.h
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND CONVERTED_IMAGE_FILES)
    target_include_directories(${COMPONENT_LIB} PUBLIC ${CONVERTED_DIR})
endif()

