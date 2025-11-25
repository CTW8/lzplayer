//
// Created for lzplayer demux redesign following NuPlayerSource pattern
// VELocalSource: Concrete implementation for local file playback (similar to GenericSource)
//

#ifndef LZPLAYER_VELOCALSOURCE_H
#define LZPLAYER_VELOCALSOURCE_H

#include "VESource.h"
#include <mutex>
#include <condition_variable>

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}

namespace VE {

    /**
     * Local file source implementation
     * Handles reading and demuxing local media files using FFmpeg
     */
    class VELocalSource : public VESource {
    public:
        VELocalSource();
        ~VELocalSource() override;

        // VESource interface implementation
        VEResult prepareAsync(const std::string& path) override;
        void start() override;
        void stop() override;
        void pause() override;
        void resume() override;
        VEResult read(bool isAudio, std::shared_ptr<VEPacket>& packet) override;
        VEResult seekTo(int64_t posMs) override;
        VEResult close() override;
        std::shared_ptr<VEMediaInfo> getMediaInfo() override;
        int64_t getDuration() override;
        uint32_t getFlags() override;
        void requestMoreData(std::shared_ptr<AMessage> msg, int type) override;

    protected:
        void onMessageReceived(const std::shared_ptr<AMessage>& msg) override;

    private:
        // Internal message handlers
        VEResult onPrepare(const std::string& path);
        VEResult onStart();
        VEResult onStop();
        VEResult onPause();
        VEResult onResume();
        VEResult onRead();
        VEResult onSeek(int64_t posMs);
        VEResult onClose();

        // Helper methods
        void putPacket(std::shared_ptr<VEPacket> packet, bool isAudio);
        void resetQueues();

    private:
        // Message types
        enum {
            kWhatPrepare    = 'prep',
            kWhatStart      = 'star',
            kWhatStop       = 'stop',
            kWhatPause      = 'paus',
            kWhatResume     = 'resm',
            kWhatSeek       = 'seek',
            kWhatRead       = 'read',
            kWhatClose      = 'clos',
        };

        // FFmpeg context
        AVFormatContext* mFormatContext = nullptr;

        // File info
        std::string mFilePath;
        int32_t mWidth = 0;
        int32_t mHeight = 0;
        uint64_t mDuration = 0;
        int32_t mFps = 0;

        // Video stream info
        AVCodecParameters* mVideoCodecParams = nullptr;
        AVRational mVideoTimeBase;
        int64_t mVStartTime = 0;
        int mVideoIndex = -1;

        // Audio stream info
        int32_t mSampleRate = 0;
        int32_t mChannel = 0;
        int32_t mSampleFormat = 0;
        AVCodecParameters* mAudioCodecParams = nullptr;
        AVRational mAudioTimeBase;
        int64_t mAStartTime = 0;
        int mAudioIndex = -1;

        // State flags
        bool mIsStarted = false;
        bool mIsPrepared = false;
        bool mIsEOS = false;

        // Data request handling
        bool mNeedAudioMore = false;
        bool mNeedVideoMore = false;
        std::shared_ptr<AMessage> mAudioNotify = nullptr;
        std::shared_ptr<AMessage> mVideoNotify = nullptr;

        // Thread synchronization
        std::mutex mMutexAudio;
        std::mutex mMutexVideo;

        // PTS tracking
        int64_t mAudioStartPts = -1;
        int64_t mVideoStartPts = -1;

        // Packet queues
        std::shared_ptr<VEPacketQueue> mVideoPacketQueue = nullptr;
        std::shared_ptr<VEPacketQueue> mAudioPacketQueue = nullptr;

        // Queue size limits
        static constexpr int AUDIO_QUEUE_SIZE = 100;
        static constexpr int VIDEO_QUEUE_SIZE = 100;
    };

} // namespace VE

#endif // LZPLAYER_VELOCALSOURCE_H
