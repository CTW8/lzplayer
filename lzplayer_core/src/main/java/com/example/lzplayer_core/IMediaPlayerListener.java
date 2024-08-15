package com.example.lzplayer_core;

public interface IMediaPlayerListener {
    int VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS                  = 0x101;
    int VE_PLAYER_NOTIFY_EVENT_ON_PREPARED                  = 0x102;
    int VE_PLAYER_NOTIFY_EVENT_ON_EOS                       = 0x103;
    int VE_PLAYER_NOTIFY_EVENT_ON_ERROR                     = 0x104;
    int VE_PLAYER_NOTIFY_EVENT_ON_INFO                      = 0x105;
    int VE_PLAYER_NOTIFY_EVENT_ON_FIRST_FRAME               = 0x106;
}
