cmake_minimum_required(VERSION 3.22)
project(Torden VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

# fjord-base: the shared code of all Fjord plugins (UI, effects, build helpers, JUCE). A checkout next to this
# repository (../fjord-base) is used if present, otherwise the pinned release is downloaded when CMake configures.
set(FJORD_BASE_TAG "v0.1.0")
set(FJORD_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fjord-base" CACHE PATH "Local fjord-base checkout")
if(EXISTS "${FJORD_BASE_DIR}/cmake/FjordBase.cmake")
    add_subdirectory("${FJORD_BASE_DIR}" "${CMAKE_BINARY_DIR}/fjord-base")
else()
    include(FetchContent)
    FetchContent_Declare(fjord_base GIT_REPOSITORY https://github.com/fjord-sounds/fjord-base.git
                                    GIT_TAG ${FJORD_BASE_TAG} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(fjord_base)
endif()

# Torden plus its offline test tool TordenRenderTest (cmake --build build --target TordenRenderTest)
fjord_add_plugin(Torden
    PRODUCT_NAME "Torden"
    CODE Trdn
    BUNDLE_ID "com.fjord.torden"
    VST3_CATEGORIES Instrument Drum
    PRESETS "presets/Factory/*.torden")
