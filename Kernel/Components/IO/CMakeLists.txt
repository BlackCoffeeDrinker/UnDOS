# IO: I/O objects and streams, VFS, DMA and the IO syscalls.
add_library(kernel_io OBJECT
        src/io.cpp
        src/vfs.cpp
        src/dma_manager.cpp
        src/syscalls/IO.cpp
)
undos_kernel_component(kernel_io IO)
