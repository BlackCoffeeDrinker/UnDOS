add_library(Catch2Main STATIC main.cpp)

target_link_libraries(Catch2Main PUBLIC Catch2::Catch2)
