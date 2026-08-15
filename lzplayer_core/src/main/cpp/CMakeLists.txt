cmake_minimum_required(VERSION 3.18.1)

project("lzplayer_core")

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 获取源文件列表
# 日志开销对照实验开关：ON 时即使 Debug 构建也编译期剔除 V/D/I 日志。
# 用法: -DVE_QUIET_LOG=ON。native ALOGV 在解码/解封装路径上每帧数条，
# 30fps 下每秒上千次 __android_log_print(格式化 + 写 socket)，本身就在吃 CPU。
option(VE_TRACE_FRAME "Enable per-frame/per-packet ALOGF logs" OFF)
if (VE_TRACE_FRAME)
    add_compile_definitions(VE_TRACE_FRAME)
    message(STATUS "VE_TRACE_FRAME=ON: per-frame logs enabled")
endif ()

option(VE_QUIET_LOG "Compile out V/D/I logs even in debug builds" OFF)
if (VE_QUIET_LOG)
    add_compile_definitions(VE_QUIET_LOG)
    message(STATUS "VE_QUIET_LOG=ON: V/D/I logs compiled out")
endif()

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

# 注意：libaaudio 不能直接链接。它从 API 26 才存在，而本工程 minSdk 是 24，
# 一旦成为 DT_NEEDED 硬依赖，24/25 机型上整个 .so 都会加载失败。
# 因此 AAudio 走运行期 dlopen + dlsym(见 VEAAudioRender)。


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
        ${CMAKE_CURRENT_LIST_DIR}/platform/android/renders
        ${CMAKE_CURRENT_LIST_DIR}/platform/android/decoders
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
        mediandk
        # 网络源的 HTTPS 支持：自实现 HTTP 客户端直接用 OpenSSL 做 TLS
        ssl
        crypto
        ${log-lib})

# minSdk 是 24，但我们要用 API 26+ 的 AAudio 与 API 28+ 的部分 NDK 媒体 API。
# 这个宏让高于 minSdk 的符号变成弱引用(而不是编译错误)，运行期判空即可决定
# 用不用——低版本机型会自动退回 SLES / 软解。
target_compile_definitions(lzplayer_core PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)

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
