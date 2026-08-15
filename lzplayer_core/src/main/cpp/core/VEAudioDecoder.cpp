#include "VEAudioDecoder.h"
#include "utils/VEPerfStats.h"
#include "VEError.h"

#define AUDIO_FRAME_QUEUE_SIZE 50

/// 饥饿时的兜底重试间隔。正常靠源的 one-shot 通知唤醒，
/// 这条只防"源没发通知"的意外，取值可以放得很宽。
#define kStarveBackstopUs 500000

/// 连续坏包容忍上限，超过按致命错误处理(与 VEDemux 的同名策略一致)
#define kMaxConsecutiveSendErrors 100

// 输出格式不再写死：由 VEPlayer 依据源参数统一决定后传进来，
// 保证解码器重采样的目标和渲染器配置的设备参数是同一份。
#define AUDIO_TARGET_OUTPUT_FORMAT  ((AVSampleFormat) mOutFormat)
#define AUDIO_TARGET_OUTPUT_SAMPLERATE  mOutSampleRate
#define AUDIO_TARGET_OUTPUT_CHANNELS    mOutChannels

namespace VE {
    VEAudioDecoder::VEAudioDecoder(std::shared_ptr<AMessage> &notify) {
        mAudioCtx = nullptr;
        mIsStarted = false;
        mSwrCtx = nullptr;
        mIsEOS = false;
        mNotifyEvent = notify;
    }

    VEAudioDecoder::~VEAudioDecoder() {
        // 不能调 release()：它内部要用 shared_from_this() 投递消息，
        // 而析构期间对象已不再被 shared_ptr 持有，会抛 bad_weak_ptr。
        // 正常路径下 onRelease 已由 release() 流程执行过，这里只是兜底。
        onRelease();
    }

    VEResult VEAudioDecoder::prepare(std::shared_ptr<IMediaSource> demux,
                                     const VEAudioOutputConfig &outConfig,
                                     std::shared_ptr<IFrameSink> sink) {
        VEBundle params;
        params.set("outSampleRate", outConfig.sampleRate);
        params.set("outChannels", outConfig.channels);
        params.set("outFormat", outConfig.format);
        return prepare(std::move(demux), std::move(sink), params);
    }

    VEResult VEAudioDecoder::prepare(std::shared_ptr<IMediaSource> demux,
                                     std::shared_ptr<IFrameSink> sink,
                                     const VEBundle &params) {
        if (!demux || !sink) {
            ALOGE("VEAudioDecoder::prepare demux/sink is null");
            return VE_INVALID_PARAMS;
        }
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        msg->setObject("demux", demux);
        msg->setObject("sink", sink);
        // 随消息带过去，避免跨线程直接写解码器成员
        msg->setInt32("outSampleRate", params.get<int>("outSampleRate", 44100));
        msg->setInt32("outChannels", params.get<int>("outChannels", 2));
        msg->setInt32("outFormat", params.get<int>("outFormat", AV_SAMPLE_FMT_S16));
        msg->post();
        return VE_OK;
    }

    VEResult VEAudioDecoder::flush() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatFlush, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::release() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatUninit, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::start() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStart, shared_from_this());
        msg->post();
        return 0;
    }

    VEResult VEAudioDecoder::stop() {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatStop, shared_from_this());
        msg->post();
        return 0;
    }

    void VEAudioDecoder::onMessageReceived(const std::shared_ptr<AMessage> &msg) {

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
                    ALOGI("VEAudioDecoder::onDecode stale decode msg, epoch=%d cur=%d", epoch, mEpoch);
                    break;
                }
                if (!mIsStarted) {
                    break;
                }
                int32_t starveGen = 0;
                if (msg->findInt32("starveGen", &starveGen)) {
                    // 饥饿唤醒消息：同一次饥饿的两个唤醒源只允许一个生效
                    if (starveGen != mStarveGen) {
                        ALOGV("VEAudioDecoder stale starve wake, gen=%d cur=%d",
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
                    ALOGI("VEAudioDecoder::onMessageReceived onDecode stopped, ret=%d", ret);
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
                if (mIsStarted && !mIsEOS && mInFlightFrames == AUDIO_FRAME_QUEUE_SIZE - 1) {
                    // 从满转不满：复活解码循环(credit 归还就是数据面唤醒)
                    postDecode();
                }
                break;
            }
            case kWhatSeek:{
                double timestampMs = 0;
                msg->findDouble("timestamp", &timestampMs);
                onSeek(timestampMs);
                postMessage(VE_NOTIFY_EVENT_SEEK_DONE, 0, 0, 0, nullptr);
                break;
            }
            default: {
                ALOGW("VEAudioDecoder::onMessageReceived unknown message");
                break;
            }
        }
    }

    VEResult VEAudioDecoder::onPrepare(std::shared_ptr<AMessage> msg) {
        std::shared_ptr<void> tmp;
        msg->findObject("demux", &tmp);

        msg->findInt32("outSampleRate", &mOutSampleRate);
        msg->findInt32("outChannels", &mOutChannels);
        msg->findInt32("outFormat", &mOutFormat);
        ALOGI("VEAudioDecoder::%s output config %dHz %dch fmt:%d", __FUNCTION__,
              mOutSampleRate, mOutChannels, mOutFormat);

        mDemux = std::static_pointer_cast<IMediaSource>(tmp);
        std::shared_ptr<void> sinkTmp;
        if (!msg->findObject("sink", &sinkTmp)) {
            ALOGE("VEAudioDecoder::%s sink not found in message", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }
        mSink = std::static_pointer_cast<IFrameSink>(sinkTmp);
        mInFlightFrames = 0;
        std::shared_ptr<VEMediaInfo> info = mDemux->getFileInfo();
        const VETrackInfo *track = info ? info->audioTrack() : nullptr;
        if (track == nullptr || track->codecParams == nullptr) {
            ALOGE("VEAudioDecoder::%s invalid media info or audio codec params", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }
        // 持住这份信息：codecParams 归 VEMediaInfo 所有，解码器活着它就得活着
        mMediaInfo = info;

        const AVCodec *codec = avcodec_find_decoder(track->codecParams->codec_id);
        if (codec == nullptr) {
            ALOGE("VEAudioDecoder::%s could not find audio codec", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        mAudioCtx = avcodec_alloc_context3(codec);
        if (mAudioCtx == nullptr) {
            ALOGE("VEAudioDecoder::%s could not allocate audio codec context", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }

        if (avcodec_parameters_to_context(mAudioCtx, track->codecParams) < 0) {
            ALOGE("VEAudioDecoder::%s could not copy codec parameters", __FUNCTION__);
            avcodec_free_context(&mAudioCtx);
            mAudioCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }

        if (avcodec_open2(mAudioCtx, codec, nullptr) != 0) {
            ALOGE("VEAudioDecoder::%s could not open audio codec", __FUNCTION__);
            avcodec_free_context(&mAudioCtx);
            mAudioCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }

        ALOGI("VEAudioDecoder::%s success", __FUNCTION__);
        return VE_OK;
    }

    VEResult VEAudioDecoder::onFlush() {
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
        if (mAudioCtx) {
            avcodec_flush_buffers(mAudioCtx);
        }
        return VE_OK;
    }

    VEResult VEAudioDecoder::onSeek(double timestampMs) {
        onFlush();
        mSeekTargetUs = static_cast<int64_t>(timestampMs * 1000);
        return VE_OK;
    }

    void VEAudioDecoder::postDecode(int64_t delayUs) {
        auto decodeMsg = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
        decodeMsg->setInt32("epoch", mEpoch);
        decodeMsg->post(delayUs);
    }

    VEResult VEAudioDecoder::onDecode() {
        VEResult ret = VE_OK;
        if (mInFlightFrames >= AUDIO_FRAME_QUEUE_SIZE) {
            // credit 用尽：park。渲染器每消费一帧就回执还 credit，
            // kWhatFrameConsumed 处理时会复活解码循环
            if (mPerfStats) { ++mPerfStats->audioCreditPark; }
            ALOGF("VEAudioDecoder::onDecode out of credit, parking");
            return VE_NO_MEMORY;
        }

        do {
            std::shared_ptr<VEFrame> frame = std::make_shared<VEFrame>();
            const int64_t decodeBeginUs = mPerfStats ? nowUs() : 0;
            ret = avcodec_receive_frame(mAudioCtx, frame->getFrame());
            if (ret == AVERROR_EOF) {
                ALOGI("VEAudioDecoder::onDecode AVERROR_EOF");
                frame->setFrameType(E_FRAME_TYPE_EOF);
                queueFrame(frame);
                mIsEOS = true;
                return VE_EOS;
            }

            if (ret >= 0) {
                ALOGF("###VEAudioDecoder Audio frame: pts:%" PRId64 ", dts:%" PRId64,
                      frame->getFrame()->pts, frame->getFrame()->pkt_dts);

                // 精准 seek：丢弃目标位置之前的音频帧，避免 seek 后回放旧内容
                if (mSeekTargetUs != kNoSeekTarget) {
                    int64_t pts = frame->getFrame()->pts;
                    if (pts != AV_NOPTS_VALUE && pts < mSeekTargetUs) {
                        ALOGD("VEAudioDecoder::onDecode drop frame pts=%" PRId64
                                      " until %" PRId64, pts, mSeekTargetUs);
                        return VE_OK;
                    }
                    ALOGI("VEAudioDecoder::onDecode reached seek target pts=%" PRId64, pts);
                    mSeekTargetUs = kNoSeekTarget;
                }

                if (frame->getFrame()->format != (int32_t) AUDIO_TARGET_OUTPUT_FORMAT ||
                    frame->getFrame()->sample_rate != AUDIO_TARGET_OUTPUT_SAMPLERATE ||
                    frame->getFrame()->ch_layout.nb_channels != AUDIO_TARGET_OUTPUT_CHANNELS) {
                    if (mSwrCtx == nullptr) {
                        // 统一用新版 ch_layout 接口，不再混用已废弃的
                        // "in_channel_layout"/"out_channel_layout" 掩码选项
                        AVChannelLayout inLayout;
                        AVChannelLayout outLayout;
                        av_channel_layout_copy(&inLayout, &frame->getFrame()->ch_layout);
                        av_channel_layout_default(&outLayout, AUDIO_TARGET_OUTPUT_CHANNELS);

                        int ret = swr_alloc_set_opts2(
                                &mSwrCtx,
                                &outLayout, AUDIO_TARGET_OUTPUT_FORMAT, AUDIO_TARGET_OUTPUT_SAMPLERATE,
                                &inLayout,
                                static_cast<AVSampleFormat>(frame->getFrame()->format),
                                frame->getFrame()->sample_rate,
                                0, nullptr);

                        av_channel_layout_uninit(&inLayout);
                        av_channel_layout_uninit(&outLayout);

                        if (ret < 0 || mSwrCtx == nullptr) {
                            ALOGE("VEAudioDecoder failed to alloc resampling context: %d", ret);
                            return VE_UNKNOWN_ERROR;
                        }
                        if (swr_init(mSwrCtx) < 0) {
                            ALOGE("VEAudioDecoder Failed to initialize the resampling context");
                            swr_free(&mSwrCtx);
                            return VE_UNKNOWN_ERROR;
                        }
                    }

                    // 直接让 swr_convert 写进目标帧的缓冲：原先先转到临时
                    // 缓冲再 memcpy 过来，等于每帧多一次分配加一次全量拷贝
                    const int32_t out_nb_samples =
                            swr_get_out_samples(mSwrCtx, frame->getFrame()->nb_samples);
                    if (out_nb_samples <= 0) {
                        ALOGE("VEAudioDecoder swr_get_out_samples returned %d", out_nb_samples);
                        return VE_UNKNOWN_ERROR;
                    }
                    // 按上界分配，转换后再把 nb_samples 修正成实际值
                    std::shared_ptr<VEFrame> audioFrame = std::make_shared<VEFrame>(
                            AUDIO_TARGET_OUTPUT_SAMPLERATE, AUDIO_TARGET_OUTPUT_CHANNELS,
                            out_nb_samples, (int32_t) AUDIO_TARGET_OUTPUT_FORMAT);
                    // VEFrame 构造内 av_frame_get_buffer 失败会留下 mFrame=null 的半残对象
                    // (构造函数只 av_frame_free 不抛出)，必须判空
                    if (audioFrame->getFrame() == nullptr ||
                        audioFrame->getFrame()->data[0] == nullptr) {
                        ALOGE("VEAudioDecoder::%s resampled frame buffer alloc failed",
                              __FUNCTION__);
                        return VE_UNKNOWN_ERROR;
                    }

                    const int32_t out_samples_per_channel = swr_convert(
                            mSwrCtx, audioFrame->getFrame()->data, out_nb_samples,
                            (const uint8_t **) frame->getFrame()->data,
                            frame->getFrame()->nb_samples);
                    if (out_samples_per_channel < 0) {
                        ALOGE("VEAudioDecoder swr_convert failed");
                        return VE_UNKNOWN_ERROR;
                    }
                    ALOGF("VEAudioDecoder out_samples_per_channel:%d (cap %d)",
                          out_samples_per_channel, out_nb_samples);

                    audioFrame->getFrame()->pts = frame->getFrame()->pts;
                    audioFrame->getFrame()->pkt_dts = frame->getFrame()->pkt_dts;
                    audioFrame->getFrame()->nb_samples = out_samples_per_channel;
                    // 不改写 AVFrame->linesize[0]：SLES 读取(renderFrame)按
                    // nb_samples*channels*bytes_per_sample 取长，不读 linesize；
                    // 手动改成紧凑字节数会破坏"linesize 是对齐行距"不变量，
                    // 且对播放路径无任何作用。保留 av_frame_get_buffer 给出的对齐值。

                    audioFrame->setPts(audioFrame->getFrame()->pts);
                    audioFrame->setDts(audioFrame->getFrame()->pkt_dts);
                    audioFrame->setFrameType(E_FRAME_TYPE_AUDIO);
                    if (mPerfStats) {
                        mPerfStats->audioDecodeUs.add(mDecodeAccumUs + nowUs() - decodeBeginUs);
                        mDecodeAccumUs = 0;
                    }
                    queueFrame(audioFrame);
                } else {
                    // 解码输出已是目标格式，直通不重采样。pts 必须照样搬到
                    // VEFrame 上——渲染器给主时钟打点用的是 VEFrame::getPts()，
                    // 漏了这一步主时钟就恒被锚在 0：进度出负数、AVSync 误判
                    // 视频领先几百毫秒、画面变慢动作。
                    frame->setPts(frame->getFrame()->pts);
                    frame->setDts(frame->getFrame()->pkt_dts);
                    frame->setFrameType(E_FRAME_TYPE_AUDIO);
                    if (mPerfStats) {
                        mPerfStats->audioDecodeUs.add(mDecodeAccumUs + nowUs() - decodeBeginUs);
                        mDecodeAccumUs = 0;
                    }
                    queueFrame(frame);
                }
                ALOGF("VEAudioDecoder Audio frame: pts=%s, nb_samples=%d, channels=%d samplerate:%d format:%d\n",
                      av_ts2str(frame->getFrame()->pts),
                      frame->getFrame()->nb_samples,
                      frame->getFrame()->ch_layout.nb_channels,
                      frame->getFrame()->sample_rate,
                      frame->getFrame()->format);
                return VE_OK;
            }
            if (ret != AVERROR(EAGAIN)) {
                // 持续性错误(如 codec 未打开返回 EINVAL)：必须退出，
                // 否则循环条件不变化，忙循环卡死整个解码 looper
                ALOGE("VEAudioDecoder::onDecode receive_frame fatal: %d", ret);
                return VE_UNKNOWN_ERROR;
            }
        } while (ret != AVERROR(EAGAIN));

        std::shared_ptr<VEPacket> packet;
        ret = mDemux->read(ETrackType::AUDIO, packet);
        if (ret == VE_NOT_ENOUGH_DATA) {
            // 上游饥饿：登记一次性通知，数据入队时被唤醒(不再 10ms 轮询)。
            // 消息带当前 epoch，flush/seek 后自动作废；这是纯数据面事件，
            // 不触碰命令态。同时投一条兜底重试，防源实现漏发通知。
            ALOGV("VEAudioDecoder::onDecode starving, waiting for data notify");
            if (mPerfStats) {
                ++mPerfStats->audioStarve;
                // 只在"从不饥饿转饥饿"时记起点：饥饿期间可能被唤醒多次
                if (mStarveBeginUs == 0) { mStarveBeginUs = nowUs(); }
            }
            const int32_t starveGen = ++mStarveGen;
            auto notify = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
            notify->setInt32("epoch", mEpoch);
            notify->setInt32("starveGen", starveGen);
            mDemux->requestReadNotify(ETrackType::AUDIO, notify);
            auto backstop = std::make_shared<AMessage>(kWhatDecode, shared_from_this());
            backstop->setInt32("epoch", mEpoch);
            backstop->setInt32("starveGen", starveGen);
            backstop->post(kStarveBackstopUs);
            return VE_NOT_ENOUGH_DATA;
        }

        if (mPerfStats && mStarveBeginUs != 0) {
            mPerfStats->starveUs.add(nowUs() - mStarveBeginUs);
            mStarveBeginUs = 0;
        }
        if (packet == nullptr) {
            return VE_UNKNOWN_ERROR;
        }

        if (packet->getPacketType() == E_PACKET_TYPE_AUDIO) {
            const int64_t sendBeginUs = mPerfStats ? nowUs() : 0;
            ret = avcodec_send_packet(mAudioCtx, packet->getPacket());
            if (mPerfStats) {
                // 与视频同理：真正的解码工作在 send_packet 里，
                // 只量 receive 会把每帧成本报低一个数量级
                mDecodeAccumUs += nowUs() - sendBeginUs;
            }
            ALOGF("VEAudioDecoder::onDecode send packet pts:%" PRId64 ", dts:%" PRId64,
                  packet->getPacket()->pts, packet->getPacket()->dts);
        } else if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
            ret = avcodec_send_packet(mAudioCtx, nullptr);
        }

        if (ret == AVERROR(EAGAIN)) {
            // 解码器内部满了(极少见，上面已经排空过)：不算错误，下一轮继续收帧
            return VE_OK;
        }
        if (ret == AVERROR_INVALIDDATA && ++mSendErrorCount < kMaxConsecutiveSendErrors) {
            // 局部损坏的包：跳过继续解，别让单个坏包打死整个播放
            ALOGW("VEAudioDecoder::onDecode skip corrupt packet (%d in a row)", mSendErrorCount);
            return VE_OK;
        }
        if (ret < 0) {
            ALOGE("VEAudioDecoder fatal error sending packet for decoding %d", ret);
            return VE_UNKNOWN_ERROR;
        }
        mSendErrorCount = 0;
        return VE_OK;
    }

    VEResult VEAudioDecoder::onRelease() {
        mIsStarted = false;
        if (mAudioCtx) {
            avcodec_free_context(&mAudioCtx);
            mAudioCtx = nullptr;
        }
        if (mSwrCtx) {
            swr_free(&mSwrCtx);
            mSwrCtx = nullptr;
        }
        mDemux.reset();
        mSink.reset();
        mInFlightFrames = 0;
        mMediaInfo.reset();
        return VE_OK;
    }

    VEResult VEAudioDecoder::onStart() {
        if (mIsStarted) {
            ALOGI("VEAudioDecoder::onStart already started");
            return VE_OK;
        }
        mIsEOS = false;
        mIsStarted = true;
        postDecode();
        return VE_OK;
    }

    VEResult VEAudioDecoder::onStop() {
        mIsStarted = false;
        // 残留的精准 seek 目标会让下次重播把 0~target 的帧全部丢掉
        mSeekTargetUs = kNoSeekTarget;

        // prepare 失败(codec 缺失)或 release 之后仍可能收到 stop，须判空
        if (mAudioCtx) {
            avcodec_flush_buffers(mAudioCtx);
        }
        // 渲染器 stop 时清掉了自己的队列(不发回执)，credit 由本侧清算
        mInFlightFrames = 0;
        return VE_OK;
    }

    void VEAudioDecoder::queueFrame(std::shared_ptr<VEFrame> frame) {
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

    VEResult VEAudioDecoder::onPause() {
        mIsStarted = false;
        return 0;
    }

    VEResult VEAudioDecoder::pause() {
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        return 0;
    }



    VEResult VEAudioDecoder::seekTo(double timestamp) {
        std::shared_ptr<AMessage> msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp",timestamp);
        msg->post();
        return VE_OK;
    }

    VEResult VEAudioDecoder::postMessage(int32_t event, int32_t arg1, int32_t arg2, int64_t arg3,
                                         void *params) {
        std::shared_ptr<AMessage> msg = mNotifyEvent->dup();
        msg->setInt32("type",EComponentType::E_COMPONENT_TYPE_AUDIO_DECODER);
        msg->setInt32("event",event);
        msg->setInt32("arg1",arg1);
        msg->setInt32("arg2",arg2);
        msg->setInt64("arg3",arg3);
        msg->setPointer("params",params);
        msg->post();
        return 0;
    }
}