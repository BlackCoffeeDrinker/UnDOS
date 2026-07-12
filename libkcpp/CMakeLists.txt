
add_library(libkcpp INTERFACE
        include/__config.hpp
        include/array.hpp
        include/buffer_string.hpp
        include/char_traits.hpp
        include/compare.hpp
        include/concepts.hpp
        include/limits.hpp
        include/nomm_string.hpp
        include/optional.hpp
        include/reverse_iterator.hpp
        include/stddef.hpp
        include/strfmt.hpp
        include/string_view.hpp
        include/tuple.hpp
        include/type_traits.hpp
        include/utility.hpp
        include/chrono.hpp
        include/ratio.hpp
        include/functional.hpp
        include/memory.hpp
        include/new.hpp
        include/static_string.hpp
        include/expected.hpp
        include/system_error.hpp
        include/__system_error/error_code.hpp
)

target_include_directories(libkcpp INTERFACE include/)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(libkcpp INTERFACE
            -Wno-error=literal-suffix
            -Wno-literal-suffix
    )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(libkcpp INTERFACE
            -Wno-reserved-user-defined-literal
            -Wno-user-defined-literals
    )
endif ()
