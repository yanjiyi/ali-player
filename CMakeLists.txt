cmake_minimum_required(VERSION 3.5)

project(ali-player)

set(CMAKE_CXX_STANDARD 17)

include_directories("ffmpeg-build/include")

add_executable(ali-player main.cpp)

link_directories("ffmpeg-build/lib")

target_link_libraries(ali-player SDL2 GLEW GL X11 GLU avcodec avformat avutil swscale swresample)
