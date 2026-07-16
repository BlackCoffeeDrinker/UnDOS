
add_library(libkcpp INTERFACE
        include/__config.hpp
        include/algorithm.hpp
        include/array.hpp
        include/atomic.hpp
        include/buffer_string.hpp
        include/char_traits.hpp
        include/chrono.hpp
        include/compare.hpp
        include/concepts.hpp
        include/expected.hpp
        include/functional.hpp
        include/initializer_list.hpp
        include/limits.hpp
        include/memory.hpp
        include/new.hpp
        include/nomm_string.hpp
        include/optional.hpp
        include/ratio.hpp
        include/reverse_iterator.hpp
        include/span.hpp
        include/static_string.hpp
        include/stddef.hpp
        include/strfmt.hpp
        include/string.hpp
        include/string_view.hpp
        include/system_error.hpp
        include/tuple.hpp
        include/type_traits.hpp
        include/utility.hpp
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
