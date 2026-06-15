cmake_minimum_required(VERSION 3.20)

project(GraphicsCapture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(GraphicsCapture SHARED
    src/GraphicsCapture.cpp
    src/WindowCapture.cpp
    src/WindowCaptureWgc.cpp
    src/WindowCapturePrintWindow.cpp
    src/WindowCaptureDesktopDup.cpp
    src/CaptureSession.cpp
)

target_compile_definitions(GraphicsCapture PRIVATE GRAPHICSCAPTURE_EXPORTS)

target_include_directories(GraphicsCapture
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(GraphicsCapture PRIVATE
    user32
    gdi32
    dwmapi
    d3d11
    dxgi
    windowsapp
)

if(MSVC)
    target_compile_options(GraphicsCapture PRIVATE /W4 /permissive-)
else()
    target_compile_options(GraphicsCapture PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(GraphicsCapture PROPERTIES
    OUTPUT_NAME "GraphicsCapture"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

add_executable(GCExample examples/example.cpp)
target_link_libraries(GCExample PRIVATE GraphicsCapture)
set_target_properties(GCExample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(CaptureGw2 examples/capture_gw2.cpp)
target_link_libraries(CaptureGw2 PRIVATE GraphicsCapture windowscodecs ole32)
set_target_properties(CaptureGw2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(GCSessionExample examples/session_example.cpp)
target_link_libraries(GCSessionExample PRIVATE GraphicsCapture)
set_target_properties(GCSessionExample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(TestPrintWindow examples/test_printwindow.cpp)
target_include_directories(TestPrintWindow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
target_link_libraries(TestPrintWindow PRIVATE GraphicsCapture windowscodecs ole32)
set_target_properties(TestPrintWindow PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(TestDesktopDup examples/test_desktop_dup.cpp)
target_include_directories(TestDesktopDup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
target_link_libraries(TestDesktopDup PRIVATE GraphicsCapture windowscodecs ole32)
set_target_properties(TestDesktopDup PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
