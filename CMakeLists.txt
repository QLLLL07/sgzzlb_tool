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

option(SGZZLB_BUILD_QT_APP "Build the native Qt Widgets desktop application" ON)

if(SGZZLB_BUILD_QT_APP)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        set(SGZZLB_QT_WIDGETS_TARGET Qt6::Widgets)
    else()
        find_package(Qt5 COMPONENTS Widgets QUIET)
        if(Qt5_FOUND)
            set(SGZZLB_QT_WIDGETS_TARGET Qt5::Widgets)
        endif()
    endif()

    if(SGZZLB_QT_WIDGETS_TARGET)
        add_executable(sgzzlb_qt app/qt/main.cpp)
        target_include_directories(sgzzlb_qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
        target_link_libraries(sgzzlb_qt PRIVATE sgzzlb ${SGZZLB_QT_WIDGETS_TARGET})
        target_compile_features(sgzzlb_qt PRIVATE cxx_std_17)
        if(UNIX AND NOT APPLE)
            set_target_properties(sgzzlb_qt PROPERTIES BUILD_RPATH "$ORIGIN")
        endif()
    else()
        message(STATUS "Qt 5/6 Widgets not found; sgzzlb_qt will not be built.")
    endif()
endif()
