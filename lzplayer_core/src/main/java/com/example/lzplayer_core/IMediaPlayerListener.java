package com.example.lzplayer_core;

public interface IMediaPlayerListener {
    int VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS                  = 0x101;
    int VE_PLAYER_NOTIFY_EVENT_ON_PREPARED                  = 0x102;
    int VE_PLAYER_NOTIFY_EVENT_ON_EOS                       = 0x103;
    int VE_PLAYER_NOTIFY_EVENT_ON_ERROR                     = 0x104;
    int VE_PLAYER_NOTIFY_EVENT_ON_INFO                      = 0x105;
    int VE_PLAYER_NOTIFY_EVENT_ON_FIRST_FRAME               = 0x106;
    int VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION                = 0x107;
    int VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE                 = 0x108;
    /** 音轨/字幕轨切换完成，msg1 = 新轨道号(-1 表示已关闭) */
    int VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED             = 0x109;
    /** 字幕到点显示，obj = 文本 */
    int VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE                  = 0x10A;
    /** 字幕到点清除 */
    int VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE_CLEAR            = 0x10B;
    /** 网络缓冲不足开始卡顿，msg1 = 已缓冲百分比(-1 未知) */
    int VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_START           = 0x10C;
    int VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_UPDATE          = 0x10D;
    int VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_END             = 0x10E;
}
