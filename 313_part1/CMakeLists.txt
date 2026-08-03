cmake_minimum_required(VERSION 3.15)
# 取一个更大气的项目名
project(Medical_Vision_System)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 找外援：OpenCV
find_package(OpenCV REQUIRED)

if(NOT DEFINED ONNXRUNTIME_DIR)
    message(FATAL_ERROR "Please set ONNXRUNTIME_DIR to your ONNX Runtime folder")
endif()

include_directories("${ONNXRUNTIME_DIR}/include")
link_directories("${ONNXRUNTIME_DIR}/lib")

# ==========================================
# 【核心动作 1：制造砖块】把视觉代码打包成一个库
# ==========================================

add_library(VisionModule
    src/VisionSensor.cpp
    src/Letterbox.cpp
    src/OnnxModel.cpp
    src/VisionProcessor.cpp
)

# 给我们的库配上 OpenCV 的武器！
# PUBLIC 是现代 CMake 的灵魂！它的意思是：
# “不仅 VisionModule 自己要用到 OpenCV，以后谁敢调用我 VisionModule，谁也就自动拥有了 OpenCV 的超能力和头文件路径！”
target_link_libraries(VisionModule PUBLIC ${OpenCV_LIBS})

# 告诉别人去哪里找我们的说明书 (VisionSensor.h)
target_include_directories(VisionModule PUBLIC src)


# ==========================================
# 【核心动作 2：盖大楼】生成最终的主程序 exe
# ==========================================
# 现在的 main.cpp 会变得极其简单
add_executable(VisionApp app/main.cpp)
target_compile_definitions(VisionApp PRIVATE MODEL_PATH="${CMAKE_SOURCE_DIR}/models/best.onnx")

if(MSVC)
    target_compile_options(VisionModule PUBLIC /utf-8)
    target_compile_options(VisionApp PRIVATE /utf-8)
endif()

# ==========================================
# 【核心动作 3：组装】将库链接到主程序
# ==========================================
# PRIVATE 的意思是：VisionApp 自己偷偷用 VisionModule 这个库就行了，不需要再把它传染给别人。
target_link_libraries(VisionApp PRIVATE VisionModule onnxruntime)

if(WIN32)
    target_link_libraries(VisionApp PRIVATE Ws2_32)
endif()

# ==========================================
# 【终极防弹衣】编译完成后，自动把 OpenCV 的 DLL 复制到 exe 旁边
# ==========================================
add_custom_command(TARGET VisionApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
    $<TARGET_FILE_DIR:VisionApp>
)
if(WIN32)
    # 找到 OpenCV bin 目录下的所有 .dll 文件
    file(GLOB OpenCV_DLLS "${OpenCV_DIR}/x64/vc16/bin/*.dll")
    
    # 命令 CMake：在 VisionApp 编译完成后，执行复制动作
    add_custom_command(TARGET VisionApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OpenCV_DLLS}
        $<TARGET_FILE_DIR:VisionApp>
    )
endif()

