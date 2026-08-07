//
// Created by 李振 on 2024/7/25.
//

#ifndef LZPLAYER_VEPLAYERDRIVER_H
#define LZPLAYER_VEPLAYERDRIVER_H

#include "VEPlayer.h"
#include <android/native_window_jni.h>
#include <chrono>
namespace VE {
    class MediaPlayerListener {
    public:
        virtual void notify(int msg, int ext1, double ext2, const void *obj) = 0;
    };

    class VEPlayerDriver {
    public:
        VEPlayerDriver();

        ~VEPlayerDriver();

        VEResult setDataSource(std::string path);

        VEResult setSurface(ANativeWindow *win, int width, int height);

        VEResult prepare();

        VEResult prepareAsync();

        VEResult start();

        VEResult stop();

        VEResult pause();

        VEResult seekTo(double timestampMs);

        VEResult reset();

        int64_t getDuration();

        /// 当前播放位置(毫秒)
        int64_t getCurrentPosition();

        VEResult setLooping(bool looping);

        VEResult setSpeedRate(float speed);

        /// 轨道列表(JSON)。PREPARED 之后才有内容。
        std::string getTrackInfo();

        /// 切换/关闭轨道。PREPARED/STARTED/PAUSED/COMPLETE 状态下可调。
        VEResult selectTrack(int trackIndex);
        VEResult deselectTrack(int trackIndex);

        /// 加载外挂字幕文件
        VEResult addExternalSubtitle(const std::string &path);

        /// 运行期统计快照(JSON)，诊断面板用
        std::string getStats();

        /// 测试开关：下次 prepare 生效
        VEResult setForceSoftwareDecoder(bool force);
        VEResult setForceSlesAudio(bool force);

        VEResult setListener(std::shared_ptr<MediaPlayerListener> listener);

    private:
        enum media_player_states {
            MEDIA_PLAYER_STATE_ERROR = 0,
            MEDIA_PLAYER_IDLE = 1 << 0,
            MEDIA_PLAYER_INITIALIZED = 1 << 1,
            MEDIA_PLAYER_PREPARING = 1 << 2,
            MEDIA_PLAYER_PREPARED = 1 << 3,
            MEDIA_PLAYER_STARTED = 1 << 4,
            MEDIA_PLAYER_PAUSED = 1 << 5,
            MEDIA_PLAYER_STOPPED = 1 << 6,
            MEDIA_PLAYER_PLAYBACK_COMPLETE = 1 << 7
        };

        int currentState;
        std::shared_ptr<ALooper> mPlayerLooper = nullptr;
        std::shared_ptr<VEPlayer> mPlayer;
        std::shared_ptr<MediaPlayerListener> mListener;

        bool mEnableLooping = false;

        bool mIsSeeking = false;

        std::mutex mMutex;
        std::condition_variable mCond;

        void notifyListener(int msg, int ext1, double ext2, const void *obj);
    };

}
#endif //LZPLAYER_VEPLAYERDRIVER_H
