#ifndef     __VE_MEDIA_DEF__
#define     __VE_MEDIA_DEF__
#include <string>
#include <vector>
extern "C"{
#include"libavcodec/avcodec.h"
#include"libavformat/avformat.h"
}
namespace VE {

    /// 轨道类型。read()/selectTrack() 都按它区分链路。
    enum class ETrackType {
        AUDIO,
        VIDEO,
        SUBTITLE,
    };

    /// 外挂字幕的虚拟轨道号从这里起，与容器内流的轨道号区分开
    static const int kExternalTrackIndexBase = 0x10000;

    /// 单条轨道的描述。codecParams 由所属 VEMediaInfo 深拷贝自持——
    /// 源(demux)释放后本结构仍可安全使用，解码器不会拿到悬垂指针。
    struct VETrackInfo {
        int         index = -1;         ///< 播放器轨道号(对外 selectTrack 用)
        int         streamIndex = -1;   ///< FFmpeg stream index；虚拟轨为 -1
        ETrackType  type = ETrackType::AUDIO;
        AVCodecID   codecId = AV_CODEC_ID_NONE;
        std::string lang;               ///< metadata "language"
        std::string title;              ///< metadata "title"
        AVCodecParameters *codecParams = nullptr;  ///< 本结构所属 VEMediaInfo 持有
        AVRational  timeBase = {0, 1};
        int64_t     startTime = 0;

        // —— 视频轨 ——
        int width = 0;
        int height = 0;
        int fps = 0;
        /// 容器 display matrix 解出的旋转角(0/90/180/270)，渲染时应用
        int rotationDegrees = 0;

        // —— 音频轨 ——
        int sampleRate = 0;
        int channels = 0;
        int sampleFormat = 0;
    };

    /// 媒体信息：轨道列表 + 当前活跃轨道。
    /// 生命周期独立于源：codecParams 全部是深拷贝，析构时统一释放，
    /// 因此 demux 已经 release 之后本对象仍然完整可用。
    struct VEMediaInfo {
        std::vector<VETrackInfo> tracks;

        /// 活跃轨道在 tracks 中的下标(不是 track index)，-1 表示无
        int activeAudio = -1;
        int activeVideo = -1;
        int activeSubtitle = -1;

        uint64_t duration = 0;   ///< 毫秒

        VEMediaInfo() = default;

        ~VEMediaInfo() {
            for (auto &t : tracks) {
                if (t.codecParams) {
                    avcodec_parameters_free(&t.codecParams);
                    t.codecParams = nullptr;
                }
            }
        }

        // 深拷贝语义复杂且无使用场景，直接禁掉，一律经 shared_ptr 传递
        VEMediaInfo(const VEMediaInfo &) = delete;
        VEMediaInfo &operator=(const VEMediaInfo &) = delete;

        const VETrackInfo *trackAt(int slot) const {
            return (slot >= 0 && slot < static_cast<int>(tracks.size()))
                   ? &tracks[slot] : nullptr;
        }
        const VETrackInfo *audioTrack() const { return trackAt(activeAudio); }
        const VETrackInfo *videoTrack() const { return trackAt(activeVideo); }
        const VETrackInfo *subtitleTrack() const { return trackAt(activeSubtitle); }

        /// 按对外轨道号查找 tracks 下标，找不到返回 -1
        int slotOfTrackIndex(int trackIndex) const {
            for (size_t i = 0; i < tracks.size(); ++i) {
                if (tracks[i].index == trackIndex) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        bool hasAudio() const { return activeAudio >= 0; }
        bool hasVideo() const { return activeVideo >= 0; }

        // —— 活跃轨道的常用参数，调用方不必每次判空取轨 ——
        int width()  const { const auto *t = videoTrack(); return t ? t->width  : 0; }
        int height() const { const auto *t = videoTrack(); return t ? t->height : 0; }
        int fps()    const { const auto *t = videoTrack(); return t ? t->fps    : 0; }
        int sampleRate() const { const auto *t = audioTrack(); return t ? t->sampleRate : 0; }
        int channels()   const { const auto *t = audioTrack(); return t ? t->channels   : 0; }
        int rotationDegrees() const {
            const auto *t = videoTrack(); return t ? t->rotationDegrees : 0;
        }
    };
}

#endif
