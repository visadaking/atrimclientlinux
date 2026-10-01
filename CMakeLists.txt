cmake_minimum_required(VERSION 3.16)
project(AtrimLauncher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL)

qt_add_executable(atrim-launcher main.cpp)
target_link_libraries(atrim-launcher PRIVATE Qt6::Widgets Qt6::OpenGL)
