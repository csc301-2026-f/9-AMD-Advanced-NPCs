# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
cmake_minimum_required(VERSION 3.20)
project(ScholaNativeLearning LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SCHOLA_RLTOOLS_ASAN "Check native learner memory safety with MSVC AddressSanitizer" OFF)
set(SCHOLA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(native_learning_tests Main.cpp
    "${SCHOLA_ROOT}/Source/ScholaRLTools/Private/Learning/RLToolsLearner.cpp")
target_include_directories(native_learning_tests PRIVATE
    "${SCHOLA_ROOT}/Source/ScholaRLTools/Private"
    "${SCHOLA_ROOT}/Source/ThirdParty/RLTools/include")
target_compile_definitions(native_learning_tests PRIVATE NOMINMAX)
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(native_learning_tests PRIVATE /fp:precise /GR- /EHs-c- /bigobj)
    target_compile_definitions(native_learning_tests PRIVATE _HAS_EXCEPTIONS=0)
    if(SCHOLA_RLTOOLS_ASAN)
        target_compile_options(native_learning_tests PRIVATE /fsanitize=address /Zi)
        target_link_options(native_learning_tests PRIVATE /INCREMENTAL:NO /DEBUG)
    endif()
endif()
enable_testing()
add_test(NAME native_learning COMMAND native_learning_tests)
