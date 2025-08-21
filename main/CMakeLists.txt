set(SRCS_FILES
    main.cpp
    app_main.cpp
    ../hal/hal.cpp
    ../hal/esp32/*.cpp
)

set(COMPONENT_ADD_INCLUDEDIRS
    "."
    "../hal"
    "../common"
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(IMAGE_DIR "${PROJECT_DIR}/main/images")
    set(CONVERTED_DIR "${PROJECT_DIR}/build/images_build")
    file(MAKE_DIRECTORY ${CONVERTED_DIR})
    set(IMG_HEADER_FILE "${PROJECT_DIR}/build/images_generated.h")
    file(WRITE ${IMG_HEADER_FILE} "#pragma once\n\n#include \"lvgl.h\"\n\n")
    set(LVGL_IMAGE_CONVERTER_SCRIPT "${PROJECT_DIR}/scripts/LVGLImage.py")

    file(GLOB IMAGE_FILES "${IMAGE_DIR}/*.png")

    foreach(png_file ${IMAGE_FILES})
        get_filename_component(file_name ${png_file} NAME_WE)
        set(c_file "${CONVERTED_DIR}/${file_name}.c")

        get_filename_component(file_name ${png_file} NAME_WE)

        add_custom_command(
            OUTPUT ${c_file}
            COMMAND ${Python3_EXECUTABLE} ${LVGL_IMAGE_CONVERTER_SCRIPT}
                --ofmt C
                --cf RGB565A8
                --output ${CONVERTED_DIR}
                ${png_file}
            DEPENDS ${png_file}
            COMMENT "Converting ${png_file} to ${c_file}"
        )

        string(REPLACE "-" "_" var_name ${file_name})
        file(APPEND ${IMG_HEADER_FILE} "extern const lv_img_dsc_t ${var_name};\n")

        list(APPEND CONVERTED_IMAGE_FILES ${c_file})
    endforeach()
endif()

idf_component_register(
    SRCS ${SRCS_FILES} ${CONVERTED_IMAGE_FILES}
    INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)

