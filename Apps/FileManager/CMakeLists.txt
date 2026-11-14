cmake_minimum_required(VERSION 3.27)
project(FileManager LANGUAGES C CXX)
find_package(UnDOS CONFIG REQUIRED)

# File Manager: the UnDOS version of Microsoft WINFILE (winfile/, edited for
# UnDOS) plus the UnDOS shim (undos/). Installed as /System/winfile.elf.
option(FILEMANAGER_BUILD_WINFILE_PORT "Build winfile.elf from the WINFILE sources" ON)

set(WINFILE_PORT_SOURCES
    undos/resources.c
    undos/stubs.c
    undos/win32_stubs.c
    undos/mounts.c)
set(WINFILE_PORT_COPIED_SOURCES
    winfile/winfile.c winfile/wfinit.c winfile/wfdir.c winfile/wfdirrd.c
    winfile/wfdirsrc.c winfile/treectl.c winfile/wftree.c winfile/wfcomman.c
    winfile/wfcopy.c winfile/wfdlgs.c winfile/wfdlgs2.c winfile/wfdlgs3.c
    winfile/wfdos.c winfile/wffile.c winfile/wfutil.c winfile/wfinfo.c
    winfile/wfsearch.c winfile/wfdrives.c winfile/lfn.c
    winfile/lfnmisc.c winfile/numfmt.c winfile/wfmem.c winfile/wfloc.c
    winfile/suggest.c winfile/Resize.c winfile/tbar.c)

if(FILEMANAGER_BUILD_WINFILE_PORT)
    add_executable(fileman ${WINFILE_PORT_SOURCES} ${WINFILE_PORT_COPIED_SOURCES})
    set_target_properties(fileman PROPERTIES OUTPUT_NAME winfile C_STANDARD 11)
    target_compile_definitions(fileman PRIVATE UNDOS)
    target_include_directories(fileman PRIVATE undos winfile)
    # SDK HANDLE is an integer (it matches the kernel ABI); WINFILE assigns
    # NULL to handles everywhere, which is harmless.
    target_compile_options(fileman PRIVATE -include undos_compat.h
        -Wno-int-conversion -Wno-incompatible-pointer-types)
    target_link_libraries(fileman PRIVATE UnDOS::GUI)
endif()

# Regenerates undos/resources.c from winfile/res.rc (checked in; run after
# editing the .rc, .dlg or headers it reads).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(fileman-resources
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../../Scripts/rc2c.py
            ${CMAKE_CURRENT_SOURCE_DIR}/winfile/res.rc
            -o ${CMAKE_CURRENT_SOURCE_DIR}/undos/resources.c
            -DUNDOS -I ${CMAKE_CURRENT_SOURCE_DIR}/undos/rcinc -I ${CMAKE_CURRENT_SOURCE_DIR}/undos
            --pre-include wfdlgs.h --header undos_compat.h --symbol winfile_resources
        COMMENT "rc2c: winfile/res.rc -> undos/resources.c"
        VERBATIM)
endif()

option(FILEMANAGER_BUILD_PROBE "Build the destructive-on-test-folder filesystem conformance probe" OFF)
if(FILEMANAGER_BUILD_PROBE)
    add_executable(fmprobe tests/runtime_probe.cpp)
    target_compile_features(fmprobe PRIVATE cxx_std_20)
    target_link_libraries(fmprobe PRIVATE UnDOS::Application)
endif()
