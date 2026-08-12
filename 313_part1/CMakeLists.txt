cmake_minimum_required(VERSION 3.15)
project(Medical_Vision_System)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

#set(CMAKE_PREFIX_PATH "D:/qt5/6.8.0/msvc2022_64")
find_package(Qt6 REQUIRED COMPONENTS Widgets)

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(OpenCV REQUIRED)

if(NOT DEFINED ONNXRUNTIME_DIR)
    message(FATAL_ERROR "Please set ONNXRUNTIME_DIR to your ONNX Runtime folder")
endif()

include_directories("${ONNXRUNTIME_DIR}/include")
link_directories("${ONNXRUNTIME_DIR}/lib")

add_library(VisionModule
    src/ConfigManager.cpp
    src/Logger.cpp
    src/VisionSensor.cpp
    src/Letterbox.cpp
    src/OnnxModel.cpp
    src/VisionProcessor.cpp
)

target_link_libraries(VisionModule PUBLIC ${OpenCV_LIBS} spdlog::spdlog_header_only nlohmann_json::nlohmann_json)
target_include_directories(VisionModule PUBLIC src)

add_executable(VisionApp
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/VisionWorker.cpp
    app/VisionWorker.h
)
if(MSVC)
    target_compile_options(VisionModule PUBLIC /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VisionApp PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
endif()

target_link_libraries(VisionApp PRIVATE VisionModule onnxruntime spdlog::spdlog_header_only Qt6::Widgets)
if(WIN32)
    target_link_libraries(VisionApp PRIVATE Ws2_32)
endif()

add_custom_command(TARGET VisionApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
    $<TARGET_FILE_DIR:VisionApp>
)

add_custom_command(TARGET VisionApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/config.json"
    $<TARGET_FILE_DIR:VisionApp>
)

if(WIN32)
    file(GLOB OpenCV_DLLS "${OpenCV_DIR}/x64/vc16/bin/*.dll")

    add_custom_command(TARGET VisionApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OpenCV_DLLS}
        $<TARGET_FILE_DIR:VisionApp>
    )
endif()
