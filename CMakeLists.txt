cmake_minimum_required(VERSION 3.19)
project(Kaleidoscope LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

# ─── Source files ───────────────────────────────────────────────
set(SOURCES
    main.cpp
    screensaverwidget.cpp
    sony_core.cpp
)

set(HEADERS
    screensaverwidget.h
    sony_core.h
    font8x8.h
)

# ─── Platform-specific build targets ──────────────────────────
if(WIN32)
    # Windows: Build as .scr (screensaver executable)
    add_executable(Kaleidoscope WIN32 ${SOURCES} ${HEADERS})
    set_target_properties(Kaleidoscope PROPERTIES
        OUTPUT_NAME "Kaleidoscope"
        SUFFIX ".scr"
    )
    target_link_libraries(Kaleidoscope PRIVATE Qt6::Core Qt6::Widgets)

elseif(APPLE)
    # macOS: Build as .saver bundle
    add_executable(Kaleidoscope MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(Kaleidoscope PROPERTIES
        OUTPUT_NAME "Kaleidoscope"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Kaleidoscope"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.kaleidoscope.screensaver"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    )
    target_link_libraries(Kaleidoscope PRIVATE Qt6::Core Qt6::Widgets "-framework Cocoa")

    # Install into ~/Library/Screen Savers or /Library/Screen Savers
    install(TARGETS Kaleidoscope
        BUNDLE DESTINATION "$ENV{HOME}/Library/Screen Savers"
    )

else()
    # Linux: Standard executable (installed via XDG desktop entry)
    add_executable(Kaleidoscope ${SOURCES} ${HEADERS})
    target_link_libraries(Kaleidoscope PRIVATE Qt6::Core Qt6::Widgets)

    # Install binary + desktop entry for screensaver selection
    install(TARGETS Kaleidoscope DESTINATION bin)
    install(FILES kaleidoscope.desktop DESTINATION share/applications)
endif()
