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


#define VE_PLAYER_ERROR                         0x2000
#define VE_PLAYER_ERROR_OPEN_DEMUX_FAILED                   (VE_PLAYER_ERROR + 1)


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
        E_PACKET_TYPE_EOF
    };

    enum EComponentType{
        E_COMPONENT_TYPE_UNKNOW = -1,
        E_COMPONENT_TYPE_VIDEO_RENDER = 0,
        E_COMPONENT_TYPE_AUDIO_RENDER,
        E_COMPONENT_TYPE_VIDEO_DECODER,
        E_COMPONENT_TYPE_AUDIO_DECODER,
        E_COMPONENT_TYPE_DEMUX
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

}

#endif //LZPLAYER_VEDEF_H
