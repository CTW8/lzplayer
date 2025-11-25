# Linux Platform Implementation

This directory will contain Linux-specific implementations for the lzplayer core.

## Planned Components

### Video Rendering
- `VEGLVideoRenderer.cpp/h` - OpenGL-based video renderer using X11/Wayland

### Audio Rendering
- `VEAlsaAudioRender.cpp/h` - ALSA-based audio renderer
- `VEPulseAudioRender.cpp/h` - PulseAudio-based audio renderer

## Building on Linux

Requirements:
- FFmpeg development libraries (libavformat-dev, libavcodec-dev, etc.)
- OpenGL development libraries
- ALSA or PulseAudio development libraries

Build command:
```bash
mkdir build && cd build
cmake ..
make
```

## Status

This is a placeholder for future Linux platform implementation.
The core player architecture (VEPlayer, VEDemux, VEVideoDecoder, VEAudioDecoder)
is now platform-independent and uses FFmpeg for demuxing and decoding.
