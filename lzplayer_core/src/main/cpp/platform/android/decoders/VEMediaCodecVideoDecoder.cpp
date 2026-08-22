#include "VEMediaCodecVideoDecoder.h"
#include "utils/VEFaultInject.h"
#include "VECodecWarmup.h"
#include "utils/VEPerfStats.h"

#include <cstring>
#include "utils/Log.h"
#include "VEDef.h"

namespace VE {
    namespace {
        /// 有活干时的工作循环间隔。同步 API 下这是轮询节奏——取得足够小
        /// 以免拖慢解码，又足够大以免空转烧 CPU。
        constexpr int64_t kWorkIntervalUs = 2000;
        /// 上游饥饿/输出未就绪时的退避间隔
        constexpr int64_t kIdleIntervalUs = 10000;
        /// 待上屏队列上限：MediaCodec 的 output buffer 本就有限，
        /// 这里只是防御性兜底
        constexpr size_t kMaxOutQueue = 16;

        const char *mimeForCodec(AVCodecID id) {
            switch (id) {
                case AV_CODEC_ID_H264: return "video/avc";
                case AV_CODEC_ID_HEVC: return "video/hevc";
                default:               return nullptr;
            }
        }
    }

    bool VEMediaCodecVideoDecoder::isSupported(const VETrackInfo &track) {
        // 首批白名单只放 H.264/H.265：这两个格式的硬解在 arm64 机型上
        // 覆盖率与稳定性都最好，其余交给软解
        return mimeForCodec(track.codecId) != nullptr &&
               track.width > 0 && track.height > 0;
    }

    VEMediaCodecVideoDecoder::VEMediaCodecVideoDecoder(
            std::shared_ptr<AMessage> &notify, const std::shared_ptr<VEAVsync> &avSync)
            : mNotify(notify), mAVSync(avSync) {
    }

    VEMediaCodecVideoDecoder::~VEMediaCodecVideoDecoder() {
        // 析构期间不能投消息(shared_from_this 已失效)，直接同步清理
        destroyCodec();
        destroyBitstreamFilter();
    }

    // ---------------------------------------------------------------------
    // 命令面（全部异步，回执经 notify）
    // ---------------------------------------------------------------------

    VEResult VEMediaCodecVideoDecoder::prepare(std::shared_ptr<IMediaSource> source,
                                               std::shared_ptr<IFrameSink> sink,
                                               const VEBundle &params) {
        (void) sink;   // 直出 Surface，不经帧链路
        if (!source) {
            return VE_INVALID_PARAMS;
        }
        auto msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("source", source);
        msg->setPointer("surface", params.get<ANativeWindow *>("surface", nullptr));
        msg->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::start() {
        std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::stop() {
        std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::pause() {
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::flush() {
        std::make_shared<AMessage>(kWhatFlush, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::seekTo(double timestampMs) {
        auto msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp", timestampMs);
        msg->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::release() {
        std::make_shared<AMessage>(kWhatRelease, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::setSurface(ANativeWindow *win) {
        auto msg = std::make_shared<AMessage>(kWhatSurface, shared_from_this());
        msg->setPointer("surface", win);
        msg->post();
        return VE_OK;
    }

    void VEMediaCodecVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        switch (msg->what()) {
            case kWhatInit:
                if (onPrepare(msg) != VE_OK) {
                    // 创建失败：上报带 fallback 标记的错误，播放器换软解重建
                    reportFatal("codec configure failed");
                }
                break;
            case kWhatStart:
                onStart();
                break;
            case kWhatPause:
                onPause();
                postEventBothRoles(VE_NOTIFY_EVENT_PAUSE_DONE);
                break;
            case kWhatStop:
                onStop();
                postEventBothRoles(VE_NOTIFY_EVENT_STOP_DONE);
                break;
            case kWhatFlush:
                onFlush();
                postEventBothRoles(VE_NOTIFY_EVENT_FLUSH_DONE);
                break;
            case kWhatSeek: {
                double ts = 0;
                msg->findDouble("timestamp", &ts);
                onSeek(ts);
                postEventBothRoles(VE_NOTIFY_EVENT_SEEK_DONE);
                break;
            }
            case kWhatRelease:
                onRelease();
                postEventBothRoles(VE_NOTIFY_EVENT_RELEASE_DONE);
                break;
            case kWhatSurface: {
                ANativeWindow *win = nullptr;
                msg->findPointer("surface", (void **) &win);
                onSurfaceChanged(win);
                break;
            }
            case kWhatDoWork: {
                int32_t epoch = 0;
                msg->findInt32("epoch", &epoch);
                if (epoch != mEpoch || !mIsStarted) {
                    break;   // flush/seek 之前投的，作废
                }
                onDoWork();
                break;
            }
            case kWhatRenderOut:
                onRenderOut(msg);
                break;
            default:
                break;
        }
    }

    // ---------------------------------------------------------------------
    // 建链
    // ---------------------------------------------------------------------

    VEResult VEMediaCodecVideoDecoder::onPrepare(const std::shared_ptr<AMessage> &msg) {
        std::shared_ptr<void> tmp;
        if (!msg->findObject("source", &tmp)) {
            return VE_INVALID_PARAMS;
        }
        mSource = std::static_pointer_cast<IMediaSource>(tmp);
        msg->findPointer("surface", (void **) &mWindow);

        auto info = mSource->getFileInfo();
        const VETrackInfo *track = info ? info->videoTrack() : nullptr;
        if (track == nullptr || track->codecParams == nullptr) {
            ALOGE("VEMediaCodecVideoDecoder::%s no video track", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }
        mTrack = *track;
        // 只借用参数，不接管所有权(codecParams 归 VEMediaInfo)
        mTrack.codecParams = track->codecParams;

        if (mWindow == nullptr) {
            // 硬解必须绑 Surface 才能零拷贝直出；没有窗口就没意义，
            // 让工厂/播放器退回软解
            ALOGE("VEMediaCodecVideoDecoder::%s no surface", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }

        VEResult ret = setupBitstreamFilter();
        if (ret != VE_OK) {
            return ret;
        }
        return configureCodec();
    }

    VEResult VEMediaCodecVideoDecoder::setupBitstreamFilter() {
        // MP4/MKV 里的 H.264/H.265 是长度前缀(AVCC/HVCC)，MediaCodec 要 Annex-B。
        // TS/FLV 本来就是 Annex-B，bsf 会自动透传，无需分支判断。
        const char *bsfName = (mTrack.codecId == AV_CODEC_ID_H264)
                              ? "h264_mp4toannexb" : "hevc_mp4toannexb";
        const AVBitStreamFilter *filter = av_bsf_get_by_name(bsfName);
        if (filter == nullptr) {
            ALOGE("VEMediaCodecVideoDecoder::%s bsf %s not found", __FUNCTION__, bsfName);
            return VE_UNKNOWN_ERROR;
        }
        if (av_bsf_alloc(filter, &mBsf) < 0) {
            return VE_NO_MEMORY;
        }
        if (avcodec_parameters_copy(mBsf->par_in, mTrack.codecParams) < 0) {
            destroyBitstreamFilter();
            return VE_UNKNOWN_ERROR;
        }
        mBsf->time_base_in = mTrack.timeBase;
        if (av_bsf_init(mBsf) < 0) {
            ALOGE("VEMediaCodecVideoDecoder::%s av_bsf_init failed", __FUNCTION__);
            destroyBitstreamFilter();
            return VE_UNKNOWN_ERROR;
        }
        return VE_OK;
    }

    void VEMediaCodecVideoDecoder::destroyBitstreamFilter() {
        if (mBsf) {
            av_bsf_free(&mBsf);
            mBsf = nullptr;
        }
    }

    VEResult VEMediaCodecVideoDecoder::configureCodec() {
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T4A_CONFIGURE_BEGIN);
        }
        const char *mime = mimeForCodec(mTrack.codecId);
        if (mime == nullptr) {
            return VE_INVALID_PARAMS;
        }

        // 优先用预热好的实例(见 VECodecWarmup)。取不到就照常自建——
        // 预热是"赶上就赶上"的优化，不能成为正确性依赖
        mCodec = VECodecWarmup::take(mime);
        if (mCodec == nullptr) {
            mCodec = AMediaCodec_createDecoderByType(mime);
        }
        // 注入点放在**两条路径汇合之后**。第一版放在 createDecoderByType 那一
        // 支里, 而预热实例命中时走的是 take() 分支 —— 注入设置生效了、注入点
        // 却从不执行, 实测 "VEFAULT inject" 一次都没打印。
        // 凡有多条路径得到同一个资源的地方, 注入点必须在汇合处。
        if (VE_FAULT_HW_CREATE()) {
            ALOGW("VEFAULT inject: hw create failure (codec=%p)", (void *) mCodec);
            if (mCodec != nullptr) {
                AMediaCodec_delete(mCodec);
            }
            mCodec = nullptr;
        }
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T4A_CODEC_CREATED);
        }
        if (mCodec == nullptr) {
            ALOGE("VEMediaCodecVideoDecoder::%s createDecoderByType(%s) failed",
                  __FUNCTION__, mime);
            return VE_UNKNOWN_ERROR;
        }

        AMediaFormat *format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, mTrack.width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, mTrack.height);
        // 交给系统合成器做旋转，省掉我们自己转纹理
        if (mTrack.rotationDegrees != 0) {
            AMediaFormat_setInt32(format, "rotation-degrees", mTrack.rotationDegrees);
        }

        // csd：bsf 初始化后 par_out->extradata 已经是 Annex-B 的参数集，
        // 直接整段作为 csd-0 交给 codec(H.264 的 SPS+PPS / H.265 的 VPS+SPS+PPS)
        if (mBsf && mBsf->par_out->extradata && mBsf->par_out->extradata_size > 0) {
            AMediaFormat_setBuffer(format, "csd-0",
                                   mBsf->par_out->extradata,
                                   static_cast<size_t>(mBsf->par_out->extradata_size));
        }

        media_status_t st = AMediaCodec_configure(mCodec, format, mWindow, nullptr, 0);
        if (VE_FAULT_HW_CONFIGURE()) {
            // 注入配置期失败: 与建链期失败走的是不同分支
            ALOGW("VEFAULT inject: hw configure failure");
            st = AMEDIA_ERROR_UNSUPPORTED;
        }
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T4A_CODEC_CONFIGURED);
        }
        AMediaFormat_delete(format);
        if (st != AMEDIA_OK) {
            ALOGE("VEMediaCodecVideoDecoder::%s configure failed: %d", __FUNCTION__, st);
            destroyCodec();
            return VE_UNKNOWN_ERROR;
        }
        st = AMediaCodec_start(mCodec);
        if (st != AMEDIA_OK) {
            ALOGE("VEMediaCodecVideoDecoder::%s start failed: %d", __FUNCTION__, st);
            destroyCodec();
            return VE_UNKNOWN_ERROR;
        }

        mCodecReady = true;
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T4A_CONFIGURE_END);
        }
        ALOGI("VEMediaCodecVideoDecoder::%s ready: %s %dx%d rot=%d", __FUNCTION__,
              mime, mTrack.width, mTrack.height, mTrack.rotationDegrees);
        return VE_OK;
    }

    void VEMediaCodecVideoDecoder::destroyCodec() {
        if (mCodec) {
            AMediaCodec_stop(mCodec);
            AMediaCodec_delete(mCodec);
            mCodec = nullptr;
        }
        mCodecReady = false;
    }

    // ---------------------------------------------------------------------
    // 生命周期
    // ---------------------------------------------------------------------

    VEResult VEMediaCodecVideoDecoder::onStart() {
        if (mIsStarted) {
            return VE_OK;
        }
        if (!mCodecReady) {
            ALOGW("VEMediaCodecVideoDecoder::%s codec not ready", __FUNCTION__);
            return VE_INVALID_OPERATION;
        }
        mIsStarted = true;
        mInputEOS = false;
        mOutputEOS = false;
        postDoWork(0);
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::onPause() {
        mIsStarted = false;
        ++mEpoch;
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::onStop() {
        mIsStarted = false;
        ++mEpoch;
        mSeekTargetUs = kNoSeekTarget;
        mPendingPacket.reset();
        mInputEOS = false;
        mOutputEOS = false;
        if (mCodecReady && mCodec) {
            // 丢掉未上屏的 buffer，再 flush codec 内部队列
            for (const auto &out : mOutQueue) {
                AMediaCodec_releaseOutputBuffer(mCodec, out.index, false);
            }
            AMediaCodec_flush(mCodec);
        }
        mOutQueue.clear();
        mRenderPending = false;
        // 被 flush 掉的包不会再有对应输出，留着会让表撑满并让后续样本
        // 配到错误的入队时刻
        mFeedTimeUs.clear();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::onFlush() {
        onStop();
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::onSeek(double timestampMs) {
        onFlush();
        // demux 只能定位到关键帧，目标之前的帧解出来也不上屏——
        // 这是硬解侧的精准 seek(等价于软解的 decode-and-drop)
        mSeekTargetUs = static_cast<int64_t>(timestampMs * 1000);
        mNotifyFirstFrame = true;
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::onRelease() {
        onStop();
        destroyCodec();
        destroyBitstreamFilter();
        mSource.reset();
        mWindow = nullptr;
        return VE_OK;
    }

    VEResult VEMediaCodecVideoDecoder::onSurfaceChanged(ANativeWindow *win) {
        mWindow = win;
        if (!mCodecReady || mCodec == nullptr) {
            return VE_OK;
        }
        if (win == nullptr) {
            // surface 没了：继续解码但输出一律丢弃，绝不画向失效窗口
            mSurfaceLost = true;
            return VE_OK;
        }
        // API 23+ 支持换绑输出 surface，不必重建 codec
        const media_status_t st = AMediaCodec_setOutputSurface(mCodec, win);
        if (st != AMEDIA_OK) {
            ALOGE("VEMediaCodecVideoDecoder::%s setOutputSurface failed: %d",
                  __FUNCTION__, st);
            reportFatal("setOutputSurface failed");
            return VE_UNKNOWN_ERROR;
        }
        mSurfaceLost = false;
        if (mIsStarted) {
            postDoWork(0);
        }
        return VE_OK;
    }

    // ---------------------------------------------------------------------
    // 工作循环
    // ---------------------------------------------------------------------

    void VEMediaCodecVideoDecoder::postDoWork(int64_t delayUs) {
        auto msg = std::make_shared<AMessage>(kWhatDoWork, shared_from_this());
        msg->setInt32("epoch", mEpoch);
        msg->post(delayUs);
    }

    void VEMediaCodecVideoDecoder::onDoWork() {
        if (!mCodecReady) {
            return;
        }
        bool progressed = false;
        // 一轮里多喂几次，摊薄轮询开销
        for (int i = 0; i < 4 && feedInput(); ++i) {
            progressed = true;
        }
        for (int i = 0; i < 4 && drainOutput(); ++i) {
            progressed = true;
        }
        scheduleRender();

        if (mOutputEOS) {
            return;   // 链条终结，不再自驱
        }
        // 有进展就快转，没进展就退避——同步 API 下这是唯一的节奏来源
        postDoWork(progressed ? kWorkIntervalUs : kIdleIntervalUs);
    }

    bool VEMediaCodecVideoDecoder::feedInput() {
        if (mInputEOS || mSource == nullptr) {
            return false;
        }
        // 先备好包再要输入缓冲：反过来的话拿到 index 却没数据可填，
        // 这个 index 就被白占住了
        if (mPendingPacket == nullptr) {
            std::shared_ptr<VEPacket> packet;
            const VEResult ret = mSource->read(ETrackType::VIDEO, packet);
            if (ret == VE_NOT_ENOUGH_DATA || packet == nullptr) {
                return false;   // 上游饥饿，下一轮再来
            }
            mPendingPacket = packet;
        }

        const ssize_t index = AMediaCodec_dequeueInputBuffer(mCodec, 0);
        if (index < 0) {
            return false;   // codec 输入满，包留到下一轮
        }

        size_t capacity = 0;
        uint8_t *buf = AMediaCodec_getInputBuffer(mCodec, index, &capacity);
        if (buf == nullptr) {
            AMediaCodec_queueInputBuffer(mCodec, index, 0, 0, 0, 0);
            mPendingPacket.reset();
            return false;
        }

        std::shared_ptr<VEPacket> packet = mPendingPacket;
        mPendingPacket.reset();

        if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
            AMediaCodec_queueInputBuffer(mCodec, index, 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            mInputEOS = true;
            return true;
        }

        // 走 bsf 转 Annex-B。一个输入包可能吐出多个包(极少见)，
        // 这里只取第一个，剩下的下一轮由 bsf 继续吐
        AVPacket *av = packet->getPacket();
        size_t written = 0;
        int64_t ptsUs = packet->getPts();
        if (av_bsf_send_packet(mBsf, av) == 0) {
            AVPacket *out = av_packet_alloc();
            if (out && av_bsf_receive_packet(mBsf, out) == 0) {
                if (static_cast<size_t>(out->size) <= capacity) {
                    memcpy(buf, out->data, out->size);
                    written = static_cast<size_t>(out->size);
                    if (out->pts != AV_NOPTS_VALUE) {
                        ptsUs = out->pts;
                    }
                } else {
                    ALOGW("VEMediaCodecVideoDecoder::%s packet %d > capacity %zu, dropped",
                          __FUNCTION__, out->size, capacity);
                }
            }
            if (out) av_packet_free(&out);
        }

        if (mPerfStats && written > 0) {
            // 有界表：codec 内部通常只压几帧，64 条足够；满了丢最旧的，
            // 避免异常情况下无界增长
            if (mFeedTimeUs.size() >= kMaxPendingTimes) {
                mFeedTimeUs.erase(mFeedTimeUs.begin());
            }
            mFeedTimeUs[ptsUs] = nowUs();
        }
        AMediaCodec_queueInputBuffer(mCodec, index, 0, written,
                                     static_cast<uint64_t>(ptsUs), 0);
        return written > 0;
    }

    bool VEMediaCodecVideoDecoder::drainOutput() {
        if (mOutputEOS || mOutQueue.size() >= kMaxOutQueue) {
            return false;
        }
        AMediaCodecBufferInfo info;
        ssize_t index = AMediaCodec_dequeueOutputBuffer(mCodec, &info, 0);
        const int64_t renderedNow = mRenderedFrames.load(std::memory_order_relaxed);
        if (VE_FAULT_HW_AFTER(renderedNow)) {
            // **运行期失败, 最难触发也最该测的一条**: 建链期失败还有工厂兜底,
            // 运行期要求播放器在播放中途重建为软解且画面不中断
            ALOGW("VEFAULT inject: hw runtime failure after %lld frames",
                  (long long) renderedNow);
            index = AMEDIA_ERROR_UNKNOWN;
        }
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER ||
            index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            return false;
        }
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *fmt = AMediaCodec_getOutputFormat(mCodec);
            if (fmt) {
                int32_t w = 0, h = 0;
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                ALOGI("VEMediaCodecVideoDecoder::%s output format %dx%d",
                      __FUNCTION__, w, h);
                AMediaFormat_delete(fmt);
            }
            return true;
        }
        if (index < 0) {
            ALOGE("VEMediaCodecVideoDecoder::%s dequeueOutputBuffer error %zd",
                  __FUNCTION__, index);
            reportFatal("dequeueOutputBuffer error");
            return false;
        }

        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            ALOGI("VEMediaCodecVideoDecoder::%s output EOS", __FUNCTION__);
            AMediaCodec_releaseOutputBuffer(mCodec, index, false);
            mOutputEOS = true;
            if (mNotifyFirstFrame) {
                // seek 目标落在最后一帧之后：没有帧可上屏，用 EOS 顶替首帧回执，
                // 否则 seek 只能干等超时
                mNotifyFirstFrame = false;
                postEvent(EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER,
                          VE_NOTIFY_EVENT_FIRST_FRAME, 0, 0);
            }
            postEvent(EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER,
                      VE_NOTIFY_EVENT_EOS, 0, 0);
            return false;
        }

        const int64_t ptsUs = info.presentationTimeUs;

        // 精准 seek：目标之前的帧直接丢，不上屏
        if (mSeekTargetUs != kNoSeekTarget && ptsUs < mSeekTargetUs) {
            AMediaCodec_releaseOutputBuffer(mCodec, index, false);
            return true;
        }
        if (mSeekTargetUs != kNoSeekTarget) {
            mSeekTargetUs = kNoSeekTarget;
        }
        // surface 没了：解码继续(保持参考帧链完整)但不上屏
        if (mSurfaceLost || mWindow == nullptr) {
            AMediaCodec_releaseOutputBuffer(mCodec, index, false);
            return true;
        }

        // T6：硬解的"首帧解出"——codec 已经吐出可渲染的 buffer。
        // 注意这不是首帧上屏，上屏还要等同步时钟放行(见 T7)
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T6_FIRST_FRAME_DECODED);
        }
        if (mPerfStats) {
            auto it = mFeedTimeUs.find(ptsUs);
            if (it != mFeedTimeUs.end()) {
                // 写进 codecLatencyUs 而**不是** videoDecodeUs：这个数字里
                // 大部分是背压等待，不是解码的 CPU 成本。混进同一字段会让
                // 软硬解的对照彻底失真。
                mPerfStats->codecLatencyUs.add(nowUs() - it->second);
                // 用完即删，顺带把比它更早的残留一起清掉(那些包被 codec
                // 丢弃或重排了，留着只会让表被撑满)
                mFeedTimeUs.erase(mFeedTimeUs.begin(), std::next(it));
            }
        }
        mOutQueue.push_back({index, ptsUs});
        return true;
    }

    void VEMediaCodecVideoDecoder::scheduleRender() {
        if (mRenderPending || mOutQueue.empty() || !mIsStarted) {
            return;
        }
        const OutBuffer &head = mOutQueue.front();
        mAVSync->updateVideoPts(static_cast<double>(head.ptsUs));

        if (mAVSync->shouldDropFrame()) {
            // 落后太多：丢帧追赶(render=false 即丢弃，不上屏)
            AMediaCodec_releaseOutputBuffer(mCodec, head.index, false);
            ++mDroppedFrames;
            if (mPerfStats) { ++mPerfStats->dropLate; }
            mOutQueue.pop_front();
            scheduleRender();
            return;
        }

        const int64_t waitUs = mAVSync->getWaitTime();
        auto msg = std::make_shared<AMessage>(kWhatRenderOut, shared_from_this());
        msg->setInt32("epoch", mEpoch);
        msg->post(waitUs);
        mRenderPending = true;
    }

    void VEMediaCodecVideoDecoder::onRenderOut(const std::shared_ptr<AMessage> &msg) {
        int32_t epoch = 0;
        msg->findInt32("epoch", &epoch);
        mRenderPending = false;
        if (epoch != mEpoch || mOutQueue.empty() || mCodec == nullptr) {
            return;
        }

        const OutBuffer out = mOutQueue.front();
        mOutQueue.pop_front();

        // 零拷贝上屏：直接把 buffer 交还给 codec 并要求它渲染到绑定的
        // Surface。全程不经 CPU，也没有 GL 纹理上传。
        if (mPerfStats && mAVSync) {
            mPerfStats->noteSyncMargin(mAVSync->getLastDiffUs());
        }
        const int64_t presentBeginUs = mPerfStats ? nowUs() : 0;
        AMediaCodec_releaseOutputBuffer(mCodec, out.index, true);
        if (mPerfStats) {
            const int64_t nowU = nowUs();
            mPerfStats->presentUs.add(nowU - presentBeginUs);
            if (mLastPresentUs != 0) {
                mPerfStats->presentIntervalUs.add(nowU - mLastPresentUs);
            }
            mLastPresentUs = nowU;
        }
        ++mRenderedFrames;
        // T7：硬解路径的首帧上屏。物理含义是"交给 SurfaceFlinger 合成"，
        // 与软解的"提交 GLES/Vulkan 并 swap"不同，两者不可直接互比——
        // JSON 里的 decodePath 字段就是为此存在的
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T7_FIRST_FRAME_PRESENTED);
        }

        if (mNotifyFirstFrame) {
            mNotifyFirstFrame = false;
            postEvent(EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER,
                      VE_NOTIFY_EVENT_FIRST_FRAME, 0, out.ptsUs);
        }
        scheduleRender();
    }

    // ---------------------------------------------------------------------
    // 事件上报
    // ---------------------------------------------------------------------

    void VEMediaCodecVideoDecoder::postEvent(int32_t componentType, int32_t event,
                                             int32_t arg1, int64_t arg3) {
        auto msg = mNotify->dup();
        msg->setInt32("type", componentType);
        msg->setInt32("event", event);
        msg->setInt32("arg1", arg1);
        msg->setInt32("arg2", 0);
        msg->setInt64("arg3", arg3);
        msg->setPointer("params", nullptr);
        msg->post();
    }

    void VEMediaCodecVideoDecoder::postEventBothRoles(int32_t event, int32_t arg1) {
        // 本组件在播放器的 Role 表里占两个槽位(解码 + 显示)，
        // 因此每条命令都要回两份回执，否则分阶段握手永远等不齐
        postEvent(EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER, event, arg1, 0);
        postEvent(EComponentType::E_COMPONENT_TYPE_VIDEO_RENDER, event, arg1, 0);
    }

    void VEMediaCodecVideoDecoder::reportFatal(const char *reason) {
        ALOGE("VEMediaCodecVideoDecoder fatal: %s", reason);
        mIsStarted = false;
        // arg2 带上 fallback 标记：播放器据此走"重建为软解"而不是直接进 ERROR
        auto msg = mNotify->dup();
        msg->setInt32("type", EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER);
        msg->setInt32("event", VE_NOTIFY_EVENT_ERROR);
        msg->setInt32("arg1", VE_UNKNOWN_ERROR);
        msg->setInt32("arg2", VE_INFO_DECODER_FALLBACK);
        msg->setInt64("arg3", 0);
        msg->setPointer("params", nullptr);
        msg->post();
    }
}
