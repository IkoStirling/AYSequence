cmake_minimum_required(VERSION 3.25)
project(AYSequence VERSION 0.1.0 LANGUAGES CXX)

# Bootstrap only: no sequence playback or engine installation exists yet.
# S1 will turn this target into the independent runtime implementation.
add_library(AYSequence INTERFACE)
add_library(AYSequence::Core ALIAS AYSequence)
target_compile_features(AYSequence INTERFACE cxx_std_23)
target_include_directories(AYSequence INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
