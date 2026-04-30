cmake_minimum_required(VERSION 3.26)

project(helengine_windows LANGUAGES CXX)

set(HELENGINE_CORE_CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/generated/helengine.core" CACHE PATH "Path to the generated helengine.core C++ output folder.")
set(HELENGINE_WINDOWS_RENDER_BACKEND "DirectX" CACHE STRING "Windows rendering backend to compile.")
set_property(CACHE HELENGINE_WINDOWS_RENDER_BACKEND PROPERTY STRINGS DirectX Vulkan)

if("${HELENGINE_CORE_CPP_ROOT}" STREQUAL "")
    message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT must point at the generated helengine.core C++ output folder.")
endif()

if(NOT EXISTS "${HELENGINE_CORE_CPP_ROOT}/helcpp_config.hpp")
    message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT does not contain helcpp_config.hpp. Regenerate helengine.core C++ output first.")
endif()

if(EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_windows_handoff.cmake")
    include("${HELENGINE_CORE_CPP_ROOT}/helengine_windows_handoff.cmake")
endif()

if(NOT EXISTS "${HELENGINE_CORE_CPP_ROOT}/helengine_core_unity.cpp")
    message(FATAL_ERROR "HELENGINE_CORE_CPP_ROOT does not contain helengine_core_unity.cpp. Regenerate helengine.core C++ output first.")
endif()

if(NOT HELENGINE_WINDOWS_RENDER_BACKEND STREQUAL "DirectX")
    message(FATAL_ERROR "Only the DirectX bootstrap scaffold exists today. Add the Vulkan path before selecting that backend.")
endif()

add_executable(helengine_windows
    ${HELENGINE_CORE_CPP_ROOT}/helengine_core_unity.cpp
    src/main.cpp
    src/platform/windows/directx/directx_feature_bootstrap.cpp
)

target_compile_features(helengine_windows PRIVATE cxx_std_20)

target_compile_definitions(helengine_windows PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(helengine_windows PRIVATE
    ${HELENGINE_CORE_CPP_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
