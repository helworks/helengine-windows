cmake_minimum_required(VERSION 3.26)

project(helengine_windows LANGUAGES CXX)

set(HELENGINE_CORE_CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/generated/helengine.core" CACHE PATH "Path to the generated helengine.core C++ output folder.")
set(HELENGINE_CODE_ROOT "" CACHE PATH "Path to the staged generated code-module root folder.")
set(HELENGINE_WINDOWS_RENDER_BACKEND "DirectX11" CACHE STRING "Windows rendering backend to compile.")
set(HELENGINE_WINDOWS_NATIVE_PROFILE "Debug" CACHE STRING "Native Windows player profile used to select profile-specific dependencies.")
option(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE "Build the Windows host with generated helengine.core sources." ON)
set_property(CACHE HELENGINE_WINDOWS_RENDER_BACKEND PROPERTY STRINGS DirectX11 Vulkan)
set_property(CACHE HELENGINE_WINDOWS_NATIVE_PROFILE PROPERTY STRINGS Debug Release Profiler)

if(NOT HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Debug" AND NOT HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Release" AND NOT HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Profiler")
    message(FATAL_ERROR "HELENGINE_WINDOWS_NATIVE_PROFILE must be Debug, Release, or Profiler.")
endif()

if(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
    set(HELENGINE_CORE_TRANSLATION_UNIT "")

    if("${HELENGINE_CORE_CPP_ROOT}" STREQUAL "")
        message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT must point at the generated helengine.core C++ output folder.")
    endif()

    if(NOT EXISTS "${HELENGINE_CORE_CPP_ROOT}/helcpp_config.hpp")
        message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT does not contain helcpp_config.hpp. Regenerate helengine.core C++ output first.")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_windows_handoff.cmake")
        include("${HELENGINE_CORE_CPP_ROOT}/helengine_windows_handoff.cmake")
    endif()

    if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_core_unity.cpp")
        set(HELENGINE_CORE_TRANSLATION_UNIT "${HELENGINE_CORE_CPP_ROOT}/helengine_core_unity.cpp")
    elseif(EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_core_amalgamated.cpp")
        set(HELENGINE_CORE_TRANSLATION_UNIT "${HELENGINE_CORE_CPP_ROOT}/helengine_core_amalgamated.cpp")
    else()
        message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT does not contain helengine_core_unity.cpp or helengine_core_amalgamated.cpp. Regenerate helengine.core C++ output first.")
    endif()
endif()

if(NOT HELENGINE_WINDOWS_RENDER_BACKEND STREQUAL "DirectX11")
    message(FATAL_ERROR "Only the DirectX11 bootstrap scaffold exists today. Add the Vulkan path before selecting that backend.")
endif()

set(HELENGINE_WINDOWS_SOURCES
    src/main.cpp
    src/platform/windows/runtime/runtime_memory_diagnostics_provider.cpp
    src/platform/windows/runtime/runtime_memory_snapshot.cpp
    src/platform/windows/runtime/runtime_player_profile.cpp
    src/platform/windows/runtime/runtime_player_profile_loader.cpp
    src/platform/windows/runtime/runtime_render_diagnostics.cpp
    src/platform/windows/win32/win32_application.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_presenter.cpp
)

if(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
    list(APPEND HELENGINE_WINDOWS_SOURCES
        src/platform/windows/win32/win32_audio_backend.cpp
        src/platform/windows/win32/win32_input_bridge.cpp
        src/platform/windows/win32/win32_render_bridge.cpp
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

target_compile_features(helengine_windows PRIVATE cxx_std_20)

if(HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL "Profiler")
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy instrumentation for the native profiler player profile." FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy" "${CMAKE_CURRENT_BINARY_DIR}/third_party/tracy")
    target_compile_definitions(helengine_windows PRIVATE TRACY_ENABLE)
    target_link_libraries(helengine_windows PRIVATE TracyClient)
endif()

target_compile_definitions(helengine_windows PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Debug>:HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS>
)

if(MSVC)
    target_compile_options(helengine_windows PRIVATE /bigobj)
endif()

target_include_directories(helengine_windows PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(HELENGINE_WINDOWS_INCLUDE_GENERATED_CORE)
    target_include_directories(helengine_windows PRIVATE ${HELENGINE_CORE_CPP_ROOT})
endif()

target_link_libraries(helengine_windows PRIVATE
    d3d11
    d3dcompiler
    dbghelp
    dxgi
    psapi
    user32
    gdi32
    winmm
)
