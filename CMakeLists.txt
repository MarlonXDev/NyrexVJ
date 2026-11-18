cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(NyrexVJ VERSION 0.2.0 LANGUAGES C CXX)

option(NYREXVJ_RELEASE "Build an official release (no -dev suffix)" OFF)
option(NYREXVJ_BUILD_TESTS "Build the unit tests" ON)
set(NYREXVJ_BUILD_NUMBER "0" CACHE STRING "Build number stamped into the program")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(NYREXVJ_RELEASE)
    set(NYREXVJ_VERSION_STRING "${PROJECT_VERSION}")
else()
    set(NYREXVJ_VERSION_STRING "${PROJECT_VERSION}-dev")
endif()

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.0-docking
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw imgui)

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

set(NYREXVJ_CORE_SOURCES
    src/clip_library.cpp
    src/composition.cpp
    src/effect_library.cpp
    src/factory_presets.cpp
    src/log.cpp
    src/preset.cpp
    src/settings.cpp
    src/sha256.cpp
    src/update_logic.cpp
    src/updater.cpp
)

if(WIN32)
    list(APPEND NYREXVJ_CORE_SOURCES src/platform_win.cpp)
else()
    list(APPEND NYREXVJ_CORE_SOURCES src/platform_other.cpp)
endif()

add_library(nvj_core STATIC ${NYREXVJ_CORE_SOURCES})
target_include_directories(nvj_core PUBLIC src)
target_include_directories(nvj_core SYSTEM PUBLIC third_party/json)
target_compile_definitions(nvj_core PUBLIC
    NYREXVJ_VERSION="${NYREXVJ_VERSION_STRING}"
    NYREXVJ_BUILD_NUMBER=${NYREXVJ_BUILD_NUMBER}
)

if(WIN32)
    target_link_libraries(nvj_core PUBLIC winhttp shell32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(nvj_core PUBLIC Threads::Threads)
endif()

add_executable(NyrexVJ
    src/main.cpp
    src/app.cpp
    src/engine.cpp
    src/gl_util.cpp
    src/output_window.cpp
    src/theme.cpp
    src/stb_impl.cpp
)

target_include_directories(NyrexVJ PRIVATE src third_party/stb)
target_link_libraries(NyrexVJ PRIVATE nvj_core glad imgui glfw)
target_compile_definitions(NyrexVJ PRIVATE GLFW_INCLUDE_NONE)

if(MSVC)
    target_compile_options(NyrexVJ PRIVATE /W3 /utf-8)
    target_compile_options(nvj_core PRIVATE /W3 /utf-8)
    target_link_options(NyrexVJ PRIVATE "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS;/ENTRY:mainCRTStartup>")
else()
    target_compile_options(NyrexVJ PRIVATE -Wall -Wextra)
    target_compile_options(nvj_core PRIVATE -Wall -Wextra)
endif()

if(NYREXVJ_BUILD_TESTS)
    enable_testing()
    add_executable(nvj_tests tests/tests.cpp)
    target_link_libraries(nvj_tests PRIVATE nvj_core)
    add_test(NAME nvj_tests COMMAND nvj_tests)
endif()
