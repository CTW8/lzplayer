//
// Created for lzplayer demux redesign following NuPlayerSource pattern
// VEGenericSource: Concrete implementation for local file playback
// Based on Android's NuPlayer::GenericSource design
//

#ifndef LZPLAYER_VEGENERICSOURCE_H
#define LZPLAYER_VEGENERICSOURCE_H

#include "VESource.h"
#include <mutex>
#include <condition_variable>
#include <vector>

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/avutil.h"
    #include "libavutil/timestamp.h"
}

namespace VE {

    /**
     * GenericSource implementation (based on Android's NuPlayer::GenericSource)
     * Handles reading and demuxing local media files using FFmpeg
     * 
     * Key behaviors matching NuPlayer::GenericSource:
     * - Async preparation with setDataSource() + prepareAsync()
     * - Track management (getTrackCount, getTrackInfo, selectTrack)
     * - Access unit dequeuing (dequeueAccessUnit)
     * - Buffering management
     * - Seek support with multiple modes
     */
    class VEGenericSource : public VESource {
    public:
        VEGenericSource();
        ~VEGenericSource() override;

        // ============ VESource interface implementation ============

        // Data source and preparation
        VEResult setDataSource(const std::string& path) override;
        VEResult prepareAsync() override;
        
        // Control
        void start() override;
        void stop() override;
        void pause() override;
        void resume() override;
        void disconnect() override;

        // Track management
        size_t getTrackCount() const override;
        VEResult getTrackInfo(size_t trackIndex, VETrackInfo* info) const override;
        ssize_t getSelectedTrack(VETrackInfo::TrackType type) const override;
        VEResult selectTrack(size_t trackIndex, bool select) override;

        // Format and duration
        VEResult getFormat(bool audio, std::shared_ptr<AMessage>* format) override;
        std::shared_ptr<VEMediaInfo> getMediaInfo() override;
        int64_t getDurationUs() override;
        uint32_t getFlags() override;

        // Data access
        VEResult dequeueAccessUnit(bool audio, std::shared_ptr<VEPacket>* accessUnit) override;
        void requestMoreData(std::shared_ptr<AMessage> msg, int type) override;

        // Seek
        VEResult seekTo(int64_t seekTimeUs, int32_t mode = SEEK_PREVIOUS_SYNC) override;

        // Buffering
        int64_t getBufferedPositionUs() override;
        bool isBuffering() override;

    protected:
        void onMessageReceived(const std::shared_ptr<AMessage>& msg) override;

    private:
        // Internal track representation
        struct Track {
            size_t index;
            VETrackInfo::TrackType type;
            AVStream* stream;
            AVCodecParameters* codecParams;
            std::shared_ptr<VEPacketQueue> packetQueue;
            bool selected;
            int64_t lastDequeuedTimeUs;
        };

        // Message types (matching NuPlayer pattern)
        enum {
            kWhatSetDataSource  = 'sDsS',
            kWhatPrepare        = 'prep',
            kWhatStart          = 'star',
            kWhatStop           = 'stop',
            kWhatPause          = 'paus',
            kWhatResume         = 'resm',
            kWhatSeek           = 'seek',
            kWhatReadBuffer     = 'rdBf',
            kWhatDisconnect     = 'disc',
            kWhatSelectTrack    = 'slTk',
        };

        // Internal message handlers
        VEResult onSetDataSource(const std::string& path);
        VEResult onPrepare();
        VEResult onStart();
        VEResult onStop();
        VEResult onPause();
        VEResult onResume();
        VEResult onReadBuffer();
        VEResult onSeek(int64_t seekTimeUs, int32_t mode);
        void onDisconnect();

        // Helper methods
        void initTracks();
        void putPacket(std::shared_ptr<VEPacket> packet, bool isAudio);
        void resetQueues();
        void notifyBufferingUpdate(int32_t percentage);
        void schedulePollBuffering();
        VEResult readBuffer(bool audio, int64_t* timeUs = nullptr, std::shared_ptr<VEPacket>* pkt = nullptr);

        // FFmpeg context
        AVFormatContext* mFormatContext = nullptr;

        // File info
        std::string mFilePath;
        int64_t mDurationUs = 0;

        // Track management
        std::vector<Track> mTracks;
        ssize_t mVideoTrackIndex = -1;
        ssize_t mAudioTrackIndex = -1;
        ssize_t mSubtitleTrackIndex = -1;

        // Video info
        int32_t mWidth = 0;
        int32_t mHeight = 0;
        int32_t mFps = 0;
        AVCodecParameters* mVideoCodecParams = nullptr;
        AVRational mVideoTimeBase;
        int64_t mVStartTime = 0;

        // Audio info
        int32_t mSampleRate = 0;
        int32_t mChannel = 0;
        int32_t mSampleFormat = 0;
        AVCodecParameters* mAudioCodecParams = nullptr;
        AVRational mAudioTimeBase;
        int64_t mAStartTime = 0;

        // State flags
        bool mIsStarted = false;
        bool mIsPrepared = false;
        bool mIsPreparing = false;
        bool mIsEOS = false;
        bool mIsDisconnecting = false;
        bool mStopRead = false;

        // Buffering state
        bool mIsBuffering = false;
        int32_t mBufferingPercentage = 0;
        int64_t mBufferedPositionUs = 0;

        // Data request handling
        bool mNeedAudioMore = false;
        bool mNeedVideoMore = false;
        std::shared_ptr<AMessage> mAudioNotify = nullptr;
        std::shared_ptr<AMessage> mVideoNotify = nullptr;

        // Thread synchronization
        mutable std::mutex mLock;
        mutable std::mutex mReadLock;

        // PTS tracking
        int64_t mAudioStartPts = -1;
        int64_t mVideoStartPts = -1;

        // Packet queues
        std::shared_ptr<VEPacketQueue> mVideoPacketQueue = nullptr;
        std::shared_ptr<VEPacketQueue> mAudioPacketQueue = nullptr;

        // Queue size limits (matching NuPlayer buffering)
        static constexpr int AUDIO_QUEUE_SIZE = 100;
        static constexpr int VIDEO_QUEUE_SIZE = 100;
        
        // Buffering thresholds
        static constexpr int64_t kLowWaterMarkUs = 2000000LL;   // 2 seconds
        static constexpr int64_t kHighWaterMarkUs = 5000000LL;  // 5 seconds
    };

} // namespace VE

#endif // LZPLAYER_VEGENERICSOURCE_H
