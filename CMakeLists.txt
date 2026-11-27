cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(NyrexVJ VERSION 0.62.0 LANGUAGES C CXX)

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

option(NYREXVJ_MESHOPT "Read glTF files compressed with EXT_meshopt_compression (decoder of meshoptimizer, MIT license)" ON)
option(NYREXVJ_DRACO "Read glTF files compressed with KHR_draco_mesh_compression (Draco, Apache 2.0 license; large, off by default)" OFF)

if(NYREXVJ_MESHOPT)
    FetchContent_Declare(
        meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG v1.3
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR nyrexvj_skip_upstream_cmake
    )
    FetchContent_MakeAvailable(meshoptimizer)
    add_library(meshopt_decoder STATIC
        ${meshoptimizer_SOURCE_DIR}/src/vertexcodec.cpp
        ${meshoptimizer_SOURCE_DIR}/src/indexcodec.cpp
        ${meshoptimizer_SOURCE_DIR}/src/vertexfilter.cpp
        ${meshoptimizer_SOURCE_DIR}/src/allocator.cpp
    )
    target_include_directories(meshopt_decoder PUBLIC ${meshoptimizer_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(meshopt_decoder PRIVATE /w)
    endif()
endif()

if(NYREXVJ_DRACO)
    set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
    set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
    set(DRACO_GLTF_BITSTREAM ON CACHE BOOL "" FORCE)
    set(DRACO_INSTALL OFF CACHE BOOL "" FORCE)
    set(DRACO_BACKWARDS_COMPATIBILITY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        draco
        GIT_REPOSITORY https://github.com/google/draco.git
        GIT_TAG 1.5.7
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(draco)
endif()

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
nyrexvj_embed_text(nvjManualEn ${CMAKE_SOURCE_DIR}/docs/manual/manual.en.md ${NYREXVJ_GENERATED_DIR}/manual_en.cpp)
nyrexvj_embed_text(nvjManualDe ${CMAKE_SOURCE_DIR}/docs/manual/manual.de.md ${NYREXVJ_GENERATED_DIR}/manual_de.cpp)

set(NYREXVJ_CORE_SOURCES
    ${NYREXVJ_GENERATED_DIR}/changelog_en.cpp
    ${NYREXVJ_GENERATED_DIR}/changelog_de.cpp
    ${NYREXVJ_GENERATED_DIR}/manual_en.cpp
    ${NYREXVJ_GENERATED_DIR}/manual_de.cpp
    src/core/manual.cpp
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
    src/render/isf.cpp
    src/render/clips_extra.cpp
    src/core/clip_pack.cpp
    src/core/show_package.cpp
    src/core/text_font.cpp
    src/core/text_layout.cpp
    src/core/generator_files.cpp
    src/core/text_doc.cpp
    src/core/particles.cpp
    src/core/gpu_particles.cpp
    src/core/gpu_diag.cpp
    src/core/shader_editor.cpp
    src/core/routing.cpp
    src/core/effect_json.cpp
    src/core/effect_chain.cpp
    src/core/show_workflow.cpp
    src/core/resoport.cpp
    src/core/pack_workflow.cpp
    src/scene3d/builtin_docs.cpp
    src/scene3d/doc_build.cpp
    src/scene3d/glb_writer.cpp
    src/scene3d/gltf.cpp
    src/scene3d/gltf_compression.cpp
    src/scene3d/image_decode.cpp
    src/scene3d/primitives.cpp
    src/scene3d/procedural.cpp
    src/scene3d/scene_doc.cpp
    src/scene3d/scene_model.cpp
    src/scene3d/scene_pack.cpp
    src/core/composition.cpp
    src/core/modulation.cpp
    src/core/output_map.cpp
    src/core/spatial_map.cpp
    src/core/spatial_mesh.cpp
    src/core/spatial_calibration.cpp
    src/core/color_model.cpp
    src/core/optical_flow.cpp
    src/core/person_mask.cpp
    src/core/flow_synth.cpp
    src/core/color_lut.cpp
    src/core/venue_setup.cpp
    src/core/mask_images.cpp
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
    src/core/show_timeline.cpp
    src/core/beat_clock.cpp
    src/core/failover.cpp
    src/core/run_conditions.cpp
    src/core/netsync.cpp src/control/netsync_key.cpp src/control/netsync_show.cpp src/control/netsync_transport.cpp
    src/core/ndi_runtime.cpp
    src/core/stream_url.cpp
    src/core/stream_clock.cpp
    src/core/stoppable_thread.cpp
    src/core/ndi_receiver.cpp
    src/core/ndi_sender.cpp
    src/core/stage.cpp
    src/core/stage_json.cpp
    src/core/edit_history.cpp
    src/core/frame_pacer.cpp
    src/core/frame_lock.cpp
    src/core/hardware_sync.cpp
    src/core/render_policy.cpp
    src/core/frame_stats.cpp
    src/core/growth_check.cpp
    src/core/soak_plan.cpp
    src/core/record_plan.cpp
    src/media/ffmpeg_dyn.cpp
    src/media/ffmpeg_setup.cpp
    src/media/frame_convert.cpp
    src/media/hap_convert.cpp
    src/media/convert_cache.cpp
    src/media/convert_manager.cpp
    src/media/video_redirect.cpp
    src/media/live_convert.cpp
    src/media/camera_capture.cpp
    src/media/stream_capture.cpp
    src/media/hap.cpp
    src/media/media_image.cpp
    src/media/playback_driver.cpp
    src/media/frame_luma.cpp
    src/media/flow_worker.cpp
    src/media/person_worker.cpp
    src/media/video_decoder.cpp
    src/media/video_recorder.cpp
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
    src/core/workspace.cpp
    src/core/shortcuts.cpp
    src/core/contrast.cpp
    src/core/sha256.cpp
    src/core/library_meta.cpp
    src/core/launch_quantizer.cpp
    src/core/archive.cpp
    src/control/control_system.cpp
    src/control/djlink_input.cpp
    src/control/sync_system.cpp
    src/control/timeline_system.cpp
    src/control/control_targets.cpp
    src/control/midi_map.cpp
    src/control/midi_feedback.cpp
    src/control/osc.cpp
    src/control/osc_out.cpp
    src/control/stage_out.cpp
    src/control/api_hub.cpp
    src/control/api_page.cpp
    src/control/api_page_lang.cpp
    src/control/api_router.cpp
    src/control/api_security.cpp
    src/control/api_server.cpp
    src/control/http_client.cpp
    src/core/http_message.cpp
    src/update/update_logic.cpp
    src/update/updater.cpp
)

if(WIN32)
    list(APPEND NYREXVJ_CORE_SOURCES src/platform/platform_win.cpp src/audio/audio_input_win.cpp src/control/midi_input_win.cpp src/control/midi_output_win.cpp src/control/udp_socket_win.cpp src/control/tcp_socket_win.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND NYREXVJ_CORE_SOURCES src/platform/platform_other.cpp src/platform/platform_linux_update.cpp src/audio/audio_input_linux.cpp src/control/midi_input_linux.cpp src/control/midi_output_other.cpp src/control/udp_socket_posix.cpp src/control/tcp_socket_posix.cpp)
else()
    list(APPEND NYREXVJ_CORE_SOURCES src/platform/platform_other.cpp src/audio/audio_input_other.cpp src/control/midi_input_other.cpp src/control/midi_output_other.cpp src/control/udp_socket_posix.cpp src/control/tcp_socket_posix.cpp)
endif()

add_library(nvj_core STATIC ${NYREXVJ_CORE_SOURCES})
if(NOT WIN32)
    target_link_libraries(nvj_core PUBLIC ${CMAKE_DL_LIBS})
endif()
if(NYREXVJ_MESHOPT)
    target_compile_definitions(nvj_core PUBLIC NYREXVJ_MESHOPT=1)
    target_link_libraries(nvj_core PUBLIC meshopt_decoder)
else()
    target_compile_definitions(nvj_core PUBLIC NYREXVJ_MESHOPT=0)
endif()
if(NYREXVJ_DRACO)
    target_compile_definitions(nvj_core PUBLIC NYREXVJ_DRACO=1)
    target_include_directories(nvj_core SYSTEM PUBLIC ${draco_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
    target_link_libraries(nvj_core PUBLIC draco_static)
else()
    target_compile_definitions(nvj_core PUBLIC NYREXVJ_DRACO=0)
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
target_include_directories(nvj_core SYSTEM PRIVATE third_party/stb ${imgui_SOURCE_DIR})
target_compile_definitions(nvj_core PUBLIC
    NYREXVJ_VERSION="${NYREXVJ_VERSION_STRING}"
    NYREXVJ_BUILD_NUMBER=${NYREXVJ_BUILD_NUMBER}
)

if(WIN32)
    target_link_libraries(nvj_core PUBLIC winhttp shell32 ole32 ws2_32 winmm psapi bcrypt advapi32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(nvj_core PUBLIC Threads::Threads)
endif()

set(NYREXVJ_APP_SOURCES
    src/main.cpp
    src/app/app.cpp
    src/app/api_system.cpp
    src/app/netsync_system.cpp src/app/netsync_selftest.cpp src/app/failover_system.cpp src/app/failover_selftest.cpp
    src/app/api_selftest.cpp
    src/app/fonts.cpp
    src/app/bench.cpp
    src/app/soak.cpp
    src/app/bench_convert.cpp
    src/app/output_selftest.cpp
    src/app/color_selftest.cpp
    src/app/flow_selftest.cpp
    src/app/person_selftest.cpp
    src/app/record_selftest.cpp
    src/app/shader_selftest.cpp
    src/app/generator_selftest.cpp
    src/app/gpu_particle_selftest.cpp
    src/app/gpu_diag_selftest.cpp
    src/app/routing_selftest.cpp
    src/app/shader_editor_selftest.cpp
    src/app/scene_selftest.cpp
    src/app/theme.cpp
    src/app/shortcut_keys.cpp
    src/app/ui_interface.cpp
    src/app/ui_browser.cpp
    src/app/ui_chrome.cpp
    src/app/ui_deck.cpp
    src/app/ui_control.cpp
    src/app/ui_creator.cpp
    src/app/ui_nodes.cpp
    src/app/ui_settings.cpp
    src/app/ui_diagnostics.cpp
    src/app/ui_record.cpp
    src/app/ui_video.cpp
    src/app/ui_color.cpp
    src/app/ui_flow.cpp
    src/app/ui_person.cpp
    src/app/ui_convert.cpp
    src/app/ui_profile.cpp
    src/app/ui_panels.cpp
    src/app/ui_live.cpp
    src/app/ui_plugins.cpp
    src/app/ui_welcome.cpp
    src/app/ui_recovery.cpp
    src/app/ui_show.cpp
    src/app/ui_resoport.cpp
    src/app/ui_packs.cpp
    src/app/ui_generators.cpp
    src/app/ui_shader_editor.cpp
    src/app/ui_routing.cpp
    src/app/ui_failover.cpp
    src/app/shader_editor_uitest.cpp
    src/app/interface_uitest.cpp
    src/app/pack_cli.cpp
    src/render/engine.cpp
    src/render/routing_engine.cpp
    src/render/gpu_profiler.cpp
    src/render/gl_util.cpp
    src/render/generators.cpp
    src/render/gpu_particles.cpp
    src/render/shader_passes.cpp
    src/render/output_window.cpp
    src/render/color_gpu.cpp
    src/render/color_stage.cpp
    src/render/flow_interp.cpp
    src/render/flow_session.cpp
    src/render/person_session.cpp
    src/render/texture_history.cpp
    src/render/hardware_sync_probe.cpp
    src/render/output_manager.cpp
    src/render/ndi_output.cpp
    src/render/recorder.cpp
    src/render/dmx_strip.cpp
    src/app/ui_outputs.cpp
    src/app/ui_spatial.cpp
    src/app/ui_stage.cpp
    src/app/ui_sync.cpp

    src/app/ui_timeline.cpp
    src/app/ui_manual.cpp
    src/render/scene_player.cpp
    src/render/media_player.cpp
    src/render/video_gpu.cpp
    src/render/shader_playground.cpp
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
    target_compile_definitions(nvj_scene3d_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_scene3d_tests COMMAND nvj_scene3d_tests)
    add_executable(nvj_creator_tests tests/test_creator.cpp)
    target_link_libraries(nvj_creator_tests PRIVATE nvj_core)
    target_include_directories(nvj_creator_tests PRIVATE tests)
    target_compile_definitions(nvj_creator_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_creator_tests COMMAND nvj_creator_tests)
    add_executable(nvj_i18n_tests tests/test_i18n.cpp)
    target_link_libraries(nvj_i18n_tests PRIVATE nvj_core)
    target_include_directories(nvj_i18n_tests PRIVATE tests)
    add_test(NAME nvj_i18n_tests COMMAND nvj_i18n_tests)
    set(NYREXVJ_UNREAL_CORE_DIR ${CMAKE_SOURCE_DIR}/integrations/unreal/NyrexVJ/Source/NyrexVJ/Private)
    add_library(nvj_unreal_core STATIC ${NYREXVJ_UNREAL_CORE_DIR}/Core/NyrexOsc.cpp ${NYREXVJ_UNREAL_CORE_DIR}/Core/NyrexState.cpp ${NYREXVJ_UNREAL_CORE_DIR}/Core/NyrexLink.cpp)
    set_target_properties(nvj_unreal_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_include_directories(nvj_unreal_core PUBLIC ${NYREXVJ_UNREAL_CORE_DIR})
    add_executable(nvj_unreal_core_tests tests/test_unreal_core.cpp)
    target_link_libraries(nvj_unreal_core_tests PRIVATE nvj_core nvj_unreal_core)
    target_include_directories(nvj_unreal_core_tests PRIVATE tests)
    add_test(NAME nvj_unreal_core_tests COMMAND nvj_unreal_core_tests)
    add_executable(nvj_unreal_link_tests tests/test_unreal_link.cpp)
    target_link_libraries(nvj_unreal_link_tests PRIVATE nvj_core nvj_unreal_core)
    target_include_directories(nvj_unreal_link_tests PRIVATE tests)
    add_test(NAME nvj_unreal_link_tests COMMAND nvj_unreal_link_tests)
    add_executable(nvj_media_tests tests/test_media.cpp)
    target_link_libraries(nvj_media_tests PRIVATE nvj_core)
    target_include_directories(nvj_media_tests PRIVATE tests)
    target_compile_definitions(nvj_media_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_media_tests COMMAND nvj_media_tests)
    add_executable(nvj_convert_tests tests/test_convert.cpp)
    target_link_libraries(nvj_convert_tests PRIVATE nvj_core)
    target_include_directories(nvj_convert_tests PRIVATE tests)
    target_compile_definitions(nvj_convert_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_convert_tests COMMAND nvj_convert_tests)
    add_executable(nvj_output_tests tests/test_outputs.cpp)
    target_link_libraries(nvj_output_tests PRIVATE nvj_core)
    target_include_directories(nvj_output_tests PRIVATE tests)
    add_test(NAME nvj_output_tests COMMAND nvj_output_tests)
    add_executable(nvj_beat_clock_tests tests/test_beat_clock.cpp)
    target_link_libraries(nvj_beat_clock_tests PRIVATE nvj_core)
    target_include_directories(nvj_beat_clock_tests PRIVATE tests)
    add_test(NAME nvj_beat_clock_tests COMMAND nvj_beat_clock_tests)
    add_executable(nvj_netsync_tests tests/test_netsync.cpp)
    target_link_libraries(nvj_netsync_tests PRIVATE nvj_core)
    target_include_directories(nvj_netsync_tests PRIVATE tests)
    add_test(NAME nvj_netsync_tests COMMAND nvj_netsync_tests)
    add_executable(nvj_release_tests tests/test_release.cpp)
    target_link_libraries(nvj_release_tests PRIVATE nvj_core)
    target_include_directories(nvj_release_tests PRIVATE tests)
    add_test(NAME nvj_release_tests COMMAND nvj_release_tests)
    add_executable(nvj_compat_tests tests/test_compat.cpp)
    target_link_libraries(nvj_compat_tests PRIVATE nvj_core)
    target_include_directories(nvj_compat_tests PRIVATE tests)
    target_compile_definitions(nvj_compat_tests PRIVATE NYREXVJ_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
    add_test(NAME nvj_compat_tests COMMAND nvj_compat_tests)
    add_executable(nvj_interface_tests tests/test_interface.cpp)
    target_link_libraries(nvj_interface_tests PRIVATE nvj_core)
    target_include_directories(nvj_interface_tests PRIVATE tests)
    add_test(NAME nvj_interface_tests COMMAND nvj_interface_tests)
    add_executable(nvj_theme_tests tests/test_theme.cpp src/app/theme.cpp)
    target_link_libraries(nvj_theme_tests PRIVATE nvj_core imgui)
    target_include_directories(nvj_theme_tests PRIVATE tests)
    add_test(NAME nvj_theme_tests COMMAND nvj_theme_tests)
    add_executable(nvj_failover_tests tests/test_failover.cpp)
    target_link_libraries(nvj_failover_tests PRIVATE nvj_core)
    target_include_directories(nvj_failover_tests PRIVATE tests)
    add_test(NAME nvj_failover_tests COMMAND nvj_failover_tests)
    add_executable(nvj_netsync_show_tests tests/test_netsync_show.cpp)
    target_link_libraries(nvj_netsync_show_tests PRIVATE nvj_core)
    target_include_directories(nvj_netsync_show_tests PRIVATE tests)
    add_test(NAME nvj_netsync_show_tests COMMAND nvj_netsync_show_tests)
    add_executable(nvj_netsync_transport_tests tests/test_netsync_transport.cpp)
    target_link_libraries(nvj_netsync_transport_tests PRIVATE nvj_core)
    target_include_directories(nvj_netsync_transport_tests PRIVATE tests)
    add_test(NAME nvj_netsync_transport_tests COMMAND nvj_netsync_transport_tests)
    add_executable(nvj_timecode_tests tests/test_timecode.cpp)
    target_link_libraries(nvj_timecode_tests PRIVATE nvj_core)
    target_include_directories(nvj_timecode_tests PRIVATE tests)
    add_test(NAME nvj_timecode_tests COMMAND nvj_timecode_tests)
    add_executable(nvj_frame_lock_tests tests/test_frame_lock.cpp)
    target_link_libraries(nvj_frame_lock_tests PRIVATE nvj_core)
    target_include_directories(nvj_frame_lock_tests PRIVATE tests)
    add_test(NAME nvj_frame_lock_tests COMMAND nvj_frame_lock_tests)
    add_executable(nvj_hardware_sync_tests tests/test_hardware_sync.cpp)
    target_link_libraries(nvj_hardware_sync_tests PRIVATE nvj_core)
    target_include_directories(nvj_hardware_sync_tests PRIVATE tests)
    add_test(NAME nvj_hardware_sync_tests COMMAND nvj_hardware_sync_tests)
    add_executable(nvj_soak_tests tests/test_soak.cpp)
    target_link_libraries(nvj_soak_tests PRIVATE nvj_core)
    target_include_directories(nvj_soak_tests PRIVATE tests)
    add_test(NAME nvj_soak_tests COMMAND nvj_soak_tests)
    add_executable(nvj_record_tests tests/test_record.cpp)
    target_link_libraries(nvj_record_tests PRIVATE nvj_core)
    target_include_directories(nvj_record_tests PRIVATE tests)
    if(NYREXVJ_HAVE_FFMPEG)
        target_compile_definitions(nvj_record_tests PRIVATE NYREXVJ_HAVE_FFMPEG=1)
        target_include_directories(nvj_record_tests SYSTEM PRIVATE "${NYREXVJ_FFMPEG_INCLUDE}")
    endif()
    add_test(NAME nvj_record_tests COMMAND nvj_record_tests)
    add_executable(nvj_api_tests tests/test_api.cpp)
    target_link_libraries(nvj_api_tests PRIVATE nvj_core)
    target_include_directories(nvj_api_tests PRIVATE tests)
    add_test(NAME nvj_api_tests COMMAND nvj_api_tests)
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
    add_executable(nvj_timeline_tests tests/test_timeline.cpp)
    target_link_libraries(nvj_timeline_tests PRIVATE nvj_core)
    target_include_directories(nvj_timeline_tests PRIVATE tests)
    add_test(NAME nvj_timeline_tests COMMAND nvj_timeline_tests)
    add_executable(nvj_manual_tests tests/test_manual.cpp)
    target_link_libraries(nvj_manual_tests PRIVATE nvj_core)
    target_include_directories(nvj_manual_tests PRIVATE tests)
    target_compile_definitions(nvj_manual_tests PRIVATE "NVJ_MANUAL_FIXTURES=\"${CMAKE_SOURCE_DIR}/tests/data\"")
    add_test(NAME nvj_manual_tests COMMAND nvj_manual_tests)
    add_executable(nvj_venue_tests tests/test_venues.cpp)
    target_link_libraries(nvj_venue_tests PRIVATE nvj_core)
    target_include_directories(nvj_venue_tests PRIVATE tests)
    add_test(NAME nvj_venue_tests COMMAND nvj_venue_tests)
    add_executable(nvj_spatial_tests tests/test_spatial.cpp)
    target_link_libraries(nvj_spatial_tests PRIVATE nvj_core)
    target_include_directories(nvj_spatial_tests PRIVATE tests)
    add_test(NAME nvj_spatial_tests COMMAND nvj_spatial_tests)

    add_executable(nvj_color_tests tests/test_color.cpp)
    target_link_libraries(nvj_color_tests PRIVATE nvj_core)
    target_include_directories(nvj_color_tests PRIVATE tests)
    add_test(NAME nvj_color_tests COMMAND nvj_color_tests)
    add_executable(nvj_person_tests tests/test_person.cpp)
    target_link_libraries(nvj_person_tests PRIVATE nvj_core)
    target_include_directories(nvj_person_tests PRIVATE tests)
    add_test(NAME nvj_person_tests COMMAND nvj_person_tests)
    add_executable(nvj_flow_tests tests/test_flow.cpp)
    target_link_libraries(nvj_flow_tests PRIVATE nvj_core)
    target_include_directories(nvj_flow_tests PRIVATE tests)
    add_test(NAME nvj_flow_tests COMMAND nvj_flow_tests)
    add_executable(nvj_show_tests tests/test_show_package.cpp)
    target_link_libraries(nvj_show_tests PRIVATE nvj_core)
    target_include_directories(nvj_show_tests PRIVATE tests)
    add_test(NAME nvj_show_tests COMMAND nvj_show_tests)
    add_executable(nvj_show_workflow_tests tests/test_show_workflow.cpp)
    target_link_libraries(nvj_show_workflow_tests PRIVATE nvj_core)
    target_include_directories(nvj_show_workflow_tests PRIVATE tests)
    add_test(NAME nvj_show_workflow_tests COMMAND nvj_show_workflow_tests)

    add_executable(nvj_resoport_tests tests/test_resoport.cpp)
    target_link_libraries(nvj_resoport_tests PRIVATE nvj_core)
    target_include_directories(nvj_resoport_tests PRIVATE tests)
    add_test(NAME nvj_resoport_tests COMMAND nvj_resoport_tests)

    add_executable(nvj_pack_tests tests/test_pack.cpp)
    target_link_libraries(nvj_pack_tests PRIVATE nvj_core)
    target_include_directories(nvj_pack_tests PRIVATE tests)
    add_test(NAME nvj_pack_tests COMMAND nvj_pack_tests)
    add_executable(nvj_creative_tests tests/test_creative.cpp)
    target_link_libraries(nvj_creative_tests PRIVATE nvj_core)
    target_include_directories(nvj_creative_tests PRIVATE tests)
    target_compile_definitions(nvj_creative_tests PRIVATE NVJ_TEST_FONT_DIR="${imgui_SOURCE_DIR}/misc/fonts")
    add_test(NAME nvj_creative_tests COMMAND nvj_creative_tests)
    add_executable(nvj_gpuparticles_tests tests/test_gpu_particles.cpp)
    target_link_libraries(nvj_gpuparticles_tests PRIVATE nvj_core)
    target_include_directories(nvj_gpuparticles_tests PRIVATE tests)
    add_test(NAME nvj_gpuparticles_tests COMMAND nvj_gpuparticles_tests)
    add_executable(nvj_gpudiag_tests tests/test_gpu_diag.cpp)
    target_link_libraries(nvj_gpudiag_tests PRIVATE nvj_core)
    target_include_directories(nvj_gpudiag_tests PRIVATE tests)
    add_test(NAME nvj_gpudiag_tests COMMAND nvj_gpudiag_tests)
    add_executable(nvj_routing_tests tests/test_routing.cpp)
    target_link_libraries(nvj_routing_tests PRIVATE nvj_core)
    target_include_directories(nvj_routing_tests PRIVATE tests)
    add_test(NAME nvj_routing_tests COMMAND nvj_routing_tests)
    add_executable(nvj_shaderedit_tests tests/test_shader_editor.cpp)
    target_link_libraries(nvj_shaderedit_tests PRIVATE nvj_core)
    target_include_directories(nvj_shaderedit_tests PRIVATE tests)
    add_test(NAME nvj_shaderedit_tests COMMAND nvj_shaderedit_tests)
    add_executable(nvj_workflow_tests tests/test_workflow.cpp)
    target_link_libraries(nvj_workflow_tests PRIVATE nvj_core)
    target_include_directories(nvj_workflow_tests PRIVATE tests)
    add_test(NAME nvj_workflow_tests COMMAND nvj_workflow_tests)
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
    add_library(fake_ndi_v3 SHARED tests/fake_ndi.cpp)
    set_target_properties(fake_ndi_v3 PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "")
    target_compile_definitions(fake_ndi_v3 PRIVATE FAKE_NDI_V3_ONLY)
    add_executable(nvj_ndi_v3_tests tests/test_ndi_v3.cpp)
    target_link_libraries(nvj_ndi_v3_tests PRIVATE nvj_core fake_ndi_v3)
    target_include_directories(nvj_ndi_v3_tests PRIVATE tests)
    target_compile_definitions(nvj_ndi_v3_tests PRIVATE NYREXVJ_FAKE_NDI_V3_PATH="$<TARGET_FILE:fake_ndi_v3>")
    add_test(NAME nvj_ndi_v3_tests COMMAND nvj_ndi_v3_tests)
    add_executable(nvj_stream_tests tests/test_stream.cpp)
    target_link_libraries(nvj_stream_tests PRIVATE nvj_core)
    target_include_directories(nvj_stream_tests PRIVATE tests)
    add_test(NAME nvj_stream_tests COMMAND nvj_stream_tests)
    add_executable(nvj_picture_convert_tests tests/test_picture_convert.cpp)
    target_link_libraries(nvj_picture_convert_tests PRIVATE nvj_core)
    target_include_directories(nvj_picture_convert_tests PRIVATE tests)
    add_test(NAME nvj_picture_convert_tests COMMAND nvj_picture_convert_tests)
    if(NYREXVJ_HAVE_FFMPEG)
        target_compile_definitions(nvj_picture_convert_tests PRIVATE NYREXVJ_HAVE_FFMPEG=1)
        target_include_directories(nvj_picture_convert_tests SYSTEM PRIVATE "${NYREXVJ_FFMPEG_INCLUDE}")
    endif()
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
    add_executable(nvj_isf_tests tests/test_isf.cpp)
    target_link_libraries(nvj_isf_tests PRIVATE nvj_core)
    target_include_directories(nvj_isf_tests PRIVATE tests)
    target_compile_definitions(nvj_isf_tests PRIVATE "NVJ_ISF_EXAMPLES=\"${CMAKE_SOURCE_DIR}/docs/shaders\"")
    add_test(NAME nvj_isf_tests COMMAND nvj_isf_tests)
    function(nyrexvj_plugin_module target source output_name output_dir)
        add_library(${target} MODULE ${source})
        if(ARGN)
            target_include_directories(${target} PRIVATE ${ARGN})
        else()
            target_include_directories(${target} PRIVATE plugins/include)
        endif()
        set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME ${output_name} C_STANDARD 99 C_VISIBILITY_PRESET hidden LIBRARY_OUTPUT_DIRECTORY "$<1:${output_dir}>" RUNTIME_OUTPUT_DIRECTORY "$<1:${output_dir}>")
        if(NOT WIN32)
            target_link_libraries(${target} PRIVATE m)
        endif()
    endfunction()
    nyrexvj_plugin_module(nyrexvj_example_plugin plugins/example/example_plugin.c nyrexvj_example ${CMAKE_BINARY_DIR}/plugins/nyrexvj-example)
    add_custom_command(TARGET nyrexvj_example_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/plugins/example/plugin.json $<TARGET_FILE_DIR:nyrexvj_example_plugin>/plugin.json)
    set(NYREXVJ_BROKEN_PLUGINS slow:1 hang:2 fail:3 crash:4 range:5 createfail:6 entryfail:7 noentry:8 duplicate:9 mismatch:10 loadslow:11 overflow:12 imagechanged:13 slowframe:14 toomany:15 idmismatch:16 nomodulation:17 crashcreate:18 crashload:19 logspam:20 tableoverflow:21 tablesize:22 sourceoverflow:23 busyframe:24 errortext:25 entrytext:26 fine:99)
    nyrexvj_plugin_module(nvj_probe_plugin tests/plugins/probe_plugin.c probe ${CMAKE_BINARY_DIR}/test_plugins)
    nyrexvj_plugin_module(nvj_fuzz_plugin tests/plugins/fuzz_plugin.c fuzz ${CMAKE_BINARY_DIR}/test_plugins)
    nyrexvj_plugin_module(nyrexvj_legacy_plugin tests/plugins/legacy_plugin.c nyrexvj_legacy ${CMAKE_BINARY_DIR}/plugins/nyrexvj-legacy ${CMAKE_SOURCE_DIR}/tests/plugins/abi/v1_0)
    add_custom_command(TARGET nyrexvj_legacy_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/tests/plugins/legacy_plugin.json $<TARGET_FILE_DIR:nyrexvj_legacy_plugin>/plugin.json)
    add_executable(nvj_plugin_tests tests/test_plugins.cpp tests/plugins/abi_layout_v1_0.c tests/plugins/abi_layout_current.c)
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
    target_compile_definitions(nvj_plugin_tests PRIVATE "NVJ_PLUGIN_PROBE=\"$<TARGET_FILE:nvj_probe_plugin>\"" "NVJ_PLUGIN_FUZZ=\"$<TARGET_FILE:nvj_fuzz_plugin>\"" "NVJ_PLUGIN_LEGACY=\"$<TARGET_FILE:nyrexvj_legacy_plugin>\"")
    add_dependencies(nvj_plugin_tests nvj_probe_plugin nvj_fuzz_plugin nyrexvj_legacy_plugin)
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
