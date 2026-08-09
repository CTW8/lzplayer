#include "VEAAudioRender.h"

#include <algorithm>
#include <cstring>
#include <dlfcn.h>

#include "utils/Log.h"
#include "VEDef.h"

extern "C" {
#include "libavutil/samplefmt.h"
}

namespace VE {
    namespace {
        /// 环形缓冲时长。够吸收调度抖动，又不至于让 seek 后残留太多旧数据。
        constexpr int kRingDurationMs = 200;

        /// libaaudio 只在 API 26+ 存在，而本工程 minSdk 是 24。直接链接会让它
        /// 变成 DT_NEEDED 硬依赖，24/25 机型上整个 so 都加载不了——所以这里
        /// 运行期 dlopen + dlsym，取不到就让工厂退回 OpenSL ES。
        struct AAudioApi {
            void *handle = nullptr;
            bool ok = false;

            aaudio_result_t (*createStreamBuilder)(AAudioStreamBuilder **) = nullptr;
            void (*builderSetFormat)(AAudioStreamBuilder *, aaudio_format_t) = nullptr;
            void (*builderSetChannelCount)(AAudioStreamBuilder *, int32_t) = nullptr;
            void (*builderSetSampleRate)(AAudioStreamBuilder *, int32_t) = nullptr;
            void (*builderSetDirection)(AAudioStreamBuilder *, aaudio_direction_t) = nullptr;
            void (*builderSetSharingMode)(AAudioStreamBuilder *, aaudio_sharing_mode_t) = nullptr;
            void (*builderSetPerformanceMode)(AAudioStreamBuilder *,
                                              aaudio_performance_mode_t) = nullptr;
            void (*builderSetDataCallback)(AAudioStreamBuilder *, AAudioStream_dataCallback,
                                           void *) = nullptr;
            void (*builderSetErrorCallback)(AAudioStreamBuilder *, AAudioStream_errorCallback,
                                            void *) = nullptr;
            aaudio_result_t (*builderOpenStream)(AAudioStreamBuilder *,
                                                 AAudioStream **) = nullptr;
            aaudio_result_t (*builderDelete)(AAudioStreamBuilder *) = nullptr;
            aaudio_result_t (*streamRequestStart)(AAudioStream *) = nullptr;
            aaudio_result_t (*streamRequestPause)(AAudioStream *) = nullptr;
            aaudio_result_t (*streamRequestFlush)(AAudioStream *) = nullptr;
            aaudio_result_t (*streamRequestStop)(AAudioStream *) = nullptr;
            aaudio_result_t (*streamClose)(AAudioStream *) = nullptr;
            aaudio_result_t (*streamGetTimestamp)(AAudioStream *, clockid_t, int64_t *,
                                                  int64_t *) = nullptr;
            int64_t (*streamGetFramesWritten)(AAudioStream *) = nullptr;
            /// AAudio 自带的欠载计数，这张表里原先漏了。
            /// **刻意不加入下面的"全部指针有效"校验**：它是可选能力，
            /// 老设备上取不到只该让这一个指标不可用，不该把整个 AAudio
            /// 后端判为不可用而退回 OpenSL ES。
            int32_t (*streamGetXRunCount)(AAudioStream *) = nullptr;
            const char *(*resultToText)(aaudio_result_t) = nullptr;

            AAudioApi() {
                handle = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
                if (handle == nullptr) {
                    ALOGI("AAudio unavailable on this device, will use OpenSL ES");
                    return;
                }
                auto sym = [this](const char *name) { return dlsym(handle, name); };
                createStreamBuilder = reinterpret_cast<decltype(createStreamBuilder)>(
                        sym("AAudio_createStreamBuilder"));
                builderSetFormat = reinterpret_cast<decltype(builderSetFormat)>(
                        sym("AAudioStreamBuilder_setFormat"));
                builderSetChannelCount = reinterpret_cast<decltype(builderSetChannelCount)>(
                        sym("AAudioStreamBuilder_setChannelCount"));
                builderSetSampleRate = reinterpret_cast<decltype(builderSetSampleRate)>(
                        sym("AAudioStreamBuilder_setSampleRate"));
                builderSetDirection = reinterpret_cast<decltype(builderSetDirection)>(
                        sym("AAudioStreamBuilder_setDirection"));
                builderSetSharingMode = reinterpret_cast<decltype(builderSetSharingMode)>(
                        sym("AAudioStreamBuilder_setSharingMode"));
                builderSetPerformanceMode = reinterpret_cast<decltype(builderSetPerformanceMode)>(
                        sym("AAudioStreamBuilder_setPerformanceMode"));
                builderSetDataCallback = reinterpret_cast<decltype(builderSetDataCallback)>(
                        sym("AAudioStreamBuilder_setDataCallback"));
                builderSetErrorCallback = reinterpret_cast<decltype(builderSetErrorCallback)>(
                        sym("AAudioStreamBuilder_setErrorCallback"));
                builderOpenStream = reinterpret_cast<decltype(builderOpenStream)>(
                        sym("AAudioStreamBuilder_openStream"));
                builderDelete = reinterpret_cast<decltype(builderDelete)>(
                        sym("AAudioStreamBuilder_delete"));
                streamRequestStart = reinterpret_cast<decltype(streamRequestStart)>(
                        sym("AAudioStream_requestStart"));
                streamRequestPause = reinterpret_cast<decltype(streamRequestPause)>(
                        sym("AAudioStream_requestPause"));
                streamRequestFlush = reinterpret_cast<decltype(streamRequestFlush)>(
                        sym("AAudioStream_requestFlush"));
                streamRequestStop = reinterpret_cast<decltype(streamRequestStop)>(
                        sym("AAudioStream_requestStop"));
                streamClose = reinterpret_cast<decltype(streamClose)>(
                        sym("AAudioStream_close"));
                streamGetTimestamp = reinterpret_cast<decltype(streamGetTimestamp)>(
                        sym("AAudioStream_getTimestamp"));
                streamGetFramesWritten = reinterpret_cast<decltype(streamGetFramesWritten)>(
                        sym("AAudioStream_getFramesWritten"));
                streamGetXRunCount = reinterpret_cast<decltype(streamGetXRunCount)>(
                        sym("AAudioStream_getXRunCount"));
                resultToText = reinterpret_cast<decltype(resultToText)>(
                        sym("AAudio_convertResultToText"));

                ok = createStreamBuilder && builderSetFormat && builderSetChannelCount &&
                     builderSetSampleRate && builderSetDirection && builderSetSharingMode &&
                     builderSetPerformanceMode && builderSetDataCallback &&
                     builderSetErrorCallback && builderOpenStream && builderDelete &&
                     streamRequestStart && streamRequestPause && streamRequestFlush &&
                     streamRequestStop && streamClose && streamGetTimestamp &&
                     streamGetFramesWritten;
                ALOGI("AAudio symbols %s", ok ? "loaded" : "incomplete");
            }
        };

        const AAudioApi &api() {
            static AAudioApi sApi;   // 进程内只 dlopen 一次
            return sApi;
        }

        const char *resultText(aaudio_result_t r) {
            return api().resultToText ? api().resultToText(r) : "?";
        }
    }

    bool VEAAudioRender::isAvailable() {
        return api().ok;
    }

    VEAAudioRender::VEAAudioRender() = default;

    VEAAudioRender::~VEAAudioRender() {
        release();
    }

    VEResult VEAAudioRender::configure(const AudioConfig &config) {
        if (mConfigured) {
            release();
        }
        mConfig = config;
        mBytesPerFrame = config.channels *
                         av_get_bytes_per_sample(static_cast<AVSampleFormat>(config.sampleFormat));
        if (mBytesPerFrame <= 0) {
            mBytesPerFrame = config.channels * 2;   // S16 兜底
        }

        AAudioStreamBuilder *builder = nullptr;
        if (!api().ok || api().createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
            ALOGE("VEAAudioRender::%s createStreamBuilder failed", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        api().builderSetFormat(builder, AAUDIO_FORMAT_PCM_I16);
        api().builderSetChannelCount(builder, config.channels);
        api().builderSetSampleRate(builder, config.sampleRate);
        api().builderSetDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        api().builderSetSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        // 低延迟模式：直通快速混音路径，功耗与延迟都优于普通路径
        api().builderSetPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        api().builderSetDataCallback(builder, &VEAAudioRender::dataCallback, this);
        api().builderSetErrorCallback(builder, &VEAAudioRender::errorCallback, this);

        const aaudio_result_t result = api().builderOpenStream(builder, &mStream);
        api().builderDelete(builder);
        if (result != AAUDIO_OK || mStream == nullptr) {
            ALOGE("VEAAudioRender::%s openStream failed: %s", __FUNCTION__,
                  resultText(result));
            return VE_UNKNOWN_ERROR;
        }

        const size_t ringBytes = static_cast<size_t>(config.sampleRate) *
                                 kRingDurationMs / 1000 * mBytesPerFrame;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mRing.assign(ringBytes, 0);
            mReadPos = mWritePos = 0;
            mFull = false;
        }

        mConfigured = true;
        ALOGI("VEAAudioRender::%s ready %dHz %dch, ring %zu bytes", __FUNCTION__,
              config.sampleRate, config.channels, ringBytes);
        return VE_OK;
    }

    aaudio_data_callback_result_t VEAAudioRender::dataCallback(
            AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {
        (void) stream;
        auto *self = static_cast<VEAAudioRender *>(userData);
        auto *out = static_cast<uint8_t *>(audioData);
        const size_t wanted = static_cast<size_t>(numFrames) * self->mBytesPerFrame;

        size_t copied = 0;
        {
            std::lock_guard<std::mutex> lk(self->mMutex);
            const size_t avail = self->readableLocked();
            copied = std::min(wanted, avail);
            const size_t first = std::min(copied, self->mRing.size() - self->mReadPos);
            memcpy(out, self->mRing.data() + self->mReadPos, first);
            if (copied > first) {
                memcpy(out + first, self->mRing.data(), copied - first);
            }
            self->mReadPos = (self->mReadPos + copied) % self->mRing.size();
            if (copied > 0) {
                self->mFull = false;
            }
        }
        // 欠数据就补静音：AAudio 的回调不能不填满，否则会出爆音
        if (copied < wanted) {
            memset(out + copied, 0, wanted - copied);
        }
        // 回调线程只做通知，真正的取帧在渲染 looper 上进行
        if (self->mConfig.onCallback) {
            self->mConfig.onCallback();
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    void VEAAudioRender::errorCallback(AAudioStream *stream, void *userData,
                                       aaudio_result_t error) {
        (void) stream;
        (void) userData;
        // 设备断开(拔耳机等)：上层会因为喂不进数据而察觉，这里只记录
        ALOGE("VEAAudioRender error callback: %s", resultText(error));
    }

    size_t VEAAudioRender::readableLocked() const {
        if (mFull) {
            return mRing.size();
        }
        if (mWritePos >= mReadPos) {
            return mWritePos - mReadPos;
        }
        return mRing.size() - mReadPos + mWritePos;
    }

    VEResult VEAAudioRender::renderFrame(std::shared_ptr<VEFrame> frame) {
        if (!mConfigured || mStream == nullptr) {
            return VE_UNKNOWN_ERROR;
        }
        if (!frame || frame->getFrame() == nullptr || frame->getFrame()->data[0] == nullptr) {
            return VE_UNKNOWN_ERROR;
        }
        AVFrame *av = frame->getFrame();
        const size_t bytes = static_cast<size_t>(av->nb_samples) * mBytesPerFrame;

        std::lock_guard<std::mutex> lk(mMutex);
        const size_t freeBytes = mRing.size() - readableLocked();
        if (bytes > freeBytes) {
            // 环形缓冲满：如实上报，调用方留住这一帧稍后重试。
            // 与 SLES 路径同样的语义，上层无需区分后端。
            return VE_WOULD_BLOCK;
        }
        const size_t first = std::min(bytes, mRing.size() - mWritePos);
        memcpy(mRing.data() + mWritePos, av->data[0], first);
        if (bytes > first) {
            memcpy(mRing.data(), av->data[0] + first, bytes - first);
        }
        mWritePos = (mWritePos + bytes) % mRing.size();
        if (mWritePos == mReadPos) {
            mFull = true;
        }
        return VE_OK;
    }

    int64_t VEAAudioRender::getUnderrunCount() {
        // 返回 -1 而不是 0："拿不到这个数"与"没有欠载"是两件事，
        // 混成 0 会让人以为音频一切正常
        if (api().streamGetXRunCount == nullptr || mStream == nullptr) {
            return -1;
        }
        return api().streamGetXRunCount(mStream);
    }

    int64_t VEAAudioRender::getQueuedDurationUs() {
        if (mStream == nullptr) {
            return 0;
        }
        size_t ringBytes;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            ringBytes = readableLocked();
        }
        const int64_t ringUs = (mConfig.sampleRate > 0 && mBytesPerFrame > 0)
                ? static_cast<int64_t>(ringBytes / mBytesPerFrame) * 1000000 / mConfig.sampleRate
                : 0;

        // 设备侧真实未播出量：已写入帧数 - 已呈现帧数。
        // 这是 AAudio 相对 SLES 的关键优势——不用再估。
        int64_t deviceUs = 0;
        int64_t framePosition = 0;
        int64_t timeNanos = 0;
        if (api().streamGetTimestamp(mStream, CLOCK_MONOTONIC,
                                      &framePosition, &timeNanos) == AAUDIO_OK) {
            const int64_t written = api().streamGetFramesWritten(mStream);
            const int64_t pending = written - framePosition;
            if (pending > 0 && mConfig.sampleRate > 0) {
                deviceUs = pending * 1000000 / mConfig.sampleRate;
            }
        }
        return ringUs + deviceUs;
    }

    VEResult VEAAudioRender::start() {
        if (mStream == nullptr) {
            return VE_UNKNOWN_ERROR;
        }
        if (mPlaying) {
            return VE_OK;   // 幂等
        }
        const aaudio_result_t r = api().streamRequestStart(mStream);
        if (r != AAUDIO_OK) {
            ALOGE("VEAAudioRender::%s requestStart failed: %s", __FUNCTION__,
                  resultText(r));
            return VE_UNKNOWN_ERROR;
        }
        mPlaying = true;
        return VE_OK;
    }

    VEResult VEAAudioRender::pause() {
        if (mStream == nullptr || !mPlaying) {
            return VE_OK;
        }
        api().streamRequestPause(mStream);
        mPlaying = false;
        return VE_OK;
    }

    VEResult VEAAudioRender::flush() {
        // 先停流再清缓冲：回调可能正在读环形缓冲
        if (mStream) {
            api().streamRequestPause(mStream);
            api().streamRequestFlush(mStream);
        }
        std::lock_guard<std::mutex> lk(mMutex);
        mReadPos = mWritePos = 0;
        mFull = false;
        return VE_OK;
    }

    VEResult VEAAudioRender::stop() {
        if (mStream) {
            api().streamRequestStop(mStream);
        }
        mPlaying = false;
        std::lock_guard<std::mutex> lk(mMutex);
        mReadPos = mWritePos = 0;
        mFull = false;
        return VE_OK;
    }

    void VEAAudioRender::closeStream() {
        if (mStream) {
            api().streamRequestStop(mStream);
            api().streamClose(mStream);
            mStream = nullptr;
        }
    }

    VEResult VEAAudioRender::release() {
        closeStream();
        mPlaying = false;
        mConfigured = false;
        std::lock_guard<std::mutex> lk(mMutex);
        mRing.clear();
        mReadPos = mWritePos = 0;
        mFull = false;
        return VE_OK;
    }
}
