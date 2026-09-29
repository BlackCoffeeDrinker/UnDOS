cmake_minimum_required(VERSION 3.16)
project(init CXX ASM)

add_library(init_shared_object OBJECT shared_message.cpp)
target_compile_options(init_shared_object PRIVATE -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fPIC -fvisibility=default -m32)
set(INIT_SHARED_FILE ${CMAKE_CURRENT_BINARY_DIR}/initlib.so)
add_custom_command(OUTPUT ${INIT_SHARED_FILE}
    COMMAND ${CMAKE_LINKER} -shared -soname initlib.so -o ${INIT_SHARED_FILE} $<TARGET_OBJECTS:init_shared_object>
    DEPENDS init_shared_object $<TARGET_OBJECTS:init_shared_object>
)
add_custom_target(init_shared DEPENDS ${INIT_SHARED_FILE})

add_executable(init main.cpp)

target_include_directories(init PRIVATE ${CMAKE_SOURCE_DIR}/userland)
target_link_libraries(init PRIVATE undoscrt KernelUserlandShared)
# Pass the .so as a link option: CMake does not know the UnDOS platform and
# rewrites a .so path in target_link_libraries to -linitlib, which ld cannot find.
target_link_options(init PRIVATE ${INIT_SHARED_FILE})
set_property(TARGET init APPEND PROPERTY LINK_DEPENDS ${INIT_SHARED_FILE})
add_dependencies(init init_shared)

set_target_properties(init PROPERTIES POSITION_INDEPENDENT_CODE OFF)

target_compile_options(init PRIVATE -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-pic -fno-pie -m32)
target_link_options(init PRIVATE -nostdlib -no-pie -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
