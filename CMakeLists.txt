cmake_minimum_required(VERSION 3.10)
project(Driver C)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

file(GLOB SRC_FILES "${SRC_DIR}/*.c")

set(OBJ_FILES "")
foreach(file ${SRC_FILES})
    get_filename_component(fname ${file} NAME_WE)
    list(APPEND OBJ_FILES "${fname}.o")
endforeach()

set(KBUILD_FILE "${CMAKE_BINARY_DIR}/Kbuild")

file(WRITE ${KBUILD_FILE} "obj-m += Driver.o\n")
file(APPEND ${KBUILD_FILE} "Driver-objs := ${OBJ_FILES}\n")

add_custom_target(
    DriverModule ALL
    COMMAND make -C /lib/modules/$(uname -r)/build
            M=${CMAKE_BINARY_DIR}
            src=${CMAKE_SOURCE_DIR}
            modules
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM
)

add_custom_target(
    clean_module
    COMMAND make -C /lib/modules/$(uname -r)/build
            M=${CMAKE_BINARY_DIR}
            clean
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)