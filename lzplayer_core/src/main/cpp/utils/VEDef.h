//
// Created by 李振 on 2024/8/15.
//

#ifndef LZPLAYER_VEDEF_H
#define LZPLAYER_VEDEF_H


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

enum MediaPlayerSeekMode{
    SEEK_PREVIOUS_SYNC = 0,
    SEEK_NEXT_SYNC = 1,
    SEEK_CLOSEST_SYNC = 2,
    SEEK_CLOSEST = 3,
    SEEK_FRAME_INDEX = 4,
};

enum EFrameType{
    E_FRAME_TYPE_UNKNOW = -1,
    E_FRAME_TYPE_VIDEO,
    E_FRAME_TYPE_AUDIO,
    E_FRAME_TYPE_EOF
};

enum EPacketType{
    E_PACKET_TYPE_UNKNOW = -1,
    E_PACKET_TYPE_VIDEO,
    E_PACKET_TYPE_AUDIO,
    E_PACKET_TYPE_EOF
};

enum media_event_type {
    MEDIA_NOP               = 0, // interface test message
    MEDIA_PREPARED          = 1,
    MEDIA_PLAYBACK_COMPLETE = 2,
    MEDIA_BUFFERING_UPDATE  = 3,
    MEDIA_SEEK_COMPLETE     = 4,
    MEDIA_SET_VIDEO_SIZE    = 5,
    MEDIA_STARTED           = 6,
    MEDIA_PAUSED            = 7,
    MEDIA_STOPPED           = 8,
    MEDIA_SKIPPED           = 9,
    MEDIA_NOTIFY_TIME       = 98,
    MEDIA_TIMED_TEXT        = 99,
    MEDIA_ERROR             = 100,
    MEDIA_INFO              = 200,
    MEDIA_SUBTITLE_DATA     = 201,
    MEDIA_META_DATA         = 202,
    MEDIA_DRM_INFO          = 210,
    MEDIA_TIME_DISCONTINUITY = 211,
    MEDIA_IMS_RX_NOTICE     = 300,
    MEDIA_AUDIO_ROUTING_CHANGED = 10000,
};

enum media_info_type {
    // 0xx
    MEDIA_INFO_UNKNOWN = 1,
    // The player was started because it was used as the next player for another
    // player, which just completed playback
    MEDIA_INFO_STARTED_AS_NEXT = 2,
    // The player just pushed the very first video frame for rendering
    MEDIA_INFO_RENDERING_START = 3,
    // 7xx
    // The video is too complex for the decoder: it can't decode frames fast
    // enough. Possibly only the audio plays fine at this stage.
    MEDIA_INFO_VIDEO_TRACK_LAGGING = 700,
    // MediaPlayer is temporarily pausing playback internally in order to
    // buffer more data.
    MEDIA_INFO_BUFFERING_START = 701,
    // MediaPlayer is resuming playback after filling buffers.
    MEDIA_INFO_BUFFERING_END = 702,
    // Bandwidth in recent past
    MEDIA_INFO_NETWORK_BANDWIDTH = 703,

    // 8xx
    // Bad interleaving means that a media has been improperly interleaved or not
    // interleaved at all, e.g has all the video samples first then all the audio
    // ones. Video is playing but a lot of disk seek may be happening.
    MEDIA_INFO_BAD_INTERLEAVING = 800,
    // The media is not seekable (e.g live stream).
    MEDIA_INFO_NOT_SEEKABLE = 801,
    // New media metadata is available.
    MEDIA_INFO_METADATA_UPDATE = 802,
    // Audio can not be played.
    MEDIA_INFO_PLAY_AUDIO_ERROR = 804,
    // Video can not be played.
    MEDIA_INFO_PLAY_VIDEO_ERROR = 805,

    //9xx
    MEDIA_INFO_TIMED_TEXT_ERROR = 900,
};


#endif //LZPLAYER_VEDEF_H
