cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(NyrexVJ VERSION 0.16.0 LANGUAGES C CXX)

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

function(nyrexvj_embed_text symbol input output)
    file(READ "${input}" content HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
    file(WRITE "${output}" "extern const unsigned char ${symbol}[];\nextern const unsigned long long ${symbol}Size;\nconst unsigned char ${symbol}[] = {${bytes}0x00};\nconst unsigned long long ${symbol}Size = sizeof(${symbol}) - 1;\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()

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

if(WIN32)
    FetchContent_Declare(
        spout2
        GIT_REPOSITORY https://github.com/leadedge/Spout2.git
        GIT_TAG 2.007.017
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR nyrexvj_skip_upstream_cmake
    )
    FetchContent_MakeAvailable(glfw imgui spout2)
else()
    FetchContent_MakeAvailable(glfw imgui)
endif()

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

if(WIN32)
    set(SPOUT_DIR ${spout2_SOURCE_DIR}/SPOUTSDK/SpoutGL)
    add_library(spout_gl STATIC
        ${SPOUT_DIR}/Spout.cpp
        ${SPOUT_DIR}/SpoutCopy.cpp
        ${SPOUT_DIR}/SpoutDirectX.cpp
        ${SPOUT_DIR}/SpoutFrameCount.cpp
        ${SPOUT_DIR}/SpoutGL.cpp
        ${SPOUT_DIR}/SpoutGLextensions.cpp
        ${SPOUT_DIR}/SpoutSenderNames.cpp
        ${SPOUT_DIR}/SpoutSharedMemory.cpp
        ${SPOUT_DIR}/SpoutUtils.cpp
    )
    target_include_directories(spout_gl PUBLIC ${SPOUT_DIR})
    target_link_libraries(spout_gl PUBLIC opengl32 user32 gdi32 advapi32 shell32 ole32 oleaut32 uuid d3d11 dxgi d3d9 version winmm comctl32 comdlg32)
    if(MSVC)
        target_compile_options(spout_gl PRIVATE /w /utf-8)
    endif()
endif()

set(NYREXVJ_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${NYREXVJ_GENERATED_DIR})
nyrexvj_embed_text(nvjChangelogEn ${CMAKE_SOURCE_DIR}/CHANGELOG.md ${NYREXVJ_GENERATED_DIR}/changelog_en.cpp)
nyrexvj_embed_text(nvjChangelogDe ${CMAKE_SOURCE_DIR}/CHANGELOG.de.md ${NYREXVJ_GENERATED_DIR}/changelog_de.cpp)

set(NYREXVJ_CORE_SOURCES
    ${NYREXVJ_GENERATED_DIR}/changelog_en.cpp
    ${NYREXVJ_GENERATED_DIR}/changelog_de.cpp
    src/update/changelog.cpp
    src/core/i18n.cpp
    src/core/i18n_de.cpp
    src/core/i18n_es.cpp
    src/core/i18n_fr.cpp
    src/audio/audio_analyzer.cpp
    src/audio/audio_system.cpp
    src/audio/synthetic_source.cpp
    src/render/asset_import.cpp
    src/render/clip_library.cpp
    src/render/shader_source.cpp
    src/render/clips_extra.cpp
    src/core/clip_pack.cpp
    src/scene3d/builtin_docs.cpp
    src/scene3d/doc_build.cpp
    src/scene3d/glb_writer.cpp
    src/scene3d/gltf.cpp
    src/scene3d/image_decode.cpp
    src/scene3d/primitives.cpp
    src/scene3d/procedural.cpp
    src/scene3d/scene_doc.cpp
    src/scene3d/scene_model.cpp
    src/scene3d/scene_pack.cpp
    src/core/composition.cpp
    src/core/modulation.cpp
    src/core/output_map.cpp
    src/core/ndi_sender.cpp
    src/core/stage.cpp
    src/core/stage_json.cpp
    src/core/edit_history.cpp
    src/core/frame_stats.cpp
    src/media/ffmpeg_dyn.cpp
    src/media/media_image.cpp
    src/media/video_decoder.cpp
    src/core/recovery.cpp
    src/core/crash_handler.cpp
    src/render/effect_library.cpp
    src/render/effects_extra.cpp
    src/core/factory_presets.cpp
    src/core/log.cpp
    src/core/novelty.cpp
    src/core/preset.cpp
    src/core/settings.cpp
    src/core/sha256.cpp
    src/control/control_system.cpp
    src/control/control_targets.cpp
    src/control/midi_map.cpp
    src/control/osc.cpp
    src/control/osc_out.cpp
    src/control/stage_out.cpp
    src/update/update_logic.cpp
    src/update/updater.cpp
)

if(WIN32)
    list(APPEND NYREXVJ_CORE_SOURCES src/platform/platform_win.cpp src/audio/audio_input_win.cpp src/control/midi_input_win.cpp src/control/udp_socket_win.cpp)
else()
    list(APPEND NYREXVJ_CORE_SOURCES src/platform/platform_other.cpp src/audio/audio_input_other.cpp src/control/midi_input_other.cpp src/control/udp_socket_posix.cpp)
endif()

add_library(nvj_core STATIC ${NYREXVJ_CORE_SOURCES})
if(NOT WIN32)
    target_link_libraries(nvj_core PUBLIC ${CMAKE_DL_LIBS})
endif()

option(NYREXVJ_VIDEO "Build video clip support (needs the FFmpeg headers; the FFmpeg libraries are loaded at run time)" ON)
set(NYREXVJ_FFMPEG_ROOT "" CACHE PATH "Folder with the FFmpeg include directory (headers only are needed)")
set(NYREXVJ_HAVE_FFMPEG OFF)
if(NYREXVJ_VIDEO)
    find_path(NYREXVJ_FFMPEG_INCLUDE libavcodec/avcodec.h
        HINTS "${NYREXVJ_FFMPEG_ROOT}/include" "$ENV{FFMPEG_ROOT}/include"
        PATH_SUFFIXES ffmpeg)
    if(NYREXVJ_FFMPEG_INCLUDE)
        set(NYREXVJ_HAVE_FFMPEG ON)
        message(STATUS "Video clips: FFmpeg headers found in ${NYREXVJ_FFMPEG_INCLUDE}")
        target_compile_definitions(nvj_core PRIVATE NYREXVJ_HAVE_FFMPEG=1)
        target_include_directories(nvj_core SYSTEM PRIVATE "${NYREXVJ_FFMPEG_INCLUDE}")
        if(NOT WIN32)
            target_link_libraries(nvj_core PUBLIC ${CMAKE_DL_LIBS})
        endif()
    else()
        message(STATUS "Video clips: FFmpeg headers not found, video support is left out of this build")
    endif()
endif()
target_include_directories(nvj_core PUBLIC src)
target_include_directories(nvj_core SYSTEM PUBLIC third_party/json)
target_include_directories(nvj_core SYSTEM PRIVATE third_party/stb)
target_compile_definitions(nvj_core PUBLIC
    NYREXVJ_VERSION="${NYREXVJ_VERSION_STRING}"
    NYREXVJ_BUILD_NUMBER=${NYREXVJ_BUILD_NUMBER}
)

if(WIN32)
    target_link_libraries(nvj_core PUBLIC winhttp shell32 ole32 ws2_32 winmm psapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(nvj_core PUBLIC Threads::Threads)
endif()

set(NYREXVJ_APP_SOURCES
    src/main.cpp
    src/app/app.cpp
    src/app/bench.cpp
    src/app/theme.cpp
    src/app/ui_browser.cpp
    src/app/ui_deck.cpp
    src/app/ui_control.cpp
    src/app/ui_creator.cpp
    src/app/ui_settings.cpp
    src/app/ui_panels.cpp
    src/app/ui_welcome.cpp
    src/app/ui_recovery.cpp
    src/render/engine.cpp
    src/render/gl_util.cpp
    src/render/output_window.cpp
    src/render/output_manager.cpp
    src/render/ndi_output.cpp
    src/render/dmx_strip.cpp
    src/app/ui_outputs.cpp
    src/app/ui_stage.cpp
    src/render/scene_player.cpp
    src/render/media_player.cpp
    src/render/stb_impl.cpp
)

if(WIN32)
    list(APPEND NYREXVJ_APP_SOURCES src/render/spout_output_win.cpp src/render/spout_input_win.cpp)
    set(NYREXVJ_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/nyrexvj.ico")
    configure_file(resources/nyrexvj.rc.in ${CMAKE_BINARY_DIR}/nyrexvj.rc @ONLY)
    list(APPEND NYREXVJ_APP_SOURCES ${CMAKE_BINARY_DIR}/nyrexvj.rc)
else()
    list(APPEND NYREXVJ_APP_SOURCES src/render/spout_output_other.cpp src/render/spout_input_other.cpp)
endif()

add_executable(NyrexVJ ${NYREXVJ_APP_SOURCES})

target_include_directories(NyrexVJ PRIVATE src third_party/stb)
target_link_libraries(NyrexVJ PRIVATE nvj_core glad imgui glfw)
if(WIN32)
    target_link_libraries(NyrexVJ PRIVATE spout_gl)
endif()
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
    add_executable(nvj_audio_tests tests/test_audio.cpp)
    target_link_libraries(nvj_audio_tests PRIVATE nvj_core)
    target_include_directories(nvj_audio_tests PRIVATE tests)
    add_test(NAME nvj_audio_tests COMMAND nvj_audio_tests)
    add_executable(nvj_control_tests tests/test_control.cpp)
    target_link_libraries(nvj_control_tests PRIVATE nvj_core)
    target_include_directories(nvj_control_tests PRIVATE tests)
    add_test(NAME nvj_control_tests COMMAND nvj_control_tests)
    add_executable(nvj_scene3d_tests tests/test_scene3d.cpp)
    target_link_libraries(nvj_scene3d_tests PRIVATE nvj_core)
    target_include_directories(nvj_scene3d_tests PRIVATE tests)
    target_include_directories(nvj_scene3d_tests SYSTEM PRIVATE third_party/stb)
    add_test(NAME nvj_scene3d_tests COMMAND nvj_scene3d_tests)
    add_executable(nvj_creator_tests tests/test_creator.cpp)
    target_link_libraries(nvj_creator_tests PRIVATE nvj_core)
    target_include_directories(nvj_creator_tests PRIVATE tests)
    add_test(NAME nvj_creator_tests COMMAND nvj_creator_tests)
    add_executable(nvj_i18n_tests tests/test_i18n.cpp)
    target_link_libraries(nvj_i18n_tests PRIVATE nvj_core)
    target_include_directories(nvj_i18n_tests PRIVATE tests)
    add_test(NAME nvj_i18n_tests COMMAND nvj_i18n_tests)
    add_executable(nvj_media_tests tests/test_media.cpp)
    target_link_libraries(nvj_media_tests PRIVATE nvj_core)
    target_include_directories(nvj_media_tests PRIVATE tests)
    target_compile_definitions(nvj_media_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_media_tests COMMAND nvj_media_tests)
    add_executable(nvj_output_tests tests/test_outputs.cpp)
    target_link_libraries(nvj_output_tests PRIVATE nvj_core)
    target_include_directories(nvj_output_tests PRIVATE tests)
    add_test(NAME nvj_output_tests COMMAND nvj_output_tests)
    add_library(fake_ndi SHARED tests/fake_ndi.cpp)
    set_target_properties(fake_ndi PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "")
    add_executable(nvj_ndi_tests tests/test_ndi.cpp)
    target_link_libraries(nvj_ndi_tests PRIVATE nvj_core fake_ndi)
    target_include_directories(nvj_ndi_tests PRIVATE tests)
    target_compile_definitions(nvj_ndi_tests PRIVATE NYREXVJ_FAKE_NDI_PATH="$<TARGET_FILE:fake_ndi>")
    add_test(NAME nvj_ndi_tests COMMAND nvj_ndi_tests)
    add_executable(nvj_stage_tests tests/test_stage.cpp)
    target_link_libraries(nvj_stage_tests PRIVATE nvj_core)
    target_include_directories(nvj_stage_tests PRIVATE tests)
    add_test(NAME nvj_stage_tests COMMAND nvj_stage_tests)
    add_executable(nvj_shader_tests tests/test_shaders.cpp)
    target_link_libraries(nvj_shader_tests PRIVATE nvj_core)
    target_include_directories(nvj_shader_tests PRIVATE tests)
    add_test(NAME nvj_shader_tests COMMAND nvj_shader_tests)
    if(WIN32)
        add_executable(nvj_update_tests tests/test_update_replace.cpp)
        target_link_libraries(nvj_update_tests PRIVATE nvj_core)
        target_include_directories(nvj_update_tests PRIVATE tests)
        add_test(NAME nvj_update_tests COMMAND nvj_update_tests)
    endif()
endif()
