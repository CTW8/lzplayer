//
// Created for lzplayer demux redesign following NuPlayerSource pattern
// VEBuffer: Common data structure for passing media data (similar to NuPlayer's ABuffer)
//

#ifndef LZPLAYER_VEBUFFER_H
#define LZPLAYER_VEBUFFER_H

#include <memory>
#include <cstdint>
#include <cstring>
#include "thread/AMessage.h"
#include "VEDef.h"
#include "Log.h"

namespace VE {

    /**
     * VEBuffer - A unified buffer class for media data transfer
     * Following NuPlayer's ABuffer design pattern for passing media data
     * between components (source -> decoder -> renderer)
     */
    class VEBuffer {
    public:
        // Buffer type enumeration
        enum BufferType {
            BUFFER_TYPE_UNKNOWN = 0,
            BUFFER_TYPE_AUDIO_PACKET,
            BUFFER_TYPE_VIDEO_PACKET,
            BUFFER_TYPE_AUDIO_FRAME,
            BUFFER_TYPE_VIDEO_FRAME,
            BUFFER_TYPE_EOS,
        };

        // Buffer flags
        enum Flags {
            FLAG_NONE           = 0,
            FLAG_EOS            = 1 << 0,  // End of stream
            FLAG_CODECCONFIG    = 1 << 1,  // Codec config data (SPS/PPS)
            FLAG_SYNCFRAME      = 1 << 2,  // Key frame / sync frame
            FLAG_ENCRYPTED      = 1 << 3,  // Encrypted data
        };

        // Create buffer with specified capacity
        explicit VEBuffer(size_t capacity = 0)
            : mData(nullptr),
              mCapacity(capacity),
              mSize(0),
              mOffset(0),
              mBufferType(BUFFER_TYPE_UNKNOWN),
              mFlags(FLAG_NONE),
              mPts(0),
              mDts(0),
              mDuration(0) {
            if (capacity > 0) {
                mData = new uint8_t[capacity];
            }
        }

        // Create buffer with data copy
        VEBuffer(const void* data, size_t size)
            : mData(nullptr),
              mCapacity(size),
              mSize(size),
              mOffset(0),
              mBufferType(BUFFER_TYPE_UNKNOWN),
              mFlags(FLAG_NONE),
              mPts(0),
              mDts(0),
              mDuration(0) {
            if (size > 0) {
                mData = new uint8_t[size];
                memcpy(mData, data, size);
            }
        }

        ~VEBuffer() {
            if (mData) {
                delete[] mData;
                mData = nullptr;
            }
        }

        // Non-copyable
        VEBuffer(const VEBuffer&) = delete;
        VEBuffer& operator=(const VEBuffer&) = delete;

        // Data access methods
        uint8_t* base() { return mData; }
        const uint8_t* base() const { return mData; }

        uint8_t* data() { return mData + mOffset; }
        const uint8_t* data() const { return mData + mOffset; }

        size_t capacity() const { return mCapacity; }
        size_t size() const { return mSize; }
        size_t offset() const { return mOffset; }

        void setRange(size_t offset, size_t size) {
            if (offset + size <= mCapacity) {
                mOffset = offset;
                mSize = size;
            }
        }

        void setSize(size_t size) {
            if (size <= mCapacity) {
                mSize = size;
            }
        }

        // Buffer type
        BufferType bufferType() const { return mBufferType; }
        void setBufferType(BufferType type) { mBufferType = type; }

        // Flags
        uint32_t flags() const { return mFlags; }
        void setFlags(uint32_t flags) { mFlags = flags; }

        bool isEOS() const { return (mFlags & FLAG_EOS) != 0; }
        void setEOS() { mFlags |= FLAG_EOS; }

        bool isSyncFrame() const { return (mFlags & FLAG_SYNCFRAME) != 0; }
        void setSyncFrame() { mFlags |= FLAG_SYNCFRAME; }

        // Timing information
        int64_t pts() const { return mPts; }
        void setPts(int64_t pts) { mPts = pts; }

        int64_t dts() const { return mDts; }
        void setDts(int64_t dts) { mDts = dts; }

        int64_t duration() const { return mDuration; }
        void setDuration(int64_t duration) { mDuration = duration; }

        // Metadata (AMessage-based, following NuPlayer pattern)
        std::shared_ptr<AMessage> meta() const { return mMeta; }
        void setMeta(std::shared_ptr<AMessage> meta) { mMeta = meta; }

        // Clone the buffer
        std::shared_ptr<VEBuffer> clone() const {
            auto cloned = std::make_shared<VEBuffer>(mData + mOffset, mSize);
            cloned->setBufferType(mBufferType);
            cloned->setFlags(mFlags);
            cloned->setPts(mPts);
            cloned->setDts(mDts);
            cloned->setDuration(mDuration);
            if (mMeta) {
                cloned->setMeta(mMeta->dup());
            }
            return cloned;
        }

    private:
        uint8_t* mData;
        size_t mCapacity;
        size_t mSize;
        size_t mOffset;
        
        BufferType mBufferType;
        uint32_t mFlags;
        
        int64_t mPts;
        int64_t mDts;
        int64_t mDuration;
        
        std::shared_ptr<AMessage> mMeta;
    };

    /**
     * VEAccessUnit - Wrapper for access unit data
     * Following NuPlayer's access unit concept for passing decoded frames
     */
    class VEAccessUnit {
    public:
        VEAccessUnit()
            : mType(VEBuffer::BUFFER_TYPE_UNKNOWN),
              mPts(0),
              mDts(0),
              mIsEOS(false) {
        }

        explicit VEAccessUnit(std::shared_ptr<VEBuffer> buffer)
            : mBuffer(buffer),
              mType(buffer ? buffer->bufferType() : VEBuffer::BUFFER_TYPE_UNKNOWN),
              mPts(buffer ? buffer->pts() : 0),
              mDts(buffer ? buffer->dts() : 0),
              mIsEOS(buffer ? buffer->isEOS() : false) {
        }

        std::shared_ptr<VEBuffer> buffer() const { return mBuffer; }
        void setBuffer(std::shared_ptr<VEBuffer> buffer) { 
            mBuffer = buffer;
            if (buffer) {
                mType = buffer->bufferType();
                mPts = buffer->pts();
                mDts = buffer->dts();
                mIsEOS = buffer->isEOS();
            }
        }

        VEBuffer::BufferType type() const { return mType; }
        void setType(VEBuffer::BufferType type) { mType = type; }

        int64_t pts() const { return mPts; }
        void setPts(int64_t pts) { mPts = pts; }

        int64_t dts() const { return mDts; }
        void setDts(int64_t dts) { mDts = dts; }

        bool isEOS() const { return mIsEOS; }
        void setEOS(bool eos) { mIsEOS = eos; }

        bool isValid() const { return mBuffer != nullptr; }

        // Metadata
        std::shared_ptr<AMessage> meta() const { return mMeta; }
        void setMeta(std::shared_ptr<AMessage> meta) { mMeta = meta; }

    private:
        std::shared_ptr<VEBuffer> mBuffer;
        VEBuffer::BufferType mType;
        int64_t mPts;
        int64_t mDts;
        bool mIsEOS;
        std::shared_ptr<AMessage> mMeta;
    };

} // namespace VE

#endif // LZPLAYER_VEBUFFER_H
