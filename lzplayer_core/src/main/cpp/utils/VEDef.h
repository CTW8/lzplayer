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


}

#endif //LZPLAYER_VEDEF_H
