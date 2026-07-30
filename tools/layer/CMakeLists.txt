cmake_minimum_required(VERSION 3.24)
project(SmartTouchXR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    OpenXRSDK
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.59
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(OpenXRSDK)

add_library(SmartTouchXR SHARED smarttouchxr_layer.cpp)

target_link_libraries(
    SmartTouchXR
    PRIVATE
    OpenXR::headers
)

target_compile_definitions(
    SmartTouchXR
    PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set_target_properties(
    SmartTouchXR
    PROPERTIES
    OUTPUT_NAME "SmartTouchXR"
)
