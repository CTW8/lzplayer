//
// Created by 李振 on 2024/8/15.
//

#ifndef LZPLAYER_VEDEF_H
#define LZPLAYER_VEDEF_H
namespace VE {

#define VE_PLAYER_NOTIFY_EVENT            0x100
#define VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS                  (VE_PLAYER_NOTIFY_EVENT + 1)
#define VE_PLAYER_NOTIFY_EVENT_ON_PREPARED                  (VE_PLAYER_NOTIFY_EVENT + 2)
#define VE_PLAYER_NOTIFY_EVENT_ON_EOS                       (VE_PLAYER_NOTIFY_EVENT + 3)
#define VE_PLAYER_NOTIFY_EVENT_ON_ERROR                     (VE_PLAYER_NOTIFY_EVENT + 4)
#define VE_PLAYER_NOTIFY_EVENT_ON_INFO                      (VE_PLAYER_NOTIFY_EVENT + 5)
#define VE_PLAYER_NOTIFY_EVENT_ON_FIRST_FRAME               (VE_PLAYER_NOTIFY_EVENT + 6)
#define VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION                (VE_PLAYER_NOTIFY_EVENT + 7)
#define VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE                 (VE_PLAYER_NOTIFY_EVENT + 8)


#define VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED             (VE_PLAYER_NOTIFY_EVENT + 9)
#define VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE                  (VE_PLAYER_NOTIFY_EVENT + 10)
#define VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE_CLEAR            (VE_PLAYER_NOTIFY_EVENT + 11)
#define VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_START           (VE_PLAYER_NOTIFY_EVENT + 12)
#define VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_UPDATE          (VE_PLAYER_NOTIFY_EVENT + 13)
#define VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_END             (VE_PLAYER_NOTIFY_EVENT + 14)


#define VE_PLAYER_ERROR                         0x2000
#define VE_PLAYER_ERROR_OPEN_DEMUX_FAILED                   (VE_PLAYER_ERROR + 1)
#define VE_PLAYER_ERROR_NETWORK_IO                          (VE_PLAYER_ERROR + 2)
#define VE_PLAYER_ERROR_NETWORK_TIMEOUT                     (VE_PLAYER_ERROR + 3)
#define VE_PLAYER_ERROR_UNSUPPORTED_TRACK                   (VE_PLAYER_ERROR + 4)
/// 到达流尾却一帧未出、时钟也从未推进 —— 文件能解析出流信息但没有可播的
/// 媒体数据(实测：截断的 mp4，free 盒里残留着一份旧 moov，于是流信息解析
/// 成功、mdat 却不在文件里)。
/// 这类文件原先会走"正常播放完成"，上层**分不清"文件是坏的"与"播完了"**。
#define VE_PLAYER_ERROR_NO_MEDIA_OUTPUT                     (VE_PLAYER_ERROR + 5)

/// 信息类(非错误)：硬解失败已回退软解，链路继续可用
#define VE_PLAYER_INFO                          0x3000
#define VE_INFO_DECODER_FALLBACK                            (VE_PLAYER_INFO + 1)


    enum EFrameType {
        E_FRAME_TYPE_UNKNOW = -1,
        E_FRAME_TYPE_VIDEO,
        E_FRAME_TYPE_AUDIO,
        E_FRAME_TYPE_EOF
    };

    enum EPacketType {
        E_PACKET_TYPE_UNKNOW = -1,
        E_PACKET_TYPE_VIDEO,
        E_PACKET_TYPE_AUDIO,
        E_PACKET_TYPE_EOF,
        E_PACKET_TYPE_SUBTITLE
    };

    enum EComponentType{
        E_COMPONENT_TYPE_UNKNOW = -1,
        E_COMPONENT_TYPE_VIDEO_RENDER = 0,
        E_COMPONENT_TYPE_AUDIO_RENDER,
        E_COMPONENT_TYPE_VIDEO_DECODER,
        E_COMPONENT_TYPE_AUDIO_DECODER,
        E_COMPONENT_TYPE_DEMUX,
        E_COMPONENT_TYPE_SUBTITLE
    };

#define VE_NOTIFY_EVENT_UNKNOW                       -100
#define VE_NOTIFY_EVENT_SEEK_DONE                    100
#define VE_NOTIFY_EVENT_OPEN_DONE                    101
#define VE_NOTIFY_EVENT_STOP_DONE                    102
#define VE_NOTIFY_EVENT_FLUSH_DOING                  103
#define VE_NOTIFY_EVENT_FLUSH_DONE                   104
#define VE_NOTIFY_EVENT_EOS                          105
#define VE_NOTIFY_EVENT_PROGRESS                     106
#define VE_NOTIFY_EVENT_ERROR                        107
/// 组件已停止消费数据(pause 命令在组件自己的 looper 上处理完毕)
#define VE_NOTIFY_EVENT_PAUSE_DONE                   108
/// seek 后渲染出的第一帧，用于判定 seek 真正完成
#define VE_NOTIFY_EVENT_FIRST_FRAME                  109
/// demux 异步 prepare 完成(arg1 = 结果码)
#define VE_NOTIFY_EVENT_PREPARE_DONE                 111
/// 组件已在自己的线程上释放完资源(编解码器上下文/EGL/SLES)，
/// 收齐后才能停掉它的 looper
#define VE_NOTIFY_EVENT_RELEASE_DONE                 110
/// 源已切换活跃轨道(arg1 = 结果码)
#define VE_NOTIFY_EVENT_SELECT_TRACK_DONE            112
/// 字幕 cue 到点显示(params = const char* 文本) / 到点清除
#define VE_NOTIFY_EVENT_SUBTITLE                     113
#define VE_NOTIFY_EVENT_SUBTITLE_CLEAR               114
/// 网络源缓冲水位事件(BUFFERING_UPDATE 的 arg1 = 百分比)
#define VE_NOTIFY_EVENT_BUFFERING_START              115
#define VE_NOTIFY_EVENT_BUFFERING_UPDATE             116
#define VE_NOTIFY_EVENT_BUFFERING_END                117
/// 音频渲染已按新速率就位(sonic 重建 + 设备旧速率 PCM 清空之后)。
/// **这才是变速的"生效"时刻** —— VEAudioRender::setSpeed 只投消息就返回,
/// 在调用点收口量到的是命令下发(实测 0.2ms), 与生效无关。
#define VE_NOTIFY_EVENT_SPEED_APPLIED                118

}

#endif //LZPLAYER_VEDEF_H
