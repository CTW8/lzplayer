cmake_minimum_required(VERSION 3.18.1)

project("lzplayer_core")

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Platform detection
if(ANDROID)
    set(VE_PLATFORM_ANDROID ON)
    message(STATUS "Building for Android platform")
elseif(APPLE)
    if(IOS)
        set(VE_PLATFORM_IOS ON)
        message(STATUS "Building for iOS platform")
    else()
        set(VE_PLATFORM_MACOS ON)
        message(STATUS "Building for macOS platform")
    endif()
elseif(UNIX AND NOT APPLE)
    set(VE_PLATFORM_LINUX ON)
    message(STATUS "Building for Linux platform")
elseif(WIN32)
    set(VE_PLATFORM_WINDOWS ON)
    message(STATUS "Building for Windows platform")
endif()

# Get common source files (core, thread, utils, interface)
file(GLOB CORE_FILES ${SRC_DIR}/core/*.cpp)
file(GLOB THREAD_FILES ${SRC_DIR}/thread/*.cpp)
file(GLOB UTILS_FILES ${SRC_DIR}/utils/*.cpp)

# Platform-specific source files
if(VE_PLATFORM_ANDROID)
    file(GLOB_RECURSE PLATFORM_FILES 
        ${SRC_DIR}/platform/android/*.cpp
    )
elseif(VE_PLATFORM_LINUX)
    file(GLOB_RECURSE PLATFORM_FILES 
        ${SRC_DIR}/platform/linux/*.cpp
    )
elseif(VE_PLATFORM_IOS)
    file(GLOB_RECURSE PLATFORM_FILES 
        ${SRC_DIR}/platform/ios/*.cpp
        ${SRC_DIR}/platform/ios/*.mm
    )
elseif(VE_PLATFORM_MACOS)
    file(GLOB_RECURSE PLATFORM_FILES 
        ${SRC_DIR}/platform/macos/*.cpp
        ${SRC_DIR}/platform/macos/*.mm
    )
elseif(VE_PLATFORM_WINDOWS)
    file(GLOB_RECURSE PLATFORM_FILES 
        ${SRC_DIR}/platform/windows/*.cpp
    )
endif()

# Combine all source files
set(SRC_FILES 
    ${CORE_FILES}
    ${THREAD_FILES}
    ${UTILS_FILES}
    ${PLATFORM_FILES}
)

add_library(
        lzplayer_core
        SHARED
        ${SRC_FILES})

# Include directories (common for all platforms)
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
        ${CMAKE_CURRENT_LIST_DIR}/thread
        )

# Platform-specific configuration
if(VE_PLATFORM_ANDROID)
    set(LIB_THIRD_PATH "../jniLibs")
    
    find_library(log-lib log)
    
    target_link_directories(lzplayer_core PUBLIC "${LIB_THIRD_PATH}/${CMAKE_ANDROID_ARCH_ABI}")
    
    target_include_directories(lzplayer_core PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}/platform/android
            ${CMAKE_CURRENT_LIST_DIR}/platform/android/renders
            ${CMAKE_CURRENT_LIST_DIR}/platform/android/decoders
            )
    
    target_link_libraries(lzplayer_core
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
            -Wno-sign-conversion
            -Wno-implicit-int-float-conversion
            -Wno-implicit-int-conversion
            -Werror=nonportable-include-path
            -Wno-c99-extensions
    )
    
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE -mfpu=neon -D__ARM_NEON__")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIE -D__ARM_NEON__ -pedantic")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fPIE -pie")
    
elseif(VE_PLATFORM_LINUX)
    # Linux configuration
    find_package(PkgConfig REQUIRED)
    
    # Find FFmpeg libraries
    pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample libavfilter)
    
    target_include_directories(lzplayer_core PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}/platform/linux
            ${FFMPEG_INCLUDE_DIRS}
            )
    
    target_link_libraries(lzplayer_core
            ${FFMPEG_LIBRARIES}
            pthread
            )
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC")
    
elseif(VE_PLATFORM_MACOS OR VE_PLATFORM_IOS)
    # Apple platforms configuration
    target_include_directories(lzplayer_core PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}/platform/ios
            )
    
    # Find FFmpeg (can be installed via Homebrew on macOS)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG libavformat libavcodec libavutil libswscale libswresample libavfilter)
        if(FFMPEG_FOUND)
            target_include_directories(lzplayer_core PUBLIC ${FFMPEG_INCLUDE_DIRS})
            target_link_libraries(lzplayer_core ${FFMPEG_LIBRARIES})
        else()
            message(WARNING "FFmpeg not found. Some features may not work.")
        endif()
    else()
        message(WARNING "PkgConfig not found. FFmpeg detection skipped.")
    endif()
    
    target_link_libraries(lzplayer_core
            "-framework Foundation"
            "-framework CoreMedia"
            "-framework AVFoundation"
            )
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    
elseif(VE_PLATFORM_WINDOWS)
    # Windows configuration
    target_include_directories(lzplayer_core PUBLIC
            ${CMAKE_CURRENT_LIST_DIR}/platform/windows
            )
    
    # FFmpeg on Windows (requires manual setup or vcpkg)
    # Set FFMPEG_ROOT environment variable or use vcpkg
    if(DEFINED ENV{FFMPEG_ROOT})
        set(FFMPEG_ROOT $ENV{FFMPEG_ROOT})
        target_include_directories(lzplayer_core PUBLIC ${FFMPEG_ROOT}/include)
        target_link_directories(lzplayer_core PUBLIC ${FFMPEG_ROOT}/lib)
        target_link_libraries(lzplayer_core
                avformat
                avcodec
                avutil
                swscale
                swresample
                avfilter
        )
    else()
        message(WARNING "FFMPEG_ROOT not set. FFmpeg will not be linked on Windows.")
    endif()
    
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

