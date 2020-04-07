cmake_minimum_required(VERSION 3.10)

project(qt-rhi-bgfx)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_PREFIX_PATH "D:/Qt/Qt5.14.2/5.14.2/msvc2017_64")
set(CMAKE_MODULE_PATH "E:/tmp/proto-bgfx/proto-bgfx/cmake")
set(BGFX_ROOT "E:/tmp/proto-bgfx/bgfx.cmake/bgfx-install/x64")

find_package(Qt5 COMPONENTS Widgets Qml Quick REQUIRED)
find_package(bgfx REQUIRED)
include(Shader)

set(BGFX_SHADERS
    cubes.vert.sc
    cubes.frag.sc)

#set(RENDERER_OpenGL "ON")

set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR})
set(SHADER_DIR ${CMAKE_SOURCE_DIR})
foreach(SHADER ${BGFX_SHADERS})
    compile_shader(${SHADER})
endforeach()
message(STATUS "OUT_SHADERS = ${OUT_SHADERS}")

set(RESOURCES 
    bgfxqml.qrc
    main.qml)

add_executable(${PROJECT_NAME}
    main.cpp
    bgfxItem.h bgfxItem.cpp
    cubes.h
    ${RESOURCES}
    ${BGFX_SHADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${BGFX_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PUBLIC Qt5::Widgets Qt5::Qml Qt5::Quick d3d11 d3dcompiler)
target_link_libraries(${PROJECT_NAME} PUBLIC 
    $<$<CONFIG:Debug>:${BGFX_LIBRARY_DEBUG}>
    $<$<CONFIG:Release>:${BGFX_LIBRARY_RELEASE}>
    $<$<CONFIG:Debug>:${BIMG_LIBRARY_DEBUG}>
    $<$<CONFIG:Release>:${BIMG_LIBRARY_RELEASE}>
    $<$<CONFIG:Debug>:${BX_LIBRARY_DEBUG}> # BX not before BIMG
    $<$<CONFIG:Release>:${BX_LIBRARY_RELEASE}>
    $<$<CONFIG:Debug>:${ASTCCODEC_LIBRARY_DEBUG}>
    $<$<CONFIG:Release>:${ASTCCODEC_LIBRARY_RELEASE}>)