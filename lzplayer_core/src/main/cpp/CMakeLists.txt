cmake_minimum_required(VERSION 3.18.1)

project("lzplayer_core")

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 获取源文件列表
file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/**.cpp)
file(GLOB_RECURSE SRC_UTILS_FILES ${SRC_DIR}/utils/**.cpp)

add_library(
        lzplayer_core
        SHARED
        ${SRC_FILES} ${SRC_UTILS_FILES})

set(LIB_THIRD_PATH "../jniLibs")

find_library(
        log-lib
        log)

target_link_directories(lzplayer_core PUBLIC "${LIB_THIRD_PATH}/libffmpeg/${CMAKE_ANDROID_ARCH_ABI}")
target_include_directories(lzplayer_core PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/include/ffmpeg
        ${CMAKE_CURRENT_LIST_DIR}/utils
        )

target_link_libraries( # Specifies the target library.
        lzplayer_core
        avformat
        avcodec
        avutil
        swscale
        swresample
        avfilter
        android
        ${log-lib})