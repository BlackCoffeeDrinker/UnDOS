include(CMakePackageConfigHelpers)

execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE _undos_sdk_compiler_target
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _undos_sdk_target_result)
if (NOT _undos_sdk_target_result EQUAL 0 OR
    NOT _undos_sdk_compiler_target MATCHES "^i[3-6]86-elf($|-)" OR
    NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "The SDK requires an i386 ELF compiler (for example i686-elf-gcc); "
        "selected compiler target: '${_undos_sdk_compiler_target}'. "
        "For native hosted tests, configure with -DTESTING_ONLY=ON.")
endif ()

set(UNDOS_SDK_PREFIX "${CMAKE_BINARY_DIR}/sdk" CACHE PATH "Destination staged by the sdk target")
set(UNDOS_SDK_SYSROOT "sysroot")
set(UNDOS_SDK_LIB "${UNDOS_SDK_SYSROOT}/usr/lib")
set(UNDOS_SDK_INCLUDE "${UNDOS_SDK_SYSROOT}/usr/include")
set(UNDOS_SDK_CMAKE "share/cmake/UnDOS")

# Shared libraries resolve public runtime symbols from the main executable.
set_target_properties(undoscrt undosgui undos_font_resource PROPERTIES
    CXX_VISIBILITY_PRESET default C_VISIBILITY_PRESET default)

add_library(undos_sdk_crt0 OBJECT ../userland/crt/start.S)
add_library(undos_sdk_note OBJECT ../userland/abi_note.S)
add_library(undos_sdk_dso OBJECT dso.cpp)
target_compile_options(undos_sdk_dso PRIVATE -march=i386 -m32 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector)
set_target_properties(undos_sdk_dso PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS undoscrt undosgui undos_font_resource ARCHIVE DESTINATION ${UNDOS_SDK_LIB} COMPONENT SDK)
install(FILES $<TARGET_OBJECTS:undos_sdk_crt0> DESTINATION ${UNDOS_SDK_LIB}
    RENAME crt0.o COMPONENT SDK)
install(FILES $<TARGET_OBJECTS:undos_sdk_note> DESTINATION ${UNDOS_SDK_LIB}
    RENAME undos-abi-note.o COMPONENT SDK)
install(FILES $<TARGET_OBJECTS:undos_sdk_dso> DESTINATION ${UNDOS_SDK_LIB}
    RENAME undos-dso.o COMPONENT SDK)
install(DIRECTORY ../kernel_userland_shared/include/undos
    DESTINATION ${UNDOS_SDK_INCLUDE} COMPONENT SDK
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "abi_note.hpp" EXCLUDE)
install(DIRECTORY ../userland/crt/include/undoscrt ../userland/gui/include/undosgui
    DESTINATION ${UNDOS_SDK_INCLUDE} COMPONENT SDK
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "lifecycle.hpp" EXCLUDE)
install(FILES ../userland/userspace.h DESTINATION ${UNDOS_SDK_INCLUDE} COMPONENT SDK)
install(DIRECTORY ../userland/crt/libc/include/ DESTINATION ${UNDOS_SDK_INCLUDE}
    COMPONENT SDK FILES_MATCHING PATTERN "*.h")
install(FILES ../userland/apps/term/linker.ld DESTINATION ${UNDOS_SDK_LIB}
    RENAME application.ld COMPONENT SDK)
install(FILES UnDOS-toolchain.cmake DESTINATION ${UNDOS_SDK_CMAKE} COMPONENT SDK)
install(DIRECTORY Platform DESTINATION ${UNDOS_SDK_CMAKE} COMPONENT SDK)

configure_package_config_file(UnDOSConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/UnDOSConfig.cmake"
    INSTALL_DESTINATION ${UNDOS_SDK_CMAKE})
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/UnDOSConfigVersion.cmake"
    VERSION ${CMAKE_PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/UnDOSConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/UnDOSConfigVersion.cmake"
    DESTINATION ${UNDOS_SDK_CMAKE} COMPONENT SDK)
install(DIRECTORY examples DESTINATION share/undos COMPONENT SDK)
install(PROGRAMS tests/check-sdk.py DESTINATION share/undos COMPONENT SDK)
install(PROGRAMS driver.py DESTINATION bin RENAME i686-undos-gcc COMPONENT SDK)
install(PROGRAMS driver.py DESTINATION bin RENAME i686-undos-g++ COMPONENT SDK)

add_custom_target(sdk
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --component SDK
        --prefix "${UNDOS_SDK_PREFIX}" --config $<CONFIG>
    DEPENDS undoscrt undosgui undos_font_resource undos_sdk_crt0 undos_sdk_note undos_sdk_dso
    COMMENT "Staging the UnDOS application SDK at ${UNDOS_SDK_PREFIX}"
    VERBATIM)
