cmake_minimum_required(VERSION 3.21)
project(PhotoSlop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Test)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Core engine (no UI): layers, compositing, selections, undo, file IO ----
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/core/*.h
    src/io/*.cpp src/io/*.h)
add_library(photoslop_core STATIC ${CORE_SOURCES})
target_include_directories(photoslop_core PUBLIC src)
target_link_libraries(photoslop_core PUBLIC Qt6::Gui)

# ---- UI library (everything except main.cpp, so GUI tests can link it) ----
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/app/*.h
    src/ui/*.cpp src/ui/*.h
    src/tools/*.cpp src/tools/*.h)
add_library(photoslop_ui STATIC ${UI_SOURCES})
target_link_libraries(photoslop_ui PUBLIC photoslop_core Qt6::Widgets Qt6::Svg)

file(GLOB ICON_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/icons/*.svg)

# ---- Application ----
qt_add_executable(PhotoSlop WIN32 MACOSX_BUNDLE src/main.cpp)
qt_add_resources(PhotoSlop "resources"
    PREFIX "/"
    BASE resources
    FILES ${ICON_FILES} resources/logo.svg)
target_link_libraries(PhotoSlop PRIVATE photoslop_ui)

set_target_properties(PhotoSlop PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.photoslop.app
    MACOSX_BUNDLE_BUNDLE_NAME PhotoSlop
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

install(TARGETS PhotoSlop BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# ---- Tests ----
enable_testing()
qt_add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE photoslop_core Qt6::Test)
add_test(NAME test_core COMMAND test_core)

qt_add_executable(test_ui tests/test_ui.cpp)
qt_add_resources(test_ui "test_resources"
    PREFIX "/"
    BASE resources
    FILES ${ICON_FILES} resources/logo.svg)
target_link_libraries(test_ui PRIVATE photoslop_ui Qt6::Test)
add_test(NAME test_ui COMMAND test_ui)
set_tests_properties(test_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
