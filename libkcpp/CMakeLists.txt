
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
        include/string_interface.hpp
        include/string_view.hpp
        include/tuple.hpp
        include/type_traits.hpp
        include/utility.hpp
        include/chrono.hpp
        include/ratio.hpp
        include/functional.hpp
)

target_include_directories(libkcpp INTERFACE include/)
target_compile_options(libkcpp INTERFACE -Wno-error=literal-suffix)

