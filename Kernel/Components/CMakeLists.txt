# Kernel components: built as object libraries and linked into the kernel.
# User depends on GFX; GFX does not depend on User.
add_subdirectory(GFX)
add_subdirectory(User)
