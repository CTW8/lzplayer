#include "VESubtitleTrack.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "utils/Log.h"
#include "VEDef.h"

namespace VE {
    namespace {
        /// 字幕包稀疏，取不到就慢慢等——这里不用登记-通知也无所谓，
        /// 500ms 的延迟对字幕不可感知，还省掉一套唤醒登记
        constexpr int64_t kFetchIdleUs = 500000;
        /// cue 队列上限，防异常文件把内存吃光
        constexpr size_t kMaxCues = 4096;
    }

    VESubtitleTrack::VESubtitleTrack(std::shared_ptr<AMessage> &notify,
                                     const std::shared_ptr<VEMediaClock> &clock)
            : mNotify(notify), mClock(clock) {
    }

    VESubtitleTrack::~VESubtitleTrack() {
        // 析构期不能投消息，直接同步清理
        if (mCtx) {
            avcodec_free_context(&mCtx);
            mCtx = nullptr;
        }
    }

    std::string VESubtitleTrack::stripAss(const std::string &raw) {
        // ASS 的 Dialogue 事件形如：
        //   Layer,Start,End,Style,Name,ML,MR,MV,Effect,正文
        // 前 9 个逗号分隔字段是元数据，正文里还可能夹 {\pos(...)} 之类样式标签。
        // 第一期只出纯文本，样式保真留给未来的 libass 方案。
        std::string text = raw;
        int commas = 0;
        size_t pos = 0;
        for (; pos < text.size() && commas < 9; ++pos) {
            if (text[pos] == ',') {
                ++commas;
            }
        }
        if (commas == 9) {
            text = text.substr(pos);
        }

        std::string out;
        out.reserve(text.size());
        bool inBrace = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '{') {
                inBrace = true;
                continue;
            }
            if (c == '}') {
                inBrace = false;
                continue;
            }
            if (inBrace) {
                continue;
            }
            // ASS 的换行转义
            if (c == '\\' && i + 1 < text.size() && (text[i + 1] == 'N' || text[i + 1] == 'n')) {
                out.push_back('\n');
                ++i;
                continue;
            }
            out.push_back(c);
        }
        return out;
    }

    // ---------------------------------------------------------------------

    VEResult VESubtitleTrack::prepare(const std::shared_ptr<IMediaSource> &source,
                                      const VETrackInfo &track) {
        auto msg = std::make_shared<AMessage>(kWhatInit, shared_from_this());
        if (source) {
            msg->setObject("source", source);
        }
        msg->setInt32("trackIndex", track.index);
        msg->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::setExternalCues(std::vector<Cue> cues) {
        auto msg = std::make_shared<AMessage>(kWhatExternal, shared_from_this());
        // 用 shared_ptr 搭载，避免 AMessage 里塞大对象
        auto holder = std::make_shared<std::vector<Cue>>(std::move(cues));
        msg->setObject("cues", holder);
        msg->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::setSpeed(float speed) {
        auto msg = std::make_shared<AMessage>(kWhatSetSpeed, shared_from_this());
        msg->setFloat("speed", speed);
        msg->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::start() {
        std::make_shared<AMessage>(kWhatStart, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::stop() {
        std::make_shared<AMessage>(kWhatStop, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::pause() {
        std::make_shared<AMessage>(kWhatPause, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::flush() {
        std::make_shared<AMessage>(kWhatFlush, shared_from_this())->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::seekTo(double timestampMs) {
        auto msg = std::make_shared<AMessage>(kWhatSeek, shared_from_this());
        msg->setDouble("timestamp", timestampMs);
        msg->post();
        return VE_OK;
    }

    VEResult VESubtitleTrack::release() {
        std::make_shared<AMessage>(kWhatRelease, shared_from_this())->post();
        return VE_OK;
    }

    void VESubtitleTrack::onMessageReceived(const std::shared_ptr<AMessage> &msg) {
        switch (msg->what()) {
            case kWhatInit:
                onPrepare(msg);
                break;
            case kWhatExternal: {
                std::shared_ptr<void> obj;
                if (msg->findObject("cues", &obj)) {
                    auto holder = std::static_pointer_cast<std::vector<Cue>>(obj);
                    mCues.assign(holder->begin(), holder->end());
                    mExternal = true;
                    ALOGI("VESubtitleTrack::%s %zu external cues loaded",
                          __FUNCTION__, mCues.size());
                    if (mIsStarted) {
                        scheduleNextCue();
                    }
                }
                break;
            }
            case kWhatStart:
                if (!mIsStarted) {
                    mIsStarted = true;
                    if (!mExternal) {
                        postFetch(0);
                    }
                    scheduleNextCue();
                }
                break;
            case kWhatPause:
                mIsStarted = false;
                // 停调度但保留 cue 队列：恢复时按当前时钟重排即可
                ++mEpoch;
                postNotifyDone(VE_NOTIFY_EVENT_PAUSE_DONE);
                break;
            case kWhatStop:
                mIsStarted = false;
                ++mEpoch;
                clearCues();
                postNotifyDone(VE_NOTIFY_EVENT_STOP_DONE);
                break;
            case kWhatFlush:
                ++mEpoch;
                clearCues();
                postNotifyDone(VE_NOTIFY_EVENT_FLUSH_DONE);
                break;
            case kWhatSeek:
                mIsStarted = false;
                ++mEpoch;
                clearCues();
                if (mCtx) {
                    avcodec_flush_buffers(mCtx);
                }
                postNotifyDone(VE_NOTIFY_EVENT_SEEK_DONE);
                break;
            case kWhatRelease:
                onRelease();
                postNotifyDone(VE_NOTIFY_EVENT_RELEASE_DONE);
                break;
            case kWhatSetSpeed: {
                float speed = 1.0f;
                msg->findFloat("speed", &speed);
                mSpeed = speed;
                // 在途定时器是按旧速率算的真实时长，必须重排
                ++mEpoch;
                if (mIsStarted) {
                    scheduleNextCue();
                }
                break;
            }
            case kWhatFetch: {
                int32_t epoch = 0;
                msg->findInt32("epoch", &epoch);
                if (epoch != mEpoch || !mIsStarted || mExternal) {
                    break;
                }
                onFetch();
                break;
            }
            case kWhatCueFire:
                onCueFire(msg);
                break;
            case kWhatCueClear:
                onCueClear(msg);
                break;
            default:
                break;
        }
    }

    VEResult VESubtitleTrack::onPrepare(const std::shared_ptr<AMessage> &msg) {
        std::shared_ptr<void> obj;
        if (msg->findObject("source", &obj)) {
            mSource = std::static_pointer_cast<IMediaSource>(obj);
        }
        if (mSource == nullptr) {
            mExternal = true;   // 外挂字幕，等 setExternalCues
            return VE_OK;
        }

        auto info = mSource->getFileInfo();
        const VETrackInfo *track = info ? info->subtitleTrack() : nullptr;
        if (track == nullptr || track->codecParams == nullptr) {
            ALOGE("VESubtitleTrack::%s no active subtitle track", __FUNCTION__);
            return VE_INVALID_PARAMS;
        }
        mTrack = *track;
        mTrack.codecParams = track->codecParams;   // 只借用，归 VEMediaInfo 所有

        const AVCodec *codec = avcodec_find_decoder(track->codecParams->codec_id);
        if (codec == nullptr) {
            ALOGE("VESubtitleTrack::%s decoder not found for codec %d",
                  __FUNCTION__, track->codecParams->codec_id);
            return VE_UNKNOWN_ERROR;
        }
        mCtx = avcodec_alloc_context3(codec);
        if (mCtx == nullptr) {
            return VE_NO_MEMORY;
        }
        if (avcodec_parameters_to_context(mCtx, track->codecParams) < 0 ||
            avcodec_open2(mCtx, codec, nullptr) != 0) {
            ALOGE("VESubtitleTrack::%s open codec failed", __FUNCTION__);
            avcodec_free_context(&mCtx);
            mCtx = nullptr;
            return VE_UNKNOWN_ERROR;
        }
        ALOGI("VESubtitleTrack::%s ready, codec %d", __FUNCTION__,
              track->codecParams->codec_id);
        return VE_OK;
    }

    VEResult VESubtitleTrack::onRelease() {
        mIsStarted = false;
        ++mEpoch;
        clearCues();
        if (mCtx) {
            avcodec_free_context(&mCtx);
            mCtx = nullptr;
        }
        mSource.reset();
        return VE_OK;
    }

    void VESubtitleTrack::onFetch() {
        if (mSource == nullptr || mCtx == nullptr) {
            return;
        }
        // 一轮多取几条，字幕包稀疏，一次抓够能少醒几次
        for (int i = 0; i < 8 && mCues.size() < kMaxCues; ++i) {
            std::shared_ptr<VEPacket> packet;
            const VEResult ret = mSource->read(ETrackType::SUBTITLE, packet);
            if (ret != VE_OK || packet == nullptr) {
                break;
            }
            if (packet->getPacketType() == E_PACKET_TYPE_EOF) {
                break;
            }
            decodePacket(packet);
        }
        scheduleNextCue();
        postFetch(kFetchIdleUs);
    }

    void VESubtitleTrack::decodePacket(const std::shared_ptr<VEPacket> &packet) {
        AVSubtitle sub;
        memset(&sub, 0, sizeof(sub));
        int got = 0;
        // 字幕走独立的解码 API，不是 send/receive 那一套
        const int ret = avcodec_decode_subtitle2(mCtx, &sub, &got, packet->getPacket());
        if (ret < 0 || !got) {
            return;
        }

        const int64_t basePts = packet->getPts();
        const int64_t startUs = basePts + static_cast<int64_t>(sub.start_display_time) * 1000;
        int64_t endUs = basePts + static_cast<int64_t>(sub.end_display_time) * 1000;
        if (endUs <= startUs) {
            // 有些容器不给 end_display_time，用包时长兜底，再不行给 3 秒
            const int64_t dur = packet->getDurationUs() > 0 ? packet->getDurationUs() : 3000000;
            endUs = startUs + dur;
        }

        for (unsigned i = 0; i < sub.num_rects; ++i) {
            const AVSubtitleRect *rect = sub.rects[i];
            if (rect == nullptr) {
                continue;
            }
            std::string text;
            if (rect->type == SUBTITLE_ASS && rect->ass) {
                text = stripAss(rect->ass);
            } else if (rect->type == SUBTITLE_TEXT && rect->text) {
                text = rect->text;
            } else {
                continue;   // 位图字幕本期不支持(demux 侧已过滤，这里兜底)
            }
            if (text.empty()) {
                continue;
            }
            mCues.push_back({startUs, endUs, text});
        }
        avsubtitle_free(&sub);
    }

    void VESubtitleTrack::scheduleNextCue() {
        if (!mIsStarted || mCues.empty() || mClock == nullptr) {
            return;
        }
        const int64_t nowUs = static_cast<int64_t>(mClock->getCurrentMediaTime());

        // 丢掉已经过期的 cue(seek 到后面、或调度落后时)
        while (!mCues.empty() && mCues.front().endUs <= nowUs) {
            mCues.pop_front();
        }
        if (mCues.empty()) {
            return;
        }

        const Cue &cue = mCues.front();
        // 媒体时间差 → 真实等待时间：时钟按 speed 倍外推，
        // 所以等待要除以 speed(与视频侧 getWaitTime 同一道理)
        const double speed = (mSpeed > 0) ? mSpeed : 1.0;
        const int64_t delayUs = (cue.startUs > nowUs)
                                ? static_cast<int64_t>((cue.startUs - nowUs) / speed) : 0;

        auto msg = std::make_shared<AMessage>(kWhatCueFire, shared_from_this());
        msg->setInt32("epoch", mEpoch);
        msg->post(delayUs);
    }

    void VESubtitleTrack::onCueFire(const std::shared_ptr<AMessage> &msg) {
        int32_t epoch = 0;
        msg->findInt32("epoch", &epoch);
        if (epoch != mEpoch || !mIsStarted || mCues.empty()) {
            return;
        }
        const Cue cue = mCues.front();
        mCues.pop_front();

        postNotify(VE_NOTIFY_EVENT_SUBTITLE, cue.text);
        mShowing = true;

        // 到点清除
        const int64_t nowUs = mClock ? static_cast<int64_t>(mClock->getCurrentMediaTime()) : 0;
        const double speed = (mSpeed > 0) ? mSpeed : 1.0;
        const int64_t holdUs = (cue.endUs > nowUs)
                               ? static_cast<int64_t>((cue.endUs - nowUs) / speed) : 0;
        auto clearMsg = std::make_shared<AMessage>(kWhatCueClear, shared_from_this());
        clearMsg->setInt32("epoch", mEpoch);
        clearMsg->post(holdUs);
    }

    void VESubtitleTrack::onCueClear(const std::shared_ptr<AMessage> &msg) {
        int32_t epoch = 0;
        msg->findInt32("epoch", &epoch);
        if (epoch != mEpoch) {
            return;
        }
        if (mShowing) {
            postNotify(VE_NOTIFY_EVENT_SUBTITLE_CLEAR, "");
            mShowing = false;
        }
        scheduleNextCue();
    }

    void VESubtitleTrack::clearCues() {
        mCues.clear();
        if (mShowing) {
            // 队列清空时屏幕上可能还挂着一条，必须显式清掉
            postNotify(VE_NOTIFY_EVENT_SUBTITLE_CLEAR, "");
            mShowing = false;
        }
    }

    void VESubtitleTrack::postFetch(int64_t delayUs) {
        auto msg = std::make_shared<AMessage>(kWhatFetch, shared_from_this());
        msg->setInt32("epoch", mEpoch);
        msg->post(delayUs);
    }

    void VESubtitleTrack::postNotify(int32_t event, const std::string &text) {
        auto msg = mNotify->dup();
        msg->setInt32("type", EComponentType::E_COMPONENT_TYPE_SUBTITLE);
        msg->setInt32("event", event);
        msg->setInt32("arg1", 0);
        msg->setInt32("arg2", 0);
        msg->setInt64("arg3", 0);
        msg->setString("text", text);
        msg->post();
    }

    void VESubtitleTrack::postNotifyDone(int32_t event) {
        auto msg = mNotify->dup();
        msg->setInt32("type", EComponentType::E_COMPONENT_TYPE_SUBTITLE);
        msg->setInt32("event", event);
        msg->setInt32("arg1", 0);
        msg->setInt32("arg2", 0);
        msg->setInt64("arg3", 0);
        msg->post();
    }
}
