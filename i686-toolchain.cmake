set(CMAKE_SYSTEM_NAME UnDOS)
set(CMAKE_SYSTEM_PROCESSOR i386)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARCH)

# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Don't link with anything by default unless we say so
set(CMAKE_C_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C Libraries")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C++ Libraries")

#set(CMAKE_SYSROOT  )
#set(CMAKE_STAGING_PREFIX )

set(tools                   /usr/local/cross)
set(CMAKE_C_COMPILER        ${tools}/bin/i686-elf-gcc)
set(CMAKE_CXX_COMPILER      ${tools}/bin/i686-elf-g++)

set(CMAKE_LINKER            ${tools}/bin/i686-elf-ld)
# set(CMAKE_CXX_LINK_EXECUTABLE  "<CMAKE_LINKER> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
# set(CMAKE_C_LINK_EXECUTABLE    "<CMAKE_LINKER> <CMAKE_C_LINK_FLAGS>   <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

#set(CMAKE_ASM_COMPILER nasm)
#set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS ${CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS} s S)
#set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")
#set(CMAKE_ASM_NASM_LINK_EXECUTABLE "ld <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
#set(CMAKE_ASM_NASM_OBJECT_FORMAT elf32)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_compile_options(
        "$<$<COMPILE_LANGUAGE:ASM_NASM>:-felf32>"

        "$<$<COMPILE_LANGUAGE:ASM>:-ffreestanding>"
        "$<$<COMPILE_LANGUAGE:C>:-ffreestanding>"
        "$<$<COMPILE_LANGUAGE:CXX>:-ffreestanding>"

        "$<$<COMPILE_LANGUAGE:C>:-march=i386>"
        "$<$<COMPILE_LANGUAGE:CXX>:-march=i386>"
)

add_link_options(-nostdlib -nostartfiles -nodefaultlibs -nostdinc -nolibc -nostdlib++ -static-libgcc)
