#include "VEVideoDecoder.h"
#include "utils/VEPerfStats.h"
#include <iostream>

/// 解码帧队列深度。帧现在是引用而非整帧拷贝，但仍占住解码器的缓冲池，
/// 不宜过深；够吸收渲染抖动即可。
#define  FRAME_QUEUE_MAX_SIZE 6

/// 饥饿时的兜底重试间隔。正常靠源的 one-shot 通知唤醒，
/// 这条只防"源没发通知"的意外，取值可以放得很宽。
#define kStarveBackstopUs 500000

/// 连续坏包容忍上限，超过按致命错误处理(与 VEDemux 的同名策略一致)
#define kMaxConsecutiveSendErrors 100

namespace VE {
    namespace {
        /// 渲染器按 3 平面 8bit YUV420 上传纹理，这两种格式可以直接送过去
        bool isDirectRenderable(int format) {
            return format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P;
        }
    }

    VEVideoDecoder::VEVideoDecoder(std::shared_ptr<AMessage> &notify)
            : mVideoCtx(nullptr),
              mMediaInfo(nullptr),
              mIsStarted(false) {
        mNofityEvent = notify;
    }

    VEVideoDecoder::~VEVideoDecoder() {
        // 不直接释放资源，统一在 release / onRelease 中处理
    }

    VEResult VEVideoDecoder::prepare(std::shared_ptr<IMediaSource> demux,
                                     std::shared_ptr<IFrameSink> sink,
                                     const VEBundle &params) {
        (void) params;   // 软解不需要额外参数
        if (!demux || !sink) {
            ALOGE("VEVideoDecoder::prepare demux/sink is null");
            return VE_INVALID_PARAMS;
        }
        auto msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("demux", demux);
        msg->setObject("sink", sink);
        msg->post();
        return VE_OK;
    }


    VEResult VEVideoDecoder::seekTo(double timestampMs) {

        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp", timestampMs);
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::start() {
        auto msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::pause() {
        auto msg = std::make_shared<AMessage>(kWhatPause, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::stop() {
        auto msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEVideoDecoder::flush() {
        auto msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return VE_OK;
    }

    VEResult VEVideoDecoder::release() {
        auto msg = std::make_shared<AMessage>(kWhatUninit, shared_from_this());
        msg->post();
        return VE_OK;
    }

// 消息处理函数
    void VEVideoDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {

        switch (msg->what()) {
            case kWhatInit: {
                // prepare 失败必须上报：静默吞掉的话 codec 未打开，
                // start 后首次 receive_frame 就是持续性错误
                if (onPrepare(msg) != VE_OK) {
                    postMessage(VE_NOTIFY_EVENT_ERROR, VE_UNKNOWN_ERROR, 0, 0, nullptr);
                }
                break;
            }
            case kWhatStart: {
                onStart();
                break;
            }
            case kWhatPause: {
                onPause();
                // 走到这里说明排在前面的解码消息都已处理完，之后的解码消息会因
                // mIsStarted=false 被丢弃，可以安全地告知上层"已停止消费"
                postMessage(VE_NOTIFY_EVENT_PAUSE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatStop: {
                onStop();
                postMessage(VE_NOTIFY_EVENT_STOP_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatFlush: {
                onFlush();
                postMessage(VE_NOTIFY_EVENT_FLUSH_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatDecode: {
                int32_t epoch = 0;
                msg->findInt32("epoch", &epoch);
                if (epoch != mEpoch) {
                    // flush/seek 之前投递的解码消息，丢弃
                    ALOGI("VEVideoDecoder::onDecode stale decode msg, epoch=%d cur=%d", epoch, mEpoch);
                    break;
                }
                if (!mIsStarted) {
                    break;
                }
                int32_t starveGen = 0;
                if (msg->findInt32("starveGen", &starveGen)) {
                    // 饥饿唤醒消息：同一次饥饿的两个唤醒源只允许一个生效
                    if (starveGen != mStarveGen) {
                        ALOGV("VEVideoDecoder stale starve wake, gen=%d cur=%d",
                              starveGen, mStarveGen);
                        break;
                    }
                    ++mStarveGen;   // 让兄弟唤醒源作废
                }

                VEResult ret = onDecode();
                if (ret == VE_OK) {
                    postDecode();
                } else if (ret == VE_NOT_ENOUGH_DATA) {
                    // 上游饥饿是数据面状态：onDecode 已投延时重试，
                    // 不动命令态(mIsStarted)——这就是命令/数据分离
                } else if (ret == VE_NO_MEMORY) {
                    // credit 用尽(park)同样是纯数据面状态：渲染器消费一帧后
                    // 回执会把 credit 还回来，kWhatFrameConsumed 那里重新拉起
                    // 解码循环。这里**绝对不能**动 mIsStarted——它一旦为 false，
                    // 复活条件 `mIsStarted && ...` 永远为假，整条解码链就死了，
                    // 表现为起播一秒后声音断掉、画面冻结，而时钟还在实时外推。
                } else {
                    ALOGI("VEVideoDecoder::onMessageReceived onDecode stopped, ret=%d", ret);
                    // 到这里只剩 EOS 与真错误，才该收命令态
                    mIsStarted = false;
                    if (ret != VE_EOS) {
                        postMessage(VE_NOTIFY_EVENT_ERROR, ret, 0, 0, nullptr);
                    }
                }
                break;
            }
            case kWhatUninit: {
                onRelease();
                postMessage(VE_NOTIFY_EVENT_RELEASE_DONE, 0, 0, 0, nullptr);
                break;
            }
            case kWhatFrameConsumed: {
                // 渲染器归还 credit。epoch 校验：flush 已把在途计数清零，
                // 旧帧的迟到回执不允许再加 credit
                int32_t epoch = 0;
                msg->findInt32("epoch", &epoch);
                if (epoch != mEpoch) {
                    break;
                }
                if (mInFlightFrames > 0) {
                    --mInFlightFrames;
                }
                if (mIsStarted && !mIsEOS && mInFlightFrames == FRAME_QUEUE_MAX_SIZE - 1) {
                    // 从满转不满：复活解码循环(credit 归还就是数据面唤醒)
                    postDecode();
                }
                break;
            }
            case kWhatSeek:{
                double timestampMs = 0;
                msg->findDouble("timestamp", &timestampMs);
                onSeek(timestampMs);
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE,0,0,0, nullptr);
                break;
            }
            default: {
                ALOGW("VEVideoDecoder::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEResult VEVideoDecoder::onPrepare(std::shared_ptr<AMessage> msg) {
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T4A_CONFIGURE_BEGIN);
        }
        std::shared_ptr<void> tmp;
        if (!msg->findObject("demux", &tmp)) {
            ALOGE("VEVideoDecoder::onPrepare demux not found in message");
            return VE_INVALID_PARAMS;
        }

        mDemux = std::static_pointer_cast<IMediaSource>(tmp);
        if (!mDemux) {
            ALOGE("VEVideoDecoder::onPrepare demux cast failed");
            return VE_INVALID_PARAMS;
        }

        std::shared_ptr<void> sinkTmp;
        if (!msg->findObject("sink", &sinkTmp)) {
            ALOGE("VEVideoDecoder::onPrepare sink not found in message");
            return VE_INVALID_PARAMS;
        }
        mSink = std::static_pointer_cast<IFrameSink>(sinkTmp);
        mInFlightFrames = 0;

        mMediaInfo = mDemux->getFileInfo();
        const VETrackInfo *track = mMediaInfo ? mMediaInfo->videoTrack() : nullptr;
        if (track == nullptr || track->codecParams == nullptr) {
            ALOGE("VEVideoDecoder::onPrepare invalid media info or video codec params");
            return VE_INVALID_PARAMS;
        }

        const AVCodec *video_codec = avcodec_find_decoder(track->codecParams->codec_id);
        if (!video_codec) {
            ALOGE("VEVideoDecoder::onPrepare Could not find video codec");
            return VE_UNKNOWN_ERROR;
        }

        mVideoCtx = avcodec_alloc_context3(video_codec);
        if (!mVideoCtx) {
            ALOGE("VEVideoDecoder::onPrepare Could not allocate video codec context");
            return VE_UNKNOWN_ERROR;
        }

        if (avcodec_parameters_to_context(mVideoCtx, track->codecParams) < 0) {
            ALOGE("VEVideoDecoder::onPrepare Could not copy codec parameters to codec context");
            avcodec_free_context(&mVideoCtx);
            mVideoCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }

        // 解码线程：此前一处都没设，跑在 FFmpeg 默认(单线程)上。
        //
        // 说清它能与不能：**不降低总 CPU**——多线程是把同样的工作摊到多核，
        // 总量基本不变甚至因调度略增；但它大幅降低单帧 wall time，决定软解
        // 扛不扛得住更高规格的素材(实测 1080p30 时 vdec_thread 已占单核
        // 79.5%，只剩两成余量)。
        //
        // 0 = 按 CPU 核数自动。FF_THREAD_FRAME 会让输出延迟约 thread_count
        // 帧——这与刚做完的首帧优化直接冲突，所以下面必须同时实测首帧耗时，
        // 不能只看 CPU 和解码耗时。
        // 只开 slice 线程，**刻意不开 FF_THREAD_FRAME**。
        // 帧级线程要等流水线填满才吐第一帧(实测首帧解码 50→111.6ms、
        // 启播总耗时 139~162→252ms)，把"软解扛得住多高规格"和"启播多快"
        // 两个目标对立起来了。而当前默认路径是硬解、软解只在被迫时才走，
        // 那种场景下启播慢 90ms 比能扛 4K 更容易被感知。
        // slice 线程没有输出延迟，代价是收益依赖码流是否真的分了 slice。
        mVideoCtx->thread_count = 0;
        mVideoCtx->thread_type = FF_THREAD_SLICE;

        if (avcodec_open2(mVideoCtx, video_codec, nullptr) < 0) {
            ALOGE("VEVideoDecoder::onPrepare Could not open video codec");
            avcodec_free_context(&mVideoCtx);
            mVideoCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }

        if (mStartupTrace != nullptr) {
            // 软解没有独立的 configure 阶段，avcodec_open2 就是它的等价物
            mStartupTrace->mark(VEStartupTrace::T4A_CONFIGURE_END);
        }
        ALOGI("VEVideoDecoder::onPrepare success");
        return VE_OK;
    }

    VEResult VEVideoDecoder::onStart() {
        if (mIsStarted) {
            ALOGI("VEVideoDecoder::onStart already started");
            return VE_OK;
        }
        mIsEOS = false;
        mIsStarted = true;
        postDecode();
        return VE_OK;
    }

    VEResult VEVideoDecoder::onPause() {
        mIsStarted = false;
        return VE_OK;
    }

    VEResult VEVideoDecoder::onStop() {
        mIsStarted = false;
        // 残留的精准 seek 目标会让下次重播把 0~target 的帧全部丢掉
        mSeekTargetUs = kNoSeekTarget;
        // 渲染器 stop 时清掉了自己的队列(不发回执)，credit 由本侧清算
        mInFlightFrames = 0;
        if (mVideoCtx) {
            avcodec_flush_buffers(mVideoCtx);
            mVideoCtx->skip_frame = AVDISCARD_DEFAULT;
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onFlush() {
        // 已送包但没结算的耗时随 flush 作废，否则会挂到 flush 后的第一帧上
        mDecodeAccumUs = 0;
        // 递增 epoch，使 flush 之前投递的解码消息与旧帧的消费回执全部失效
        ++mEpoch;
        mIsStarted = false;
        mIsEOS = false;
        // seek 流程会在 flush 之后重新设置；不经 seek 的 flush 必须清掉
        mSeekTargetUs = kNoSeekTarget;
        // 渲染器同轮 seek/flush 会清自己的队列(不发回执)，credit 由本侧清算
        mInFlightFrames = 0;
        mSendErrorCount = 0;
        if (mVideoCtx) {
            avcodec_flush_buffers(mVideoCtx);
            // 兜底：不经 seek 的 flush 要恢复完整解码(onSeek 随后会重设)
            mVideoCtx->skip_frame = AVDISCARD_DEFAULT;
        }
        return VE_OK;
    }

    VEResult VEVideoDecoder::onSeek(double timestampMs) {
        // seek 必须真正清空 codec 内部参考帧和已解出的帧，
        // 否则 seek 后会渲染出目标位置之前的残留画面
        onFlush();
        // demux 只能定位到关键帧，这里记录目标位置，解码时丢弃其之前的帧
        mSeekTargetUs = static_cast<int64_t>(timestampMs * 1000);
        // 追赶期间跳过非参考帧：这段帧反正要丢，没必要完整解出来。
        // 关键帧与参考帧仍然照解(后续帧依赖它们)，所以画面不会出错。
        // 长 GOP 内容的 seek 追赶耗时会明显下降。
        if (mVideoCtx) {
            mVideoCtx->skip_frame = AVDISCARD_NONREF;
        }
        return VE_OK;
    }

    std::shared_ptr<VEFrame> VEVideoDecoder::convertToYuv420p(const std::shared_ptr<VEFrame> &src) {
        AVFrame *in = src->getFrame();

        mSwsCtx = sws_getCachedContext(mSwsCtx,
                                       in->width, in->height,
                                       static_cast<AVPixelFormat>(in->format),
                                       in->width, in->height, AV_PIX_FMT_YUV420P,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (mSwsCtx == nullptr) {
            ALOGE("VEVideoDecoder::%s failed to create sws context for format %d",
                  __FUNCTION__, in->format);
            return nullptr;
        }

        auto dst = std::make_shared<VEFrame>(in->width, in->height, AV_PIX_FMT_YUV420P);
        AVFrame *out = dst->getFrame();
        if (out == nullptr || out->data[0] == nullptr) {
            ALOGE("VEVideoDecoder::%s failed to allocate target frame", __FUNCTION__);
            return nullptr;
        }

        sws_scale(mSwsCtx, in->data, in->linesize, 0, in->height, out->data, out->linesize);

        dst->setFrameType(E_FRAME_TYPE_VIDEO);
        dst->setPts(in->pts);
        dst->setDts(in->pkt_dts);
        return dst;
    }

    void VEVideoDecoder::postDecode(int64_t delayUs) {
        auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        decodeMsg->setInt32("epoch", mEpoch);
        decodeMsg->post(delayUs);
    }

    VEResult VEVideoDecoder::onDecode() {

        if (mInFlightFrames >= FRAME_QUEUE_MAX_SIZE) {
            // credit 用尽：park。渲染器每消费一帧就回执还 credit，
            // kWhatFrameConsumed 处理时会复活解码循环
            if (mPerfStats) { ++mPerfStats->videoCreditPark; }
            ALOGF("VEVideoDecoder::onDecode out of credit, parking");
            return VE_NO_MEMORY;
        }

        // 单帧解码成本 = 送包耗时(累计) + 取帧/转换耗时。
        //
        // **avcodec_send_packet 才是真正干活的地方**，receive_frame 只是把
        // 已解好的帧递出来。早先只量 receive 那一侧，1080p 软解报 p50=0.1ms，
        // 而同一时刻 vdec_thread 占 81% CPU(30fps 摊算每帧约 27ms)——差 270 倍。
        // 一帧对应的包可能是前几次 onDecode 送进去的，所以 send 耗时必须累加，
        // 产出帧时一起结算再清零。
        //
        // 这与硬解那次是同源错误：**指标名与它实际度量的东西不一致**。
        // 已经犯过两次，下次打点前先自问：这个数字的名字和它量到的是同一件事吗。
        const int64_t decodeBeginUs = nowUs();
        // 与墙钟起点并取: 墙钟含等待, CPU 只算真正烧掉的算力。
        // 自校验要的是后者(见 VEPerfStats::CpuGauge)
        const int64_t decodeCpuBeginUs = mPerfStats ? threadCpuUs() : 0;

        VEResult ret = VE_OK;
        do {
            auto frame = std::make_shared<VEFrame>();
            ret = avcodec_receive_frame(mVideoCtx, frame->getFrame());
            if (ret == AVERROR_EOF) {
                ALOGI("VEVideoDecoder::onDecode AVERROR_EOF");
                frame->setFrameType(E_FRAME_TYPE_EOF);
                queueFrame(frame);
                mIsEOS = true;
                return VE_EOS;
            }
            if (ret >= 0) {
                // 精准 seek：解码器从关键帧开始出帧，目标之前的帧直接丢弃，
                // 不做 YUV 拷贝也不入队，只是继续解码
                if (mSeekTargetUs != kNoSeekTarget) {
                    int64_t pts = frame->getFrame()->pts;
                    if (pts != AV_NOPTS_VALUE && pts < mSeekTargetUs) {
                        ALOGF("VEVideoDecoder::onDecode drop frame pts=%" PRId64
                                      " until %" PRId64, pts, mSeekTargetUs);
                        // 精准 seek 的追帧丢弃：属 seek 成本，不是渲染缺陷
                        if (mPerfStats) { ++mPerfStats->dropSeekCatchup; }
                        return VE_OK;
                    }
                    ALOGI("VEVideoDecoder::onDecode reached seek target pts=%" PRId64, pts);
                    mSeekTargetUs = kNoSeekTarget;
                    // 追赶结束，恢复完整解码
                    mVideoCtx->skip_frame = AVDISCARD_DEFAULT;
                }

                AVFrame *decoded = frame->getFrame();

                if (isDirectRenderable(decoded->format)) {
                    // 零拷贝：直接把解码出的帧交给渲染链路。渲染器用
                    // GL_UNPACK_ROW_LENGTH 处理行距，不需要先拷成紧排布。
                    frame->setFrameType(E_FRAME_TYPE_VIDEO);
                    frame->setPts(decoded->pts);
                    frame->setDts(decoded->pkt_dts);
                    ALOGF("VEVideoDecoder::onDecode got a frame: pts=%" PRId64, decoded->pts);
                    if (mPerfStats) {
                        mPerfStats->videoDecodeUs.add(mDecodeAccumUs + nowUs() - decodeBeginUs);
                        const int64_t cpuNow = threadCpuUs();
                        mPerfStats->vdecCpu.note(cpuNow, mDecodeAccumCpuUs + cpuNow - decodeCpuBeginUs);
                        mDecodeAccumCpuUs = 0;
                    mDecodeAccumUs = 0;
                    }
                    queueFrame(frame);
                    return VE_OK;
                }

                // 其它像素格式渲染器认不了，转成 YUV420P 再送
                auto videoFrame = convertToYuv420p(frame);
                if (videoFrame == nullptr) {
                    return VE_UNKNOWN_ERROR;
                }
                if (mPerfStats) {
                    mPerfStats->videoDecodeUs.add(mDecodeAccumUs + nowUs() - decodeBeginUs);
                    const int64_t cpuNow = threadCpuUs();
                    mPerfStats->vdecCpu.note(cpuNow, mDecodeAccumCpuUs + cpuNow - decodeCpuBeginUs);
                        mDecodeAccumCpuUs = 0;
                        mDecodeAccumUs = 0;
                }
                queueFrame(videoFrame);
                return VE_OK;
            }
            if (ret != AVERROR(EAGAIN)) {
                // 持续性错误(如 codec 未打开返回 EINVAL)：必须退出，
                // 否则循环条件不变化，忙循环卡死整个解码 looper
                ALOGE("VEVideoDecoder::onDecode receive_frame fatal: %d", ret);
                return VE_UNKNOWN_ERROR;
            }
        } while (ret != AVERROR(EAGAIN));

        std::shared_ptr<VEPacket> packet;
        ret = mDemux->read(ETrackType::VIDEO, packet);
        if (ret == VE_NOT_ENOUGH_DATA) {
            // 上游饥饿：登记一次性通知，数据入队时被唤醒(不再 10ms 轮询)。
            // 消息带当前 epoch，flush/seek 后自动作废；这是纯数据面事件，
            // 不触碰命令态。同时投一条兜底重试，防源实现漏发通知。
            ALOGV("VEVideoDecoder::onDecode starving, waiting for data notify");
            if (mPerfStats) {
                ++mPerfStats->videoStarve;
                // 只在"从不饥饿转饥饿"时记起点：饥饿期间可能被唤醒多次
                if (mStarveBeginUs == 0) { mStarveBeginUs = nowUs(); }
            }
            const int32_t starveGen = ++mStarveGen;
            auto notify = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
            notify->setInt32("epoch", mEpoch);
            notify->setInt32("starveGen", starveGen);
            mDemux->requestReadNotify(ETrackType::VIDEO, notify);
            auto backstop = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
            backstop->setInt32("epoch", mEpoch);
            backstop->setInt32("starveGen", starveGen);
            backstop->post(kStarveBackstopUs);
            return VE_NOT_ENOUGH_DATA;
        }
        if (mPerfStats && mStarveBeginUs != 0) {
            // 饥饿到此结束：次数多而时长短=供给抖动，时长长=源确实供不上
            mPerfStats->starveUs.add(nowUs() - mStarveBeginUs);
            mStarveBeginUs = 0;
        }
        if (!packet) {
            ALOGI("VEVideoDecoder::onDecode packet is null");
            return VE_UNEXPECTED_NULL;
        }

        if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
            ALOGI("VEVideoDecoder::onDecode got EOF packet");
            ret = avcodec_send_packet(mVideoCtx, nullptr);
        } else {
            ALOGF("VEVideoDecoder::onDecode got normal packet, size=%d", packet->getPacket()->size);
            // **追赶期的跳帧必须在够到目标之前收手。**
            //
            // onSeek 设的 AVDISCARD_NONREF 会让解码器整帧跳过非参考帧 —— 包括
            // **目标帧本身**。目标恰好是 B 帧时它永远不会被输出，首帧于是变成
            // 下一帧。原实现只在"解出一个 pts >= 目标的帧"时才恢复，而那时已经
            // 晚了：错过的正是那一帧。
            //
            // 实测(23.976fps 素材、10 次 seek、逐条对帧栅格)：
            //   硬解 10/10 精确落在目标之后第一帧；
            //   软解 3/10，另外 7 次**恰好晚一帧**；
            //   关掉 AVDISCARD_NONREF 后软解 10/10 —— 根因确证。
            //
            // 不删这个优化：实测它把 seek 预热 max 从 58.4ms 压到 31.3ms、
            // 均值 33.8→23.2ms，长 GOP 内容上差距更大。
            //
            // 改用**包 pts** 而不是帧 pts：包在送进解码器之前就知道 pts，
            // 这是唯一来得及的时机。解码顺序与显示顺序不一致时(B 金字塔)
            // 可能提前几个包恢复，代价只是多解几帧，不影响正确性。
            if (mSeekTargetUs != kNoSeekTarget && mVideoCtx != nullptr &&
                mVideoCtx->skip_frame != AVDISCARD_DEFAULT) {
                const int64_t packetPts = packet->getPacket()->pts;
                if (packetPts != AV_NOPTS_VALUE && packetPts >= mSeekTargetUs) {
                    ALOGI("VEVideoDecoder::onDecode 追赶收手 packetPts=%" PRId64
                          " target=%" PRId64, packetPts, mSeekTargetUs);
                    mVideoCtx->skip_frame = AVDISCARD_DEFAULT;
                }
            }
            const int64_t sendBeginUs = mPerfStats ? nowUs() : 0;
            const int64_t sendCpuBeginUs = mPerfStats ? threadCpuUs() : 0;
            ret = avcodec_send_packet(mVideoCtx, packet->getPacket());
            if (mPerfStats) {
                mDecodeAccumUs += nowUs() - sendBeginUs;
                // send_packet 与 receive_frame 分属两段, 只测后者正是
                // 这个项目修过的老 bug(0.1ms vs 实际 14ms), CPU 侧同样
                // 要把两段都算进去, 否则自校验里 instrumented 恒为 0
                if (mPerfStats) {
                    mDecodeAccumCpuUs += threadCpuUs() - sendCpuBeginUs;
                }
            }
        }

        if (ret == AVERROR(EAGAIN)) {
            // 解码器内部满了(极少见，上面已经排空过)：不算错误，下一轮继续收帧
            return VE_OK;
        }
        if (ret == AVERROR_INVALIDDATA && ++mSendErrorCount < kMaxConsecutiveSendErrors) {
            // 局部损坏的包：跳过继续解，别让单个坏包打死整个播放
            // (demux 层同样容忍到 100 个连续坏包)
            ALOGW("VEVideoDecoder::onDecode skip corrupt packet (%d in a row)", mSendErrorCount);
            return VE_OK;
        }
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            ALOGE("VEVideoDecoder::onDecode fatal error sending packet: %s", errbuf);
            return VE_UNKNOWN_ERROR;
        }
        mSendErrorCount = 0;
        return VE_OK;
    }

    VEResult VEVideoDecoder::onRelease() {
        mIsStarted = false;
        if (mSwsCtx) {
            sws_freeContext(mSwsCtx);
            mSwsCtx = nullptr;
        }
        if (mVideoCtx) {
            avcodec_free_context(&mVideoCtx);
            mVideoCtx = nullptr;
        }
        mMediaInfo.reset();
        mDemux.reset();
        mSink.reset();
        mInFlightFrames = 0;

        return VE_OK;
    }


    void VEVideoDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
        // T6：首帧解出。三条产出路径都汇到这个函数，打在这里不会漏；
        // mark() 自带首次生效语义，不必自己判断是不是第一帧
        if (mStartupTrace != nullptr) {
            mStartupTrace->mark(VEStartupTrace::T6_FIRST_FRAME_DECODED);
        }
        if (!mSink) {
            return;
        }
        // 推模型：帧连同消费回执一起交给渲染器(仿 queueBuffer+notifyConsumed)。
        // 回执带当前 epoch，flush 后迟到的回执不能再归还 credit。
        auto reply = std::make_shared<AMessage>(kWhatFrameConsumed, shared_from_this());
        reply->setInt32("epoch", mEpoch);
        mSink->queueFrame(frame, reply);
        ++mInFlightFrames;
    }

    VEResult VEVideoDecoder::postMessage(int32_t event, int32_t arg1, int32_t arg2, int64_t arg3,
                                         void *params) {
        std::shared_ptr<AMessage> msg = mNofityEvent->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_VIDEO_DECODER);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return 0;
    }

}