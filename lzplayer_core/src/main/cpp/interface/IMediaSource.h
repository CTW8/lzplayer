//
// IMediaSource.h - Cross-platform media source interface
// This interface provides abstraction for media source implementations
// using FFmpeg avformat for demuxing
//

#ifndef __I_MEDIA_SOURCE__
#define __I_MEDIA_SOURCE__

#include <memory>
#include <string>
#include <cstdint>
#include "VEError.h"

namespace VE {

// Media source type enumeration
enum class MediaSourceType {
    FILE,           // Local file
    NETWORK,        // Network stream (HTTP, RTSP, etc.)
    LIVE_STREAM     // Live stream (HLS, RTMP, etc.)
};

// Media source configuration
struct MediaSourceConfig {
    std::string url;                    // Media URL or file path
    MediaSourceType type;               // Source type
    int64_t bufferSizeMs = 3000;       // Buffer size in milliseconds
    int retryCount = 3;                // Number of retries on failure
    bool enableHardwareAccel = false;  // Enable hardware acceleration if available
};

// Cross-platform media source interface
// Implementations use FFmpeg avformat for actual demuxing
class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    // Open the media source
    virtual VEResult open(const MediaSourceConfig& config) = 0;

    // Close the media source
    virtual VEResult close() = 0;

    // Read a packet from the source
    // Returns VE_OK on success, VE_EOS on end of stream
    virtual VEResult readPacket(void* packet) = 0;

    // Seek to a specific timestamp (in microseconds)
    virtual VEResult seek(int64_t timestampUs) = 0;

    // Get stream information
    virtual int64_t getDuration() const = 0;  // Duration in microseconds
    virtual int getVideoStreamIndex() const = 0;
    virtual int getAudioStreamIndex() const = 0;

    // Get codec parameters for decoder initialization
    virtual void* getVideoCodecParameters() const = 0;
    virtual void* getAudioCodecParameters() const = 0;
};

} // namespace VE

#endif // __I_MEDIA_SOURCE__
