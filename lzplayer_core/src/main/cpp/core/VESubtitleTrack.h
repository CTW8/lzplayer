#ifndef LZPLAYER_VESUBTITLETRACK_H
#define LZPLAYER_VESUBTITLETRACK_H

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "IMediaSource.h"
#include "IVEComponent.h"
#include "VEMediaClock.h"
#include "VEMediaDef.h"
#include "thread/AHandler.h"
#include "thread/AMessage.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

namespace VE {

    /// 字幕轨：解析 cue 并按主时钟调度显示/清除。
    ///
    /// 走的是"native 解析调度 + Java 层显示"(对齐 MediaPlayer TimedText)，
    /// 不在 GL 里画字：跟随主时钟调度天然把暂停/seek/变速都处理对了，
    /// UI 侧也能自由排版，还省掉 libass 依赖。
    ///
    /// 与解码器不同，字幕**不走 IFrameSink/credit 流控**——cue 稀疏
    /// (一行一条)，用帧链路那套是杀鸡用牛刀。但命令面完全对齐
    /// IVEComponent，因此它在 Role 表里就是一个普通角色，seek/teardown
    /// 的分阶段握手不需要为它开任何特例。
    class VESubtitleTrack : public AHandler, public IVEComponent {
    public:
        VESubtitleTrack(std::shared_ptr<AMessage> &notify,
                        const std::shared_ptr<VEMediaClock> &clock);
        ~VESubtitleTrack() override;

        /// source 为空时表示"只播外挂 cue"(见 setExternalCues)
        VEResult prepare(const std::shared_ptr<IMediaSource> &source,
                         const VETrackInfo &track);

        /// 外挂字幕：cue 已在别处一次性解析完，这里直接消费内存列表
        struct Cue {
            int64_t startUs;
            int64_t endUs;
            std::string text;

            // C++11 下带默认成员初始化器的结构体不是聚合类型，
            // 花括号初始化会失败，所以显式给构造函数
            Cue() : startUs(0), endUs(0) {}
            Cue(int64_t s, int64_t e, std::string t)
                    : startUs(s), endUs(e), text(std::move(t)) {}
        };
        VEResult setExternalCues(std::vector<Cue> cues);

        /// 播放速率变化：在途的定时器要按新速率重排
        VEResult setSpeed(float speed);

        // IVEComponent
        VEResult start() override;
        VEResult stop() override;
        VEResult pause() override;
        VEResult seekTo(double timestampMs) override;
        VEResult flush() override;
        VEResult release() override;

        /// 把 ASS 的 Dialogue 行剥成纯文本(去掉 {\\...} 样式标签与字段前缀)
        static std::string stripAss(const std::string &raw);

    private:
        void onMessageReceived(const std::shared_ptr<AMessage> &msg) override;

        VEResult onPrepare(const std::shared_ptr<AMessage> &msg);
        VEResult onRelease();
        void onFetch();
        void onCueFire(const std::shared_ptr<AMessage> &msg);
        void onCueClear(const std::shared_ptr<AMessage> &msg);

        /// 解一个字幕包，把其中的 cue 追加进队列
        void decodePacket(const std::shared_ptr<VEPacket> &packet);
        /// 按主时钟给队首 cue 排一个定时器(一次只挂一个)
        void scheduleNextCue();
        void clearCues();
        void postFetch(int64_t delayUs);
        void postNotify(int32_t event, const std::string &text);
        /// 命令回执(不带文本)，走与其它组件相同的 *_DONE 事件
        void postNotifyDone(int32_t event);

        std::shared_ptr<AMessage> mNotify;
        std::shared_ptr<VEMediaClock> mClock;
        std::shared_ptr<IMediaSource> mSource;
        AVCodecContext *mCtx = nullptr;
        VETrackInfo mTrack;

        std::deque<Cue> mCues;
        bool mIsStarted = false;
        bool mExternal = false;
        bool mShowing = false;
        float mSpeed = 1.0f;
        /// 定时器与 fetch 消息的代次：seek/flush 递增即作废在途消息
        int32_t mEpoch = 0;

        enum {
            kWhatInit = 'init',
            kWhatStart = 'star',
            kWhatStop = 'stop',
            kWhatPause = 'paus',
            kWhatFlush = 'flus',
            kWhatSeek = 'seek',
            kWhatRelease = 'rele',
            kWhatFetch = 'ftch',
            kWhatCueFire = 'cfir',
            kWhatCueClear = 'cclr',
            kWhatSetSpeed = 'sspd',
            kWhatExternal = 'extc',
        };
    };
}

#endif //LZPLAYER_VESUBTITLETRACK_H
