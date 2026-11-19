# Kernel components: built as object libraries and linked into the kernel.
# Components see each other only through <name>_Interface targets.
add_subdirectory(Executive)
add_subdirectory(IO)
add_subdirectory(GFX)
add_subdirectory(User)
add_subdirectory(Initial)
add_subdirectory(Input)
add_subdirectory(Profiler)
add_subdirectory(TTY)
add_subdirectory(PnP)
add_subdirectory(Loader)

