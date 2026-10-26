cmake_minimum_required(VERSION 3.19)
project(KaleidoscopeWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(kaleidoscope_web
    ../sony_core.cpp
    web_qt_main.cpp
)

target_include_directories(kaleidoscope_web PRIVATE ..)
target_link_libraries(kaleidoscope_web PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(EMSCRIPTEN)
    set_target_properties(kaleidoscope_web PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "\
            -s ALLOW_MEMORY_GROWTH=1 \
            -s ENVIRONMENT='web' \
            -s FORCE_FILESYSTEM=0 \
            -lembind \
            -O2"
    )
endif()
