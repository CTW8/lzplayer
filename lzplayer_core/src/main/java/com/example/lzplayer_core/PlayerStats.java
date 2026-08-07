package com.example.lzplayer_core;

import android.util.Log;

import org.json.JSONObject;

/**
 * 播放器运行期统计快照。由 {@link VEPlayer#getStats()} 返回。
 *
 * <p>这些数字平时只存在于 logcat 里；测试台把它们前置成读数，
 * 用来判断能力是否真的生效（走的硬解还是软解、时钟偏了多少、缓冲还剩几秒）。
 */
public class PlayerStats {
    private static final String TAG = "PlayerStats";

    /** IDLE / PREPARING / PREPARED / STARTED / PAUSED / SEEKING / COMPLETED / RELEASING / ERROR */
    public final String state;
    /** hardware / software / "-"（无视频轨） */
    public final String decoder;
    /** 视频编码名，如 h264 / hevc */
    public final String codec;
    /** AAudio / OpenSL ES / none */
    public final String audioBackend;
    /** 视频 pts 减主时钟，毫秒。正数表示视频领先 */
    public final int avOffsetMs;
    public final long renderedFrames;
    public final long droppedFrames;
    /** 待解码的包数 */
    public final int audioQueue;
    public final int videoQueue;
    /** 已缓冲时长（两路取短板），毫秒 */
    public final long bufferedMs;
    /** local / network / none */
    public final String source;
    public final float speed;
    /** 是否正处于网络缓冲导致的内部暂停 */
    public final boolean buffering;
    public final long positionMs;
    public final long durationMs;

    private PlayerStats(String state, String decoder, String codec, String audioBackend,
                        int avOffsetMs, long renderedFrames, long droppedFrames,
                        int audioQueue, int videoQueue, long bufferedMs,
                        String source, float speed, boolean buffering,
                        long positionMs, long durationMs) {
        this.state = state;
        this.decoder = decoder;
        this.codec = codec;
        this.audioBackend = audioBackend;
        this.avOffsetMs = avOffsetMs;
        this.renderedFrames = renderedFrames;
        this.droppedFrames = droppedFrames;
        this.audioQueue = audioQueue;
        this.videoQueue = videoQueue;
        this.bufferedMs = bufferedMs;
        this.source = source;
        this.speed = speed;
        this.buffering = buffering;
        this.positionMs = positionMs;
        this.durationMs = durationMs;
    }

    public boolean isHardwareDecoder() { return "hardware".equals(decoder); }
    public boolean isNetworkSource()   { return "network".equals(source); }

    /** 播放器未就绪时的占位值，UI 可以无脑用它初始化读数 */
    public static PlayerStats empty() {
        return new PlayerStats("IDLE", "-", "-", "none", 0, 0, 0, 0, 0, 0,
                "none", 1.0f, false, 0, 0);
    }

    static PlayerStats parse(String json) {
        if (json == null || json.isEmpty()) {
            return empty();
        }
        try {
            JSONObject o = new JSONObject(json);
            return new PlayerStats(
                    o.optString("state", "IDLE"),
                    o.optString("decoder", "-"),
                    o.optString("codec", "-"),
                    o.optString("audioBackend", "none"),
                    o.optInt("avOffsetMs", 0),
                    o.optLong("renderedFrames", 0),
                    o.optLong("droppedFrames", 0),
                    o.optInt("audioQueue", 0),
                    o.optInt("videoQueue", 0),
                    o.optLong("bufferedMs", 0),
                    o.optString("source", "none"),
                    (float) o.optDouble("speed", 1.0),
                    o.optBoolean("buffering", false),
                    o.optLong("positionMs", 0),
                    o.optLong("durationMs", 0));
        } catch (Exception e) {
            Log.e(TAG, "parse stats failed: " + e.getMessage());
            return empty();
        }
    }
}
