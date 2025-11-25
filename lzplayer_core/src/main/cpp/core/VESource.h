//
// Created for lzplayer demux redesign following NuPlayerSource pattern
//

#ifndef LZPLAYER_VESOURCE_H
#define LZPLAYER_VESOURCE_H

#include <string>
#include <memory>
#include "VEMediaDef.h"
#include "VEPacket.h"
#include "VEPacketQueue.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"
#include "VEError.h"

namespace VE {

    /**
     * Abstract base class for media sources (following NuPlayerSource pattern)
     * Provides interface for different types of media sources:
     * - Local file source
     * - Network streaming source (future)
     * - RTSP source (future)
     * - etc.
     */
    class VESource : public AHandler {
    public:
        // Source notification events
        enum Flags {
            FLAG_CAN_PAUSE          = 1,
            FLAG_CAN_SEEK_BACKWARD  = 2,
            FLAG_CAN_SEEK_FORWARD   = 4,
            FLAG_CAN_SEEK           = 8,
            FLAG_DYNAMIC_DURATION   = 16,
            FLAG_SECURE             = 32,
        };

        // Source status
        enum Status {
            STATUS_OK = 0,
            STATUS_ERROR = -1,
            STATUS_EOS = -2,
            STATUS_NOT_READY = -3,
        };

        VESource() = default;
        virtual ~VESource() = default;

        /**
         * Prepare the source with the given data source path/URI
         * @param path File path or URI of the media source
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult prepareAsync(const std::string& path) = 0;

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
         * Read a packet from the source
         * @param isAudio true to read audio packet, false for video
         * @param packet Output packet
         * @return VE_OK on success, VE_NOT_ENOUGH_DATA if no data available, VE_EOS at end
         */
        virtual VEResult read(bool isAudio, std::shared_ptr<VEPacket>& packet) = 0;

        /**
         * Seek to the specified position
         * @param posMs Position in milliseconds
         * @return VE_OK on success, error code otherwise
         */
        virtual VEResult seekTo(int64_t posMs) = 0;

        /**
         * Close the source and release resources
         * @return VE_OK on success
         */
        virtual VEResult close() = 0;

        /**
         * Get media information about the source
         * @return Media info structure
         */
        virtual std::shared_ptr<VEMediaInfo> getMediaInfo() = 0;

        /**
         * Get duration of the media in milliseconds
         * @return Duration in milliseconds, or -1 if unknown
         */
        virtual int64_t getDuration() = 0;

        /**
         * Get source capability flags
         * @return Combination of Flags
         */
        virtual uint32_t getFlags() = 0;

        /**
         * Request more packets to be read
         * @param msg Notification message to post when data is available
         * @param type 1 for audio, 0 for video
         */
        virtual void requestMoreData(std::shared_ptr<AMessage> msg, int type) = 0;

        /**
         * Set notification target for source events
         * @param notify Message to post for source notifications
         */
        virtual void setNotify(std::shared_ptr<AMessage> notify) {
            mNotify = notify;
        }

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

        std::shared_ptr<AMessage> mNotify = nullptr;

    public:
        // Notification event types
        enum {
            kWhatPrepared       = 'prep',
            kWhatError          = 'erro',
            kWhatEOS            = 'eos ',
            kWhatBufferingStart = 'bufS',
            kWhatBufferingEnd   = 'bufE',
            kWhatSeekDone       = 'sekD',
        };
    };

} // namespace VE

#endif // LZPLAYER_VESOURCE_H
