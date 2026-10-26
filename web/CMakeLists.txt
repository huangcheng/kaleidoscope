cmake_minimum_required(VERSION 3.19)
project(KaleidoscopeWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(kaleidoscope
    ../sony_core.cpp
    web_main.cpp
)

target_include_directories(kaleidoscope PRIVATE ..)

# Emscripten-specific flags
if(EMSCRIPTEN)
    set_target_properties(kaleidoscope PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "\
            -s EXPORTED_FUNCTIONS=['_malloc','_free','_kaleidoscope_init','_kaleidoscope_step','_kaleidoscope_key','_kaleidoscope_get_framebuffer','_kaleidoscope_get_dac','_kaleidoscope_phase','_kaleidoscope_quit','_kaleidoscope_frame_dt'] \
            -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8'] \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s MODULARIZE=1 \
            -s EXPORT_NAME='createKaleidoscope' \
            -s ENVIRONMENT='web' \
            -s SINGLE_FILE=1 \
            --no-entry \
            -O2"
    )
endif()
