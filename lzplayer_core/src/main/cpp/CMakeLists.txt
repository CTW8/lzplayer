cmake_minimum_required(VERSION 3.18.1)

project("lzplayer_core")

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 获取源文件列表
file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/**.cpp)
#file(GLOB_RECURSE SRC_UTILS_FILES ${SRC_DIR}/utils/**.cpp)

add_library(
        lzplayer_core
        SHARED
        ${SRC_FILES} ${SRC_UTILS_FILES})

set(LIB_THIRD_PATH "../jniLibs")

find_library(
        log-lib
        log)


#target_compile_options(lzplayer_core PRIVATE -fsanitize=address -fno-omit-frame-pointer)
#target_link_options(lzplayer_core PRIVATE -fsanitize=address)

target_link_directories(lzplayer_core PUBLIC "${LIB_THIRD_PATH}/${CMAKE_ANDROID_ARCH_ABI}")


target_include_directories(lzplayer_core PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/core
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/include/libyuv
        ${CMAKE_CURRENT_LIST_DIR}/include/ffmpeg
        ${CMAKE_CURRENT_LIST_DIR}/include/sonic
        ${CMAKE_CURRENT_LIST_DIR}/utils
        ${CMAKE_CURRENT_LIST_DIR}/interface
        ${CMAKE_CURRENT_LIST_DIR}/platform
        ${CMAKE_CURRENT_LIST_DIR}/platform/android
        ${CMAKE_CURRENT_LIST_DIR}/platform/ios
        ${CMAKE_CURRENT_LIST_DIR}/platform/linux
        ${CMAKE_CURRENT_LIST_DIR}/thread
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
        OpenSLES
        GLESv3
        EGL
        yuv
        sonic
        ${log-lib})

target_compile_options(lzplayer_core PRIVATE
        # 忽略签名变化警告
        -Wno-sign-conversion
        # 忽略隐式整型到浮点转换精度警告
        -Wno-implicit-int-float-conversion
        # 如果 Clang 没识别上面那个标志，还可以再加：
        -Wno-implicit-int-conversion
        -Werror=nonportable-include-path
        -Wno-c99-extensions
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE -mfpu=neon -D__ARM_NEON__")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIE -D__ARM_NEON__ -pedantic")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fPIE -pie")
#add_definitions(-D__ARM_NEON__)
#add_definitions(-DLIBYUV_DISABLE_NEON=0)
#add_definitions(-D__aarch64__=0)
