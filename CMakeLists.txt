cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(NyrexVJ VERSION 0.27.0 LANGUAGES C CXX)

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
    src/core/i18n_it.cpp
    src/core/i18n_pt.cpp
    src/core/i18n_nl.cpp
    src/core/i18n_ja.cpp
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
    src/core/venue_setup.cpp
    src/core/node_graph.cpp
    src/core/node_catalogue.cpp
    src/core/node_compile.cpp
    src/core/node_files.cpp
    src/core/plugin_manifest.cpp
    src/core/plugin_config.cpp
    src/core/dynlib.cpp
    src/core/plugin_host.cpp
    src/core/timecode.cpp
    src/core/ltc.cpp
    src/core/djlink.cpp
    src/core/midi_clock.cpp
    src/core/cue_list.cpp
    src/core/beat_clock.cpp
    src/core/ndi_runtime.cpp
    src/core/ndi_receiver.cpp
    src/core/ndi_sender.cpp
    src/core/stage.cpp
    src/core/stage_json.cpp
    src/core/edit_history.cpp
    src/core/frame_pacer.cpp
    src/core/frame_lock.cpp
    src/core/render_policy.cpp
    src/core/frame_stats.cpp
    src/media/ffmpeg_dyn.cpp
    src/media/camera_capture.cpp
    src/media/hap.cpp
    src/media/media_image.cpp
    src/media/playback_driver.cpp
    src/media/video_decoder.cpp
    src/media/video_hardware.cpp
    src/media/video_probe_data.cpp
    src/core/recovery.cpp
    src/core/crash_handler.cpp
    src/render/effect_library.cpp
    src/render/effects_extra.cpp
    src/core/factory_presets.cpp
    src/core/log.cpp
    src/core/novelty.cpp
    src/core/preset.cpp
    src/core/playhead.cpp
    src/core/live_source.cpp
    src/core/settings.cpp
    src/core/sha256.cpp
    src/control/control_system.cpp
    src/control/djlink_input.cpp
    src/control/sync_system.cpp
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
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND NYREXVJ_CORE_SOURCES src/platform/platform_other.cpp src/platform/platform_linux_update.cpp src/audio/audio_input_linux.cpp src/control/midi_input_linux.cpp src/control/udp_socket_posix.cpp)
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
target_include_directories(nvj_core PUBLIC src plugins/include)
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
    src/app/fonts.cpp
    src/app/bench.cpp
    src/app/output_selftest.cpp
    src/app/theme.cpp
    src/app/ui_browser.cpp
    src/app/ui_deck.cpp
    src/app/ui_control.cpp
    src/app/ui_creator.cpp
    src/app/ui_nodes.cpp
    src/app/ui_settings.cpp
    src/app/ui_video.cpp
    src/app/ui_profile.cpp
    src/app/ui_panels.cpp
    src/app/ui_live.cpp
    src/app/ui_plugins.cpp
    src/app/ui_welcome.cpp
    src/app/ui_recovery.cpp
    src/render/engine.cpp
    src/render/gpu_profiler.cpp
    src/render/gl_util.cpp
    src/render/output_window.cpp
    src/render/texture_history.cpp
    src/render/output_manager.cpp
    src/render/ndi_output.cpp
    src/render/dmx_strip.cpp
    src/app/ui_outputs.cpp
    src/app/ui_stage.cpp
    src/app/ui_sync.cpp
    src/render/scene_player.cpp
    src/render/media_player.cpp
    src/render/video_gpu.cpp
    src/render/live_input.cpp
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
    add_executable(nvj_live_tests tests/test_live.cpp)
    target_link_libraries(nvj_live_tests PRIVATE nvj_core)
    target_include_directories(nvj_live_tests PRIVATE tests)
    add_test(NAME nvj_live_tests COMMAND nvj_live_tests)
    add_executable(nvj_playhead_tests tests/test_playhead.cpp)
    target_link_libraries(nvj_playhead_tests PRIVATE nvj_core)
    target_include_directories(nvj_playhead_tests PRIVATE tests)
    add_test(NAME nvj_playhead_tests COMMAND nvj_playhead_tests)
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
    set(NYREXVJ_UNREAL_CORE_DIR ${CMAKE_SOURCE_DIR}/integrations/unreal/NyrexVJ/Source/NyrexVJ/Private)
    add_library(nvj_unreal_core STATIC ${NYREXVJ_UNREAL_CORE_DIR}/Core/NyrexOsc.cpp ${NYREXVJ_UNREAL_CORE_DIR}/Core/NyrexState.cpp)
    set_target_properties(nvj_unreal_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_include_directories(nvj_unreal_core PUBLIC ${NYREXVJ_UNREAL_CORE_DIR})
    add_executable(nvj_unreal_core_tests tests/test_unreal_core.cpp)
    target_link_libraries(nvj_unreal_core_tests PRIVATE nvj_core nvj_unreal_core)
    target_include_directories(nvj_unreal_core_tests PRIVATE tests)
    add_test(NAME nvj_unreal_core_tests COMMAND nvj_unreal_core_tests)
    add_executable(nvj_media_tests tests/test_media.cpp)
    target_link_libraries(nvj_media_tests PRIVATE nvj_core)
    target_include_directories(nvj_media_tests PRIVATE tests)
    target_compile_definitions(nvj_media_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_media_tests COMMAND nvj_media_tests)
    add_executable(nvj_output_tests tests/test_outputs.cpp)
    target_link_libraries(nvj_output_tests PRIVATE nvj_core)
    target_include_directories(nvj_output_tests PRIVATE tests)
    add_test(NAME nvj_output_tests COMMAND nvj_output_tests)
    add_executable(nvj_beat_clock_tests tests/test_beat_clock.cpp)
    target_link_libraries(nvj_beat_clock_tests PRIVATE nvj_core)
    target_include_directories(nvj_beat_clock_tests PRIVATE tests)
    add_test(NAME nvj_beat_clock_tests COMMAND nvj_beat_clock_tests)
    add_executable(nvj_timecode_tests tests/test_timecode.cpp)
    target_link_libraries(nvj_timecode_tests PRIVATE nvj_core)
    target_include_directories(nvj_timecode_tests PRIVATE tests)
    add_test(NAME nvj_timecode_tests COMMAND nvj_timecode_tests)
    add_executable(nvj_frame_lock_tests tests/test_frame_lock.cpp)
    target_link_libraries(nvj_frame_lock_tests PRIVATE nvj_core)
    target_include_directories(nvj_frame_lock_tests PRIVATE tests)
    add_test(NAME nvj_frame_lock_tests COMMAND nvj_frame_lock_tests)
    add_executable(nvj_djlink_tests tests/test_djlink.cpp)
    target_link_libraries(nvj_djlink_tests PRIVATE nvj_core)
    target_include_directories(nvj_djlink_tests PRIVATE tests)
    add_test(NAME nvj_djlink_tests COMMAND nvj_djlink_tests)
    add_executable(nvj_midi_clock_tests tests/test_midi_clock.cpp)
    target_link_libraries(nvj_midi_clock_tests PRIVATE nvj_core)
    target_include_directories(nvj_midi_clock_tests PRIVATE tests)
    add_test(NAME nvj_midi_clock_tests COMMAND nvj_midi_clock_tests)
    add_executable(nvj_sync_system_tests tests/test_sync_system.cpp)
    target_link_libraries(nvj_sync_system_tests PRIVATE nvj_core)
    target_include_directories(nvj_sync_system_tests PRIVATE tests)
    add_test(NAME nvj_sync_system_tests COMMAND nvj_sync_system_tests)
    add_executable(nvj_venue_tests tests/test_venues.cpp)
    target_link_libraries(nvj_venue_tests PRIVATE nvj_core)
    target_include_directories(nvj_venue_tests PRIVATE tests)
    add_test(NAME nvj_venue_tests COMMAND nvj_venue_tests)
    add_executable(nvj_node_tests tests/test_nodes.cpp)
    target_link_libraries(nvj_node_tests PRIVATE nvj_core)
    target_include_directories(nvj_node_tests PRIVATE tests)
    add_test(NAME nvj_node_tests COMMAND nvj_node_tests)
    add_library(fake_ndi SHARED tests/fake_ndi.cpp)
    set_target_properties(fake_ndi PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "")
    add_executable(nvj_ndi_tests tests/test_ndi.cpp)
    target_link_libraries(nvj_ndi_tests PRIVATE nvj_core fake_ndi)
    target_include_directories(nvj_ndi_tests PRIVATE tests)
    target_compile_definitions(nvj_ndi_tests PRIVATE NYREXVJ_FAKE_NDI_PATH="$<TARGET_FILE:fake_ndi>")
    add_test(NAME nvj_ndi_tests COMMAND nvj_ndi_tests)
    add_executable(nvj_ndi_receive_tests tests/test_ndi_receive.cpp)
    target_link_libraries(nvj_ndi_receive_tests PRIVATE nvj_core fake_ndi)
    target_include_directories(nvj_ndi_receive_tests PRIVATE tests)
    target_compile_definitions(nvj_ndi_receive_tests PRIVATE NYREXVJ_FAKE_NDI_PATH="$<TARGET_FILE:fake_ndi>")
    add_test(NAME nvj_ndi_receive_tests COMMAND nvj_ndi_receive_tests)
    add_executable(nvj_camera_tests tests/test_camera.cpp)
    target_link_libraries(nvj_camera_tests PRIVATE nvj_core)
    target_include_directories(nvj_camera_tests PRIVATE tests)
    add_test(NAME nvj_camera_tests COMMAND nvj_camera_tests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(fake_pulse SHARED tests/fake_pulse.cpp)
        set_target_properties(fake_pulse PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "")
        add_executable(nvj_linux_audio_tests tests/test_linux_audio.cpp)
        target_link_libraries(nvj_linux_audio_tests PRIVATE nvj_core fake_pulse)
        target_include_directories(nvj_linux_audio_tests PRIVATE tests)
        target_compile_definitions(nvj_linux_audio_tests PRIVATE NYREXVJ_FAKE_PULSE_PATH="$<TARGET_FILE:fake_pulse>")
        add_test(NAME nvj_linux_audio_tests COMMAND nvj_linux_audio_tests)
        add_library(fake_alsa SHARED tests/fake_alsa.cpp)
        set_target_properties(fake_alsa PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "")
        add_executable(nvj_linux_midi_tests tests/test_linux_midi.cpp)
        target_link_libraries(nvj_linux_midi_tests PRIVATE nvj_core fake_alsa)
        target_include_directories(nvj_linux_midi_tests PRIVATE tests)
        target_compile_definitions(nvj_linux_midi_tests PRIVATE NYREXVJ_FAKE_ALSA_PATH="$<TARGET_FILE:fake_alsa>")
        add_test(NAME nvj_linux_midi_tests COMMAND nvj_linux_midi_tests)
        add_executable(nvj_linux_update_tests tests/test_linux_update.cpp)
        target_link_libraries(nvj_linux_update_tests PRIVATE nvj_core)
        target_include_directories(nvj_linux_update_tests PRIVATE tests)
        add_test(NAME nvj_linux_update_tests COMMAND nvj_linux_update_tests)
    endif()
    add_executable(nvj_stage_tests tests/test_stage.cpp)
    target_link_libraries(nvj_stage_tests PRIVATE nvj_core)
    target_include_directories(nvj_stage_tests PRIVATE tests)
    add_test(NAME nvj_stage_tests COMMAND nvj_stage_tests)
    add_executable(nvj_shader_tests tests/test_shaders.cpp)
    target_link_libraries(nvj_shader_tests PRIVATE nvj_core)
    target_include_directories(nvj_shader_tests PRIVATE tests)
    add_test(NAME nvj_shader_tests COMMAND nvj_shader_tests)
    function(nyrexvj_plugin_module target source output_name output_dir)
        add_library(${target} MODULE ${source})
        target_include_directories(${target} PRIVATE plugins/include)
        set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME ${output_name} C_STANDARD 99 C_VISIBILITY_PRESET hidden LIBRARY_OUTPUT_DIRECTORY "$<1:${output_dir}>" RUNTIME_OUTPUT_DIRECTORY "$<1:${output_dir}>")
        if(NOT WIN32)
            target_link_libraries(${target} PRIVATE m)
        endif()
    endfunction()
    nyrexvj_plugin_module(nyrexvj_example_plugin plugins/example/example_plugin.c nyrexvj_example ${CMAKE_BINARY_DIR}/plugins/nyrexvj-example)
    add_custom_command(TARGET nyrexvj_example_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/plugins/example/plugin.json $<TARGET_FILE_DIR:nyrexvj_example_plugin>/plugin.json)
    set(NYREXVJ_BROKEN_PLUGINS slow:1 hang:2 fail:3 crash:4 range:5 createfail:6 entryfail:7 noentry:8 duplicate:9 mismatch:10 loadslow:11 overflow:12 imagechanged:13 slowframe:14 toomany:15 idmismatch:16 nomodulation:17 crashcreate:18 crashload:19 logspam:20)
    add_executable(nvj_plugin_tests tests/test_plugins.cpp)
    target_link_libraries(nvj_plugin_tests PRIVATE nvj_core)
    target_include_directories(nvj_plugin_tests PRIVATE tests)
    foreach(entry ${NYREXVJ_BROKEN_PLUGINS})
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 broken_name)
        list(GET parts 1 broken_mode)
        nyrexvj_plugin_module(nvj_broken_${broken_name} tests/plugins/broken_plugin.c broken_${broken_name} ${CMAKE_BINARY_DIR}/test_plugins)
        target_compile_definitions(nvj_broken_${broken_name} PRIVATE NVJ_BROKEN_MODE=${broken_mode})
        string(TOUPPER ${broken_name} broken_upper)
        target_compile_definitions(nvj_plugin_tests PRIVATE "NVJ_PLUGIN_${broken_upper}=\"$<TARGET_FILE:nvj_broken_${broken_name}>\"")
        add_dependencies(nvj_plugin_tests nvj_broken_${broken_name})
    endforeach()
    target_compile_definitions(nvj_plugin_tests PRIVATE "NVJ_PLUGIN_EXAMPLE=\"$<TARGET_FILE:nyrexvj_example_plugin>\"" "NVJ_PLUGIN_EXAMPLE_MANIFEST=\"${CMAKE_SOURCE_DIR}/plugins/example/plugin.json\"")
    add_dependencies(nvj_plugin_tests nyrexvj_example_plugin)
    add_test(NAME nvj_plugin_tests COMMAND nvj_plugin_tests)
    set_tests_properties(nvj_plugin_tests PROPERTIES TIMEOUT 300)
    if(WIN32)
        add_executable(nvj_update_tests tests/test_update_replace.cpp)
        target_link_libraries(nvj_update_tests PRIVATE nvj_core)
        target_include_directories(nvj_update_tests PRIVATE tests)
        add_test(NAME nvj_update_tests COMMAND nvj_update_tests)
    endif()
endif()
