set(SRCS_FILES
    main.cpp
    app_main.cpp
    ../hal/hal.cpp
    ../hal/esp32/esp32_hal_display.cpp
    ../hal/esp32/esp32_hal_input.cpp
    ../hal/esp32/esp32_hal_network.cpp
    ../hal/esp32/esp32_hal_system.cpp
    ../hal/esp32/esp32_hal.cpp
)

set(COMPONENT_ADD_INCLUDEDIRS
    "."
    "../hal"
    "../common"
)

# Image conversion for ESP-IDF
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(IMAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/images")
    set(CONVERTED_DIR "${CMAKE_BINARY_DIR}/images_build")
    file(MAKE_DIRECTORY ${CONVERTED_DIR})
    set(IMG_HEADER_FILE "${CONVERTED_DIR}/images_generated.h")
    file(WRITE ${IMG_HEADER_FILE} "#pragma once\n\n#include \"lvgl.h\"\n\n")
    set(LVGL_IMAGE_CONVERTER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/LVGLImage.py")

    file(GLOB IMAGE_FILES "${IMAGE_DIR}/*.png")

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
    INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)

# Add images_build directory to include paths for images_generated.h
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND CONVERTED_IMAGE_FILES)
    target_include_directories(${COMPONENT_LIB} PUBLIC ${CONVERTED_DIR})
endif()

