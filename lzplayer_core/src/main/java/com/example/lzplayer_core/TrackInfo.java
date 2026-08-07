package com.example.lzplayer_core;

import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * 一条媒体轨道的描述。由 {@link VEPlayer#getTrackInfo()} 返回，
 * index 用于 {@link VEPlayer#selectTrack(int)}。
 */
public class TrackInfo {
    private static final String TAG = "TrackInfo";

    public static final String TYPE_AUDIO = "audio";
    public static final String TYPE_VIDEO = "video";
    public static final String TYPE_SUBTITLE = "subtitle";

    /** 轨道号；外挂字幕的号 >= 0x10000 */
    public final int index;
    /** audio / video / subtitle */
    public final String type;
    /** 语言标签(容器 metadata)，可能为空 */
    public final String language;
    /** 轨道标题，可能为空 */
    public final String title;
    /** FFmpeg 的 AVCodecID 数值 */
    public final int codecId;
    /** 编码名，如 aac / ac3 / h264 */
    public final String codecName;
    /** 音频轨：采样率与声道数；其它轨为 0 */
    public final int sampleRate;
    public final int channels;
    /** 视频轨：分辨率与容器标注的旋转角；其它轨为 0 */
    public final int width;
    public final int height;
    public final int rotation;
    /** 是否为当前活跃轨道 */
    public final boolean active;

    TrackInfo(int index, String type, String language, String title,
              int codecId, String codecName, int sampleRate, int channels,
              int width, int height, int rotation, boolean active) {
        this.index = index;
        this.type = type;
        this.language = language;
        this.title = title;
        this.codecId = codecId;
        this.codecName = codecName;
        this.sampleRate = sampleRate;
        this.channels = channels;
        this.width = width;
        this.height = height;
        this.rotation = rotation;
        this.active = active;
    }

    /** 面板右侧那行规格描述：音频给采样率声道，视频给分辨率与旋转 */
    public String spec() {
        if (isAudio()) {
            return codecName + " · " + (sampleRate / 1000) + "k · " + channels + "ch";
        }
        if (isVideo()) {
            String s = codecName + " · " + width + "×" + height;
            return rotation != 0 ? s + " · 旋转 " + rotation + "°" : s;
        }
        return codecName;
    }

    public boolean isAudio() { return TYPE_AUDIO.equals(type); }
    public boolean isVideo() { return TYPE_VIDEO.equals(type); }
    public boolean isSubtitle() { return TYPE_SUBTITLE.equals(type); }

    /** native 侧以 JSON 数组回传轨道表，这里解析成对象数组 */
    static TrackInfo[] parse(String json) {
        if (json == null || json.isEmpty()) {
            return new TrackInfo[0];
        }
        try {
            JSONArray array = new JSONArray(json);
            TrackInfo[] tracks = new TrackInfo[array.length()];
            for (int i = 0; i < array.length(); i++) {
                JSONObject o = array.getJSONObject(i);
                tracks[i] = new TrackInfo(
                        o.optInt("index", -1),
                        o.optString("type", ""),
                        o.optString("lang", ""),
                        o.optString("title", ""),
                        o.optInt("codec", 0),
                        o.optString("codecName", "-"),
                        o.optInt("sampleRate", 0),
                        o.optInt("channels", 0),
                        o.optInt("width", 0),
                        o.optInt("height", 0),
                        o.optInt("rotation", 0),
                        o.optBoolean("active", false));
            }
            return tracks;
        } catch (Exception e) {
            Log.e(TAG, "parse track info failed: " + e.getMessage());
            return new TrackInfo[0];
        }
    }

    @Override
    public String toString() {
        return "TrackInfo{" + index + " " + type
                + (language.isEmpty() ? "" : " " + language)
                + (title.isEmpty() ? "" : " \"" + title + "\"")
                + (active ? " *active*" : "") + "}";
    }
}
