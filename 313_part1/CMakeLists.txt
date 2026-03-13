cmake_minimum_required(VERSION 3.15)
# 给你的项目起个霸气的名字
project(Medical_Vision_Controller)

# 强制使用 C++17 标准（现在的 AI 部署库基本都要求 14 或 17 起步）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(OpenCV_DIR "D:/opencv/build")

# 【核心动作】让 CMake 去你的电脑里寻找 OpenCV
find_package(OpenCV REQUIRED)

# 包含 OpenCV 的头文件
include_directories(${OpenCV_INCLUDE_DIRS})

# 告诉编译器，我们要用 main.cpp 生成一个叫 VisionBase 的可执行文件
add_executable(VisionBase main.cpp)

# 将 OpenCV 的动态链接库（DLL/LIB）跟我们的程序绑死
target_link_libraries(VisionBase ${OpenCV_LIBS})