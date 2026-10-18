cmake_minimum_required(VERSION 3.15)
project(Medical_Vision_System)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
include(GNUInstallDirs)

option(
    MEDICAL_VISION_ENABLE_REALSENSE
    "Build the optional Intel RealSense camera backend"
    OFF)
set(
    REALSENSE_DIR
    ""
    CACHE PATH
    "librealsense source/install root")

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
    src/MockCamera.cpp
    src/RealSenseCamera.cpp
    src/Letterbox.cpp
    src/OnnxModel.cpp
    src/VisionProcessor.cpp
)

target_link_libraries(VisionModule PUBLIC ${OpenCV_LIBS} spdlog::spdlog_header_only nlohmann_json::nlohmann_json)
target_include_directories(VisionModule PUBLIC src)

if(MEDICAL_VISION_ENABLE_REALSENSE)
    if(NOT EXISTS "${REALSENSE_DIR}/include/librealsense2/rs.hpp")
        message(FATAL_ERROR
            "REALSENSE_DIR must contain include/librealsense2/rs.hpp")
    endif()

    target_compile_definitions(
        VisionModule
        PRIVATE
        MEDICAL_VISION_ENABLE_REALSENSE)
    target_include_directories(
        VisionModule
        PRIVATE
        "${REALSENSE_DIR}/include")
    target_link_directories(
        VisionModule
        PRIVATE
        "${REALSENSE_DIR}/lib"
        "${REALSENSE_DIR}/x64/Release"
        "${REALSENSE_DIR}/x64/Debug")
    target_link_libraries(VisionModule PRIVATE realsense2)
endif()

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

add_custom_command(TARGET VisionApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/models"
    "$<TARGET_FILE_DIR:VisionApp>/models"
)

if(WIN32)
    file(GLOB OpenCV_DLLS "${OpenCV_DIR}/x64/vc16/bin/*.dll")

    add_custom_command(TARGET VisionApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OpenCV_DLLS}
        $<TARGET_FILE_DIR:VisionApp>
    )

    get_target_property(WINDEPLOYQT_EXECUTABLE Qt6::windeployqt IMPORTED_LOCATION)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "Qt windeployqt tool was not found")
    endif()

    add_custom_command(TARGET VisionApp POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations
                --no-compiler-runtime
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:VisionApp>"
        COMMENT "Deploying Qt runtime and plugins for $<CONFIG>"
        VERBATIM
    )
endif()

install(TARGETS VisionApp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WIN32)
    install(FILES
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        "${CMAKE_SOURCE_DIR}/config.json"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY
        "${OpenCV_DIR}/x64/vc16/bin/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*d.dll" EXCLUDE
    )

    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/models/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/models
    )

    qt_generate_deploy_app_script(
        TARGET VisionApp
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_TRANSLATIONS
    )
    install(SCRIPT ${deploy_script})
endif()

set(CPACK_PACKAGE_NAME "MedicalVision")
set(CPACK_PACKAGE_VENDOR "Medical Vision Controller")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Medical Vision Controller application")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MedicalVision")
set(CPACK_PACKAGE_EXECUTABLES "bin/VisionApp" "Medical Vision Controller")

set(CPACK_GENERATOR "ZIP")
if(WIN32)
    find_program(NSIS_MAKE_EXECUTABLE NAMES makensis)
    if(NSIS_MAKE_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
    endif()
endif()

include(CPack)
