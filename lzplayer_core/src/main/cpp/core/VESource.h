//
// Created for lzplayer demux redesign following NuPlayerSource pattern
// Based on Android's NuPlayer::Source interface design
//

#ifndef LZPLAYER_VESOURCE_H
#define LZPLAYER_VESOURCE_H

#include <string>
#include <memory>
#include <vector>
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "VEPacketQueue.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEError.h"

namespace VE {

    // Forward declaration
    class VEBuffer;

    /**
     * Track information structure (similar to NuPlayer's TrackInfo)
     */
    struct VETrackInfo {
        enum TrackType {
            TRACK_TYPE_UNKNOWN = 0,
            TRACK_TYPE_VIDEO = 1,
            TRACK_TYPE_AUDIO = 2,
            TRACK_TYPE_TIMEDTEXT = 3,
            TRACK_TYPE_SUBTITLE = 4,
            TRACK_TYPE_METADATA = 5,
        };

        TrackType type = TRACK_TYPE_UNKNOWN;
        int32_t trackIndex = -1;
        std::string mimeType;
        std::string language;
        bool isDefault = false;
        bool isAutoSelect = false;

        // Video specific
        int32_t width = 0;
        int32_t height = 0;
        int32_t frameRate = 0;

        // Audio specific
        int32_t sampleRate = 0;
        int32_t channelCount = 0;
        int32_t bitDepth = 0;
    };

    /**
     * Abstract base class for media sources (following NuPlayerSource pattern)
     * Based on Android's NuPlayer::Source interface design
     * 
     * Provides interface for different types of media sources:
     * - GenericSource (local file)
     * - HTTPLiveSource (HLS streaming) - future
     * - RTSPSource (RTSP streaming) - future
     * - StreamingSource (MPEG-TS streaming) - future
     */
    class VESource : public AHandler {
    public:
        // Source capability flags (matching NuPlayer)
        enum Flags {
            FLAG_CAN_PAUSE          = 1,
            FLAG_CAN_SEEK_BACKWARD  = 2,
            FLAG_CAN_SEEK_FORWARD   = 4,
            FLAG_CAN_SEEK           = 8,
            FLAG_DYNAMIC_DURATION   = 16,
            FLAG_SECURE             = 32,
            FLAG_PROTECTED          = 64,
        };

        // Source status
        enum Status {
            STATUS_OK = 0,
            STATUS_ERROR = -1,
            STATUS_EOS = -2,
            STATUS_NOT_READY = -3,
            STATUS_WOULD_BLOCK = -4,
        };

        VESource() = default;
        virtual ~VESource() = default;

        // ============ NuPlayer Source interface methods ============

        /**
         * Set data source path/URI (called before prepareAsync)
         * Following NuPlayer pattern where setDataSource is separate from prepare
         * @param path File path or URI of the media source
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult setDataSource(const std::string& path) = 0;

        /**
         * Prepare the source asynchronously
         * Sends kWhatPrepared notification when done
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult prepareAsync() = 0;

        /**
         * Start reading from the source
         */
        virtual void start() = 0;

        /**
         * Stop reading from the source
         */
        virtual void stop() = 0;

        /**
         * Pause reading from the source
         */
        virtual void pause() = 0;

        /**
         * Resume reading from the source
         */
        virtual void resume() = 0;

        /**
         * Disconnect from the source
         */
        virtual void disconnect() = 0;

        // ============ Track Management (NuPlayer pattern) ============

        /**
         * Get number of tracks
         * @return Number of tracks in the media
         */
        virtual size_t getTrackCount() const = 0;

        /**
         * Get track information
         * @param trackIndex Index of the track
         * @param info Output track info structure
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult getTrackInfo(size_t trackIndex, VETrackInfo* info) const = 0;

        /**
         * Get selected track index for a given track type
         * @param type Track type (video, audio, etc.)
         * @return Track index, or -1 if none selected
         */
        virtual ssize_t getSelectedTrack(VETrackInfo::TrackType type) const = 0;

        /**
         * Select a track
         * @param trackIndex Index of the track to select
         * @param select true to select, false to deselect
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult selectTrack(size_t trackIndex, bool select) = 0;

        // ============ Format and Duration ============

        /**
         * Get format metadata for a track (similar to NuPlayer's getFormat)
         * @param audio true for audio format, false for video format
         * @param format Output message containing format info
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult getFormat(bool audio, std::shared_ptr<AMessage>* format) = 0;

        /**
         * Get media information about the source
         * @return Media info structure
         */
        virtual std::shared_ptr<VEMediaInfo> getMediaInfo() = 0;

        /**
         * Get duration of the media in microseconds (matching NuPlayer)
         * @return Duration in microseconds, or -1 if unknown
         */
        virtual int64_t getDurationUs() = 0;

        /**
         * Get source capability flags
         * @return Combination of Flags
         */
        virtual uint32_t getFlags() = 0;

        // ============ Data Access (NuPlayer pattern) ============

        /**
         * Dequeue an access unit (packet) from the source
         * Following NuPlayer's dequeueAccessUnit pattern
         * @param audio true for audio packet, false for video
         * @param accessUnit Output packet
         * @return VE_OK on success, VE_NOT_ENOUGH_DATA if no data available, VE_EOS at end
         */
        virtual VEResult dequeueAccessUnit(bool audio, std::shared_ptr<VEPacket>* accessUnit) = 0;

        /**
         * Read a packet from the source (legacy interface, use dequeueAccessUnit)
         * @param isAudio true to read audio packet, false for video
         * @param packet Output packet
         * @return VE_OK on success, VE_NOT_ENOUGH_DATA if no data available, VE_EOS at end
         */
        virtual VEResult read(bool isAudio, std::shared_ptr<VEPacket>& packet) {
            return dequeueAccessUnit(isAudio, &packet);
        }

        // ============ Seek ============

        /**
         * Seek to the specified position
         * @param seekTimeUs Seek position in microseconds
         * @param mode Seek mode (see SeekMode enum)
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult seekTo(int64_t seekTimeUs, int32_t mode = SEEK_PREVIOUS_SYNC) = 0;

        // Seek modes (matching NuPlayer)
        enum SeekMode {
            SEEK_PREVIOUS_SYNC = 0,
            SEEK_NEXT_SYNC = 1,
            SEEK_CLOSEST_SYNC = 2,
            SEEK_CLOSEST = 3,
        };

        // ============ Buffering ============

        /**
         * Get buffered position in microseconds
         * @return Buffered position in microseconds
         */
        virtual int64_t getBufferedPositionUs() { return -1; }

        /**
         * Check if source is currently buffering
         * @return true if buffering
         */
        virtual bool isBuffering() { return false; }

        /**
         * Request more packets to be read
         * @param msg Notification message to post when data is available
         * @param type 1 for audio, 2 for video
         */
        virtual void requestMoreData(std::shared_ptr<AMessage> msg, int type) = 0;

        // ============ Notifications ============

        /**
         * Set notification target for source events
         * @param notify Message to post for source notifications
         */
        virtual void setNotify(std::shared_ptr<AMessage> notify) {
            mNotify = notify;
        }

        // Notification event types (matching NuPlayer)
        enum NotifyType {
            kWhatPrepared           = 'prep',
            kWhatFlagsChanged       = 'flag',
            kWhatVideoSizeChanged   = 'vsiz',
            kWhatBufferingUpdate    = 'buff',
            kWhatPauseOnBufferingStart = 'paus',
            kWhatResumeOnBufferingEnd = 'rsme',
            kWhatCacheStats         = 'cach',
            kWhatSubtitleData       = 'sbtD',
            kWhatTimedTextData      = 'ttxD',
            kWhatTimedMetaData      = 'tmtD',
            kWhatQueueDecoderShutdown = 'qDcS',
            kWhatDrmNoLicense       = 'drmN',
            kWhatInstantiateSecureDecoders = 'isec',
            kWhatError              = 'erro',
            kWhatEOS                = 'eos ',
            kWhatBufferingStart     = 'bufS',
            kWhatBufferingEnd       = 'bufE',
            kWhatSeekDone           = 'sekD',
        };

    protected:
        /**
         * Post notification to the listener
         * @param what Event type
         */
        void notifyListener(int what) {
            if (mNotify != nullptr) {
                std::shared_ptr<AMessage> msg = mNotify->dup();
                msg->setInt32("what", what);
                msg->post();
            }
        }

        /**
         * Post notification with extra parameters
         * @param what Event type
         * @param extra Extra message to merge
         */
        void notifyListener(int what, const std::shared_ptr<AMessage>& extra) {
            if (mNotify != nullptr) {
                std::shared_ptr<AMessage> msg = mNotify->dup();
                msg->setInt32("what", what);
                if (extra) {
                    // Merge extra parameters if needed
                }
                msg->post();
            }
        }

        std::shared_ptr<AMessage> mNotify = nullptr;
    };

} // namespace VE

#endif // LZPLAYER_VESOURCE_H
