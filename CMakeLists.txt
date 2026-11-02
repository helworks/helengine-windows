cmake_minimum_required(VERSION 3.26)

project(helengine_windows LANGUAGES CXX RC)

set(HELENGINE_CORE_CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/generated/helengine.core" CACHE PATH "Path to the generated helengine.core C++ output folder.")
set(HELENGINE_CODE_ROOT "" CACHE PATH "Path to the staged generated code-module root folder.")
set(HELENGINE_WINDOWS_RENDER_BACKEND "DirectX11" CACHE STRING "Windows rendering backend to compile.")
set(HELENGINE_WINDOWS_NATIVE_PROFILE "Debug" CACHE STRING "Native Windows player profile used to select profile-specific dependencies.")
set(HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ "" CACHE STRING "Optional fixed physics update frequency for this Windows player build.")
set(HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS "" CACHE STRING "Optional BEPU velocity-iteration count for this Windows player build.")
set(HELENGINE_WINDOWS_PHYSICS_SUBSTEPS "" CACHE STRING "Optional BEPU substep count for this Windows player build.")
option(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE "Build the Windows host with generated helengine.core sources." ON)
option(HELENGINE_ENABLE_VOXEL_GI "Compile the optional portable voxel transport and Windows toon consumer." OFF)
set(HELENGINE_VOXEL_GI_ROOT "" CACHE PATH "Explicit source path to the optional helengine.gi.native module.")
option(HELENGINE_ENABLE_LIGHTMAP "Compile the optional saved lightmap consumer and snapshot exporter." OFF)
set(HELENGINE_LIGHTMAP_ROOT "" CACHE PATH "Explicit portable lightmap tooling source module.")
set_property(CACHE HELENGINE_WINDOWS_RENDER_BACKEND PROPERTY STRINGS DirectX11 Vulkan)
set_property(CACHE HELENGINE_WINDOWS_NATIVE_PROFILE PROPERTY STRINGS Debug Release Profiler)

if(NOT HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Debug" AND NOT HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Release" AND NOT HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Profiler")
    message(FATAL_ERROR "HELENGINE_WINDOWS_NATIVE_PROFILE must be Debug, Release, or Profiler.")
endif()

if(NOT "${HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ}" STREQUAL "" OR NOT "${HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS}" STREQUAL "" OR NOT "${HELENGINE_WINDOWS_PHYSICS_SUBSTEPS}" STREQUAL "")
    if("${HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ}" STREQUAL "" OR "${HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS}" STREQUAL "" OR "${HELENGINE_WINDOWS_PHYSICS_SUBSTEPS}" STREQUAL "")
        message(FATAL_ERROR "Windows physics overrides require fixed-step hertz, velocity iterations, and substeps together.")
    endif()
    if(NOT "${HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ}" MATCHES "^[1-9][0-9]*$" OR NOT "${HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS}" MATCHES "^[1-9][0-9]*$" OR NOT "${HELENGINE_WINDOWS_PHYSICS_SUBSTEPS}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Windows physics overrides must be positive integer values.")
    endif()
endif()

if(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
    set(HELENGINE_CORE_TRANSLATION_UNIT "")

    if("${HELENGINE_CORE_CPP_ROOT}" STREQUAL "")
        message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT must point at the generated helengine.core C++ output folder.")
    endif()

    if(NOT EXISTS "${HELENGINE_CORE_CPP_ROOT}/helcpp_config.hpp")
        message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT does not contain helcpp_config.hpp. Regenerate helengine.core C++ output first.")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/generated_windows_handoff.cmake")
        include("${HELENGINE_CORE_CPP_ROOT}/generated_windows_handoff.cmake")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_core_unity.cpp")
        set(HELENGINE_CORE_TRANSLATION_UNIT "${HELENGINE_CORE_CPP_ROOT}/helengine_core_unity.cpp")
    elseif(EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_core_amalgamated.cpp")
        set(HELENGINE_CORE_TRANSLATION_UNIT "${HELENGINE_CORE_CPP_ROOT}/helengine_core_amalgamated.cpp")
    elseif(DEFINED CPP_GENERATED_UNITY_SOURCE AND EXISTS "${CPP_GENERATED_UNITY_SOURCE}")
        set(HELENGINE_CORE_TRANSLATION_UNIT "${CPP_GENERATED_UNITY_SOURCE}")
    else()
        message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT does not contain a supported generated unity or amalgamated translation unit. Regenerate helengine.core C++ output first.")
    endif()
endif()

if(NOT HELENGINE_WINDOWS_RENDER_BACKEND STREQUAL "DirectX11")
    message(FATAL_ERROR "Only the DirectX11 bootstrap scaffold exists today. Add the Vulkan path before selecting that backend.")
endif()

set(HELENGINE_WINDOWS_SOURCES
    src/helengine_windows.manifest
    src/main.cpp
    src/platform/windows/runtime/runtime_memory_diagnostics_provider.cpp
    src/platform/windows/runtime/runtime_memory_snapshot.cpp
    src/platform/windows/runtime/runtime_player_profile.cpp
    src/platform/windows/runtime/runtime_player_profile_configuration_error.cpp
    src/platform/windows/runtime/runtime_player_profile_loader.cpp
    src/platform/windows/runtime/runtime_render_diagnostics.cpp
    src/platform/windows/runtime/windows_tracy_profiler.cpp
    src/platform/windows/runtime/windows_cpu_profile_sink.cpp
    src/platform/windows/win32/bmp_image_writer.cpp
    src/platform/windows/win32/win32_activity_tracker.cpp
    src/platform/windows/win32/win32_application.cpp
    src/platform/windows/win32/win32_click_through_controller.cpp
    src/platform/windows/win32/win32_command_line_options.cpp
    src/platform/windows/win32/win32_dpi_awareness_names.cpp
    src/platform/windows/win32/win32_dpi_awareness_settings.cpp
    src/platform/windows/win32/win32_dpi_window_sizing.cpp
    src/platform/windows/win32/win32_exit_request.cpp
    src/platform/windows/win32/win32_idle_frame_pacer.cpp
    src/platform/windows/win32/win32_idle_throttle_settings.cpp
    src/platform/windows/win32/win32_overlay_hit_test_controller.cpp
    src/platform/windows/win32/win32_additional_window_settings.cpp
    src/platform/windows/win32/win32_secondary_window.cpp
    src/platform/windows/win32/win32_desktop_host.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/win32/win32_window_mode_names.cpp
    src/platform/windows/win32/win32_window_mode_settings.cpp
    src/platform/windows/win32/win32_window_style.cpp
    src/platform/windows/directx11/directx11_back_buffer_capture.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_output_attachments.cpp
    src/platform/windows/directx11/directx11_texture_preview.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp
    src/platform/windows/directx11/directx11_preview_processor.cpp
    src/platform/windows/directx11/directx11_hit_test_sampler.cpp
    src/platform/windows/directx11/directx11_host_fingerprint.cpp
    src/platform/windows/directx11/directx11_hresult_formatter.cpp
    src/platform/windows/directx11/directx11_presenter.cpp
)

if(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
    list(APPEND HELENGINE_WINDOWS_SOURCES
        src/platform/windows/win32/win32_audio_backend.cpp
        src/platform/windows/win32/win32_input_bridge.cpp
        src/platform/windows/win32/win32_render_bridge.cpp
        src/platform/windows/win32/win32_camera_toon_pass.cpp
        src/platform/windows/win32/win32_scene_ssgi.cpp
    )

    list(APPEND HELENGINE_WINDOWS_SOURCES "${HELENGINE_CORE_TRANSLATION_UNIT}")

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/runtime/runtime_startup_manifest.cpp")
        list(APPEND HELENGINE_WINDOWS_SOURCES "${HELENGINE_CORE_CPP_ROOT}/runtime/runtime_startup_manifest.cpp")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/runtime/runtime_code_module_manifest.cpp")
        list(APPEND HELENGINE_WINDOWS_SOURCES "${HELENGINE_CORE_CPP_ROOT}/runtime/runtime_code_module_manifest.cpp")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/runtime/runtime_scene_catalog_manifest.cpp")
        list(APPEND HELENGINE_WINDOWS_SOURCES "${HELENGINE_CORE_CPP_ROOT}/runtime/runtime_scene_catalog_manifest.cpp")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/runtime/player_settings.cpp")
        list(APPEND HELENGINE_WINDOWS_SOURCES "${HELENGINE_CORE_CPP_ROOT}/runtime/player_settings.cpp")
    endif()
endif()

add_executable(helengine_windows WIN32 ${HELENGINE_WINDOWS_SOURCES})

if(HELENGINE_ENABLE_VOXEL_GI)
    if(NOT HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
        message(FATAL_ERROR "Voxel GI requires generated scene types.")
    endif()
    if(NOT EXISTS "${HELENGINE_VOXEL_GI_ROOT}/CMakeLists.txt" OR NOT EXISTS "${HELENGINE_VOXEL_GI_ROOT}/include/helgi/VoxelGi.h")
        message(FATAL_ERROR "HELENGINE_VOXEL_GI_ROOT must point at the optional portable GI source module.")
    endif()
    set(HELENGINE_GI_TESTS OFF CACHE BOOL "Standalone transport tests are built separately from the player." FORCE)
    add_subdirectory("${HELENGINE_VOXEL_GI_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/voxel-gi")
    target_sources(helengine_windows PRIVATE src/platform/windows/win32/win32_scene_gi.cpp src/platform/windows/win32/win32_scene_gi_spheres.cpp src/platform/windows/win32/win32_scene_gi_usage.cpp)
    target_compile_definitions(helengine_windows PRIVATE HELENGINE_VOXEL_GI)
    target_link_libraries(helengine_windows PRIVATE helengine_gi)
    add_executable(helengine_windows_scene_gi_tests EXCLUDE_FROM_ALL tests/scene_gi_shader_tests.cpp)
    target_include_directories(helengine_windows_scene_gi_tests PRIVATE src)
    target_compile_features(helengine_windows_scene_gi_tests PRIVATE cxx_std_20)
    target_compile_definitions(helengine_windows_scene_gi_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(helengine_windows_scene_gi_tests PRIVATE helengine_gi d3d11 d3dcompiler)
endif()

if(HELENGINE_ENABLE_LIGHTMAP)
    if(NOT HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
        message(FATAL_ERROR "Lightmap scene snapshots require generated scene types.")
    endif()
    if(NOT EXISTS "${HELENGINE_LIGHTMAP_ROOT}/include/hellm/Lightmap.h")
        message(FATAL_ERROR "HELENGINE_LIGHTMAP_ROOT must point at the portable lightmap source module.")
    endif()
    set(HELENGINE_LIGHTMAP_TESTS OFF CACHE BOOL "Build portable tooling tests separately." FORCE)
    add_subdirectory("${HELENGINE_LIGHTMAP_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/lightmap")
    target_sources(helengine_windows PRIVATE src/platform/windows/win32/win32_scene_lightmap.cpp)
    target_compile_definitions(helengine_windows PRIVATE HELENGINE_LIGHTMAP)
    target_link_libraries(helengine_windows PRIVATE helengine_lightmap)
endif()

target_compile_features(helengine_windows PRIVATE cxx_std_20)

if(HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Profiler")
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy instrumentation for the native profiler player profile." FORCE)
    # A desktop host must not accumulate an unbounded trace while no collector is connected.
    set(TRACY_ON_DEMAND ON CACHE BOOL "Collect profiler events only while a Tracy client is connected." FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy" "${CMAKE_CURRENT_BINARY_DIR}/third_party/tracy")
    target_compile_definitions(helengine_windows PRIVATE TRACY_ENABLE)
    target_link_libraries(helengine_windows PRIVATE TracyClient)
endif()

target_compile_definitions(helengine_windows PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Debug>:HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS>
)

if(NOT "${HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ}" STREQUAL "")
    target_compile_definitions(helengine_windows PRIVATE
        HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ=${HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ}
        HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS=${HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS}
        HELENGINE_WINDOWS_PHYSICS_SUBSTEPS=${HELENGINE_WINDOWS_PHYSICS_SUBSTEPS}
    )
endif()

if(MSVC)
    target_compile_options(helengine_windows PRIVATE /bigobj)
    # Resolve deployed symbols beside the executable, never in the mutable native build cache.
    target_link_options(helengine_windows PRIVATE "/PDBALTPATH:helengine_windows.pdb")
endif()

target_include_directories(helengine_windows PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
    target_include_directories(helengine_windows PRIVATE ${HELENGINE_CORE_CPP_ROOT})
endif()

target_link_libraries(helengine_windows PRIVATE
    d3d11
    d3dcompiler
    dbghelp
    dcomp
    dxgi
    psapi
    shell32
    user32
    gdi32
    winmm
)

# These behavioral host tests are built explicitly and do not change the default player target.
add_executable(helengine_windows_window_tests EXCLUDE_FROM_ALL
    tests/window_configuration_tests.cpp
    src/platform/windows/win32/win32_additional_window_settings.cpp
    src/platform/windows/win32/win32_command_line_options.cpp
    src/platform/windows/win32/win32_dpi_awareness_names.cpp
    src/platform/windows/win32/win32_window_mode_names.cpp)
target_include_directories(helengine_windows_window_tests PRIVATE src)
target_compile_features(helengine_windows_window_tests PRIVATE cxx_std_20)
target_link_libraries(helengine_windows_window_tests PRIVATE shell32)

# Desktop tests create their own shell substitutes and never mutate Explorer-owned windows.
add_executable(helengine_windows_desktop_tests EXCLUDE_FROM_ALL
    src/helengine_windows.manifest
    tests/desktop_host_tests.cpp
    src/platform/windows/runtime/runtime_render_diagnostics.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/win32/win32_activity_tracker.cpp
    src/platform/windows/win32/win32_dpi_window_sizing.cpp
    src/platform/windows/win32/win32_window_mode_names.cpp
    src/platform/windows/directx11/directx11_host_fingerprint.cpp
    src/platform/windows/win32/win32_desktop_host.cpp
    src/platform/windows/win32/win32_window_style.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_output_attachments.cpp
    src/platform/windows/directx11/directx11_hresult_formatter.cpp)
target_include_directories(helengine_windows_desktop_tests PRIVATE src)
target_compile_features(helengine_windows_desktop_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_desktop_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_desktop_tests PRIVATE user32 gdi32 d3d11 dcomp dxgi)

# Pointer tests default to headless; --native-windows explicitly enables owned overlay windows and graphics.
add_executable(helengine_windows_pointer_endpoint_tests EXCLUDE_FROM_ALL tests/pointer_monitor_endpoint_tests.cpp)
target_include_directories(helengine_windows_pointer_endpoint_tests PRIVATE src)
target_compile_features(helengine_windows_pointer_endpoint_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_pointer_endpoint_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_pointer_endpoint_tests PRIVATE user32)

add_executable(helengine_windows_pointer_tests EXCLUDE_FROM_ALL
    tests/pointer_window_routing_tests.cpp
    src/platform/windows/runtime/runtime_render_diagnostics.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/win32/win32_window_style.cpp
    src/platform/windows/win32/win32_window_mode_names.cpp
    src/platform/windows/win32/win32_activity_tracker.cpp
    src/platform/windows/win32/win32_dpi_window_sizing.cpp
    src/platform/windows/win32/win32_desktop_host.cpp
    src/platform/windows/win32/win32_click_through_controller.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_output_attachments.cpp
    src/platform/windows/directx11/directx11_hresult_formatter.cpp)
target_include_directories(helengine_windows_pointer_tests PRIVATE src)
target_compile_features(helengine_windows_pointer_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_pointer_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_pointer_tests PRIVATE user32 gdi32 d3d11 dcomp dxgi)

# Windowless event-input regression coverage never creates or controls desktop windows.
add_executable(helengine_windows_pointer_message_tests EXCLUDE_FROM_ALL tests/pointer_message_state_tests.cpp)
target_include_directories(helengine_windows_pointer_message_tests PRIVATE src)
target_compile_features(helengine_windows_pointer_message_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_pointer_message_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Synthetic engine attachment coverage runs without swapchains, HWNDs or desktop capture.
add_executable(helengine_windows_output_tests EXCLUDE_FROM_ALL
    tests/output_attachments_tests.cpp
    src/platform/windows/directx11/directx11_output_attachments.cpp
    src/platform/windows/directx11/directx11_hresult_formatter.cpp)
target_include_directories(helengine_windows_output_tests PRIVATE src)
target_compile_features(helengine_windows_output_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_output_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_output_tests PRIVATE d3d11 d3dcompiler dxgi)
# External-surface preview tests use synthetic GPU textures only, never native windows or capture sessions.
add_executable(helengine_windows_preview_tests EXCLUDE_FROM_ALL
    tests/texture_preview_tests.cpp
    src/platform/windows/directx11/directx11_texture_preview.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp
    src/platform/windows/directx11/directx11_preview_processor.cpp
    src/platform/windows/directx11/directx11_hresult_formatter.cpp)
target_include_directories(helengine_windows_preview_tests PRIVATE src)
target_compile_features(helengine_windows_preview_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_preview_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_preview_tests PRIVATE d3d11 d3dcompiler dxgi)

# Camera-local toon tests draw owned geometric fixtures, without HWNDs or desktop capture.
add_executable(helengine_windows_toon_tests EXCLUDE_FROM_ALL
    tests/camera_toon_tests.cpp
    tests/camera_toon_radio_fixture.cpp
    src/platform/windows/win32/win32_camera_toon_pass.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)
target_include_directories(helengine_windows_toon_tests PRIVATE src)
target_compile_features(helengine_windows_toon_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_toon_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_toon_tests PRIVATE d3d11 d3dcompiler dxgi)

# Contour reduction tests cover fullscreen walls using owned integer 1x/4x targets only.
add_executable(helengine_windows_toon_outline_tile_tests EXCLUDE_FROM_ALL tests/camera_toon_outline_tile_tests.cpp)
target_include_directories(helengine_windows_toon_outline_tile_tests PRIVATE src)
target_compile_features(helengine_windows_toon_outline_tile_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_toon_outline_tile_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_toon_outline_tile_tests PRIVATE d3d11 d3dcompiler dxgi)

# General shader loading is a backend feature; these fixtures create no HWND or device context.
add_executable(helengine_windows_shader_tests EXCLUDE_FROM_ALL
    tests/shader_compilation_tests.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)
target_include_directories(helengine_windows_shader_tests PRIVATE src)
target_compile_features(helengine_windows_shader_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_shader_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_shader_tests PRIVATE d3dcompiler)

# Presentation health tests create only an HWND_MESSAGE receiver; no player or visible window.
add_executable(helengine_windows_presentation_health_tests EXCLUDE_FROM_ALL tests/presentation_health_tests.cpp)
target_include_directories(helengine_windows_presentation_health_tests PRIVATE src)
target_compile_features(helengine_windows_presentation_health_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_presentation_health_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_presentation_health_tests PRIVATE user32)

# Shared SSGI tests own synthetic HDR surfaces and WARP resources; voxel transport is not a dependency.
add_executable(helengine_windows_ssgi_tests EXCLUDE_FROM_ALL tests/scene_ssgi_tests.cpp
    src/platform/windows/win32/win32_scene_ssgi.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)
target_include_directories(helengine_windows_ssgi_tests PRIVATE src)
target_compile_features(helengine_windows_ssgi_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_ssgi_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_ssgi_tests PRIVATE d3d11 d3dcompiler)

# Probe diagnostics use owned WARP textures and remain testable without the optional transport module.
add_executable(helengine_windows_gi_spheres_tests EXCLUDE_FROM_ALL tests/scene_gi_spheres_tests.cpp src/platform/windows/win32/win32_scene_gi_spheres.cpp)
target_include_directories(helengine_windows_gi_spheres_tests PRIVATE src)
target_compile_features(helengine_windows_gi_spheres_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_gi_spheres_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_gi_spheres_tests PRIVATE d3d11 d3dcompiler)

# Raw indirect-only material fixtures use actual production shaders and owned headless WARP surfaces.
add_executable(helengine_windows_gi_indirect_only_tests EXCLUDE_FROM_ALL tests/scene_gi_indirect_only_tests.cpp)
target_include_directories(helengine_windows_gi_indirect_only_tests PRIVATE src)
target_compile_features(helengine_windows_gi_indirect_only_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_gi_indirect_only_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_gi_indirect_only_tests PRIVATE d3d11 d3dcompiler)

# Receiver usage runs actual full-resolution depth/reader shaders without optional engine scene types.
add_executable(helengine_windows_gi_usage_tests EXCLUDE_FROM_ALL tests/scene_gi_usage_tests.cpp src/platform/windows/win32/win32_scene_gi_usage.cpp src/platform/windows/win32/win32_scene_gi_spheres.cpp)
target_include_directories(helengine_windows_gi_usage_tests PRIVATE src)
target_compile_features(helengine_windows_gi_usage_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_gi_usage_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_gi_usage_tests PRIVATE d3d11 d3dcompiler)

if(HELENGINE_ENABLE_LIGHTMAP)
add_executable(helengine_windows_lightmap_tests EXCLUDE_FROM_ALL tests/scene_lightmap_tests.cpp)
target_include_directories(helengine_windows_lightmap_tests PRIVATE src "${HELENGINE_LIGHTMAP_ROOT}/include")
target_compile_features(helengine_windows_lightmap_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_lightmap_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_lightmap_tests PRIVATE d3d11 d3dcompiler)
endif()

# Material culling uses owned WARP geometry with real ordinary, toon and baked draw bindings.
add_executable(helengine_windows_material_culling_tests EXCLUDE_FROM_ALL
    tests/material_culling_tests.cpp
    src/platform/windows/win32/win32_camera_toon_pass.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)
target_include_directories(helengine_windows_material_culling_tests PRIVATE src)
target_compile_features(helengine_windows_material_culling_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_material_culling_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX HELENGINE_LIGHTMAP)
target_link_libraries(helengine_windows_material_culling_tests PRIVATE d3d11 d3dcompiler dxgi)

# Alpha materials use owned WARP attachments and the actual toon/baked shaders.
add_executable(helengine_windows_material_alpha_tests EXCLUDE_FROM_ALL
    tests/material_alpha_blending_tests.cpp
    src/platform/windows/win32/win32_camera_toon_pass.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)
target_include_directories(helengine_windows_material_alpha_tests PRIVATE src)
target_compile_features(helengine_windows_material_alpha_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_material_alpha_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX HELENGINE_LIGHTMAP)
target_link_libraries(helengine_windows_material_alpha_tests PRIVATE d3d11 d3dcompiler dxgi)

# Metallic sun tests exercise real ordinary/baked shaders and API bindings on owned WARP resources.
add_executable(helengine_windows_metallic_sun_tests EXCLUDE_FROM_ALL
    tests/metallic_sun_tests.cpp
    src/platform/windows/win32/win32_camera_toon_pass.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)

target_include_directories(helengine_windows_metallic_sun_tests PRIVATE src "${HELENGINE_LIGHTMAP_ROOT}/include")
target_compile_features(helengine_windows_metallic_sun_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_metallic_sun_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX HELENGINE_LIGHTMAP)
target_link_libraries(helengine_windows_metallic_sun_tests PRIVATE d3d11 d3dcompiler dxgi)

# Scene-probe fixtures additionally consume the optional portable baked asset contract.
if(EXISTS "${HELENGINE_LIGHTMAP_ROOT}/include/hellm/ReflectionProbe.h")
    target_compile_definitions(helengine_windows_metallic_sun_tests PRIVATE HELENGINE_PROBE_TESTS)
endif()

# Realtime ink tests use only owned WARP resources and production cube/lighting helpers.
add_executable(helengine_windows_ink_tests EXCLUDE_FROM_ALL tests/scene_ink_tests.cpp)
target_include_directories(helengine_windows_ink_tests PRIVATE src)
target_compile_features(helengine_windows_ink_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_ink_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_ink_tests PRIVATE d3d11 d3dcompiler dxgi)


# Native HDR transfer tests evaluate real scene/UI shaders against owned offscreen float pixels only.
add_executable(helengine_windows_hdr_output_tests EXCLUDE_FROM_ALL tests/native_hdr_output_tests.cpp
    src/platform/windows/win32/win32_camera_toon_pass.cpp
    src/platform/windows/directx11/directx11_shader_compilation.cpp)
target_include_directories(helengine_windows_hdr_output_tests PRIVATE src)
target_compile_features(helengine_windows_hdr_output_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_hdr_output_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_hdr_output_tests PRIVATE d3d11 d3dcompiler dxgi)

# Hidden application-owned composition fixtures never show windows or read desktop content.
add_executable(helengine_windows_hdr_composition_tests EXCLUDE_FROM_ALL
    tests/native_hdr_composition_tests.cpp
    src/helengine_windows.manifest
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_output_attachments.cpp
    src/platform/windows/directx11/directx11_hit_test_sampler.cpp
    src/platform/windows/directx11/directx11_hresult_formatter.cpp
    src/platform/windows/runtime/runtime_render_diagnostics.cpp
    src/platform/windows/runtime/runtime_memory_snapshot.cpp)
target_include_directories(helengine_windows_hdr_composition_tests PRIVATE src)
target_compile_features(helengine_windows_hdr_composition_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_hdr_composition_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_hdr_composition_tests PRIVATE d3d11 dxgi dcomp user32 psapi)

# Stable cascades use numerical receiver coverage plus owned WARP atlas fixtures, without desktop surfaces.
add_executable(helengine_windows_shadow_cascade_tests EXCLUDE_FROM_ALL tests/shadow_cascade_tests.cpp)
target_include_directories(helengine_windows_shadow_cascade_tests PRIVATE src)
target_compile_features(helengine_windows_shadow_cascade_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_shadow_cascade_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_shadow_cascade_tests PRIVATE d3d11 d3dcompiler dxgi)

# Optional animation tests execute the production compute path on owned WARP buffers without desktop surfaces.
add_executable(helengine_windows_gpu_skin_tests EXCLUDE_FROM_ALL tests/gpu_skin_tests.cpp)
target_include_directories(helengine_windows_gpu_skin_tests PRIVATE src)
target_compile_features(helengine_windows_gpu_skin_tests PRIVATE cxx_std_20)
target_compile_definitions(helengine_windows_gpu_skin_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(helengine_windows_gpu_skin_tests PRIVATE d3d11 d3dcompiler dxgi)

# The approved SDR transform is immutable data embedded in every executable owning a toon pass.
configure_file(src/platform/windows/directx11/color/aces_sdr.rc.in "${CMAKE_CURRENT_BINARY_DIR}/aces_sdr.rc" @ONLY)
foreach(aces_target helengine_windows helengine_windows_toon_tests helengine_windows_material_culling_tests
        helengine_windows_material_alpha_tests helengine_windows_metallic_sun_tests helengine_windows_hdr_output_tests)
    target_sources(${aces_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/aces_sdr.rc")
endforeach()
