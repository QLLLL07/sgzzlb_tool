# CMakeLists.txt - 构建 libsgzzlb 动态库（Windows 侧 __declspec(dllexport) 兼容）
cmake_minimum_required(VERSION 3.16)
project(sgzzlb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SRC
    core/data.cpp
    core/effects.cpp
    core/battle.cpp
    core/scoring.cpp
    core/tactic_assign.cpp
    core/recommend.cpp
    core/api.cpp
)

add_library(sgzzlb SHARED ${CORE_SRC})
target_include_directories(sgzzlb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

if(MSVC)
    target_compile_options(sgzzlb PRIVATE /O2 /W4)
else()
    target_compile_options(sgzzlb PRIVATE -O2 -Wall -Wextra)
endif()
