package com.example.lzplayer_core;

import android.util.Log;
import android.view.Surface;

public class VEPlayer {
    private final static String TAG = "VEPlayer";
    private IVEPlayerListener mListener;
    private NativeLib mNativeHandle = null;

    public VEPlayer(){
        mNativeHandle = new NativeLib();
    }

    public int init(String path){
        if(mNativeHandle != null){
            Log.d(TAG,"prepare enter");
            return mNativeHandle.init(path);
        }
        return -1;
    }

    public int start(){
        if(mNativeHandle != null){
            Log.d(TAG,"start enter");
            return mNativeHandle.start();
        }
        return -1;
    }

    public int setSurface(Surface surface,int w,int h){
        if(mNativeHandle != null){
            Log.d(TAG,"setSurface enter");
            return mNativeHandle.setSurface(surface,w,h);
        }
        return -1;
    }

    public int stop(){
        if(mNativeHandle != null){
            Log.d(TAG,"stop enter");
            return mNativeHandle.stop();
        }
        return -1;
    }

    public int pause(){
        if(mNativeHandle != null){
            Log.d(TAG,"pause enter");
            return  mNativeHandle.pause();
        }
        return -1;
    }

    public int resume(){
        if(mNativeHandle != null){
            Log.d(TAG,"resume enter");
            return  mNativeHandle.resume();
        }
        return -1;
    }

    public int seekTo(double timestamp){
        if(mNativeHandle != null){
            Log.d(TAG,"seekTo enter");
            return mNativeHandle.seekTo(timestamp);
        }
        return -1;
    }

    public void setLooping(boolean loop) {
        if (mNativeHandle != null) {
            Log.d(TAG,"setLooping enter");
             mNativeHandle.setLooping(loop);
        }
    }

    /**
     * 变速播放。支持 0.5x ~ 2.0x，音调保持不变。
     * @return 0 成功；负值失败(超出范围或播放器未就绪)
     */
    public int setPlaySpeed(float speed) {
        if (mNativeHandle != null) {
            Log.d(TAG,"setPlaySpeed enter speed=" + speed);
            return mNativeHandle.setPlaySpeed(speed);
        }
        return -1;
    }

    /**
     * 轨道列表。需在 prepare 之后调用。
     * @return 轨道数组；未就绪时返回空数组
     */
    public TrackInfo[] getTrackInfo() {
        if (mNativeHandle == null) {
            return new TrackInfo[0];
        }
        return TrackInfo.parse(mNativeHandle.getTrackInfo());
    }

    /**
     * 切换活跃轨道。音轨切换时画面会短暂定格(不黑屏)后从当前位置续播；
     * 字幕轨切换是即时的。视频轨切换暂不支持。
     * @param trackIndex getTrackInfo() 返回的 index
     */
    public int selectTrack(int trackIndex) {
        if (mNativeHandle != null) {
            Log.d(TAG,"selectTrack " + trackIndex);
            return mNativeHandle.selectTrack(trackIndex);
        }
        return -1;
    }

    /** 关闭指定轨道(目前只对字幕轨有意义) */
    public int deselectTrack(int trackIndex) {
        if (mNativeHandle != null) {
            return mNativeHandle.deselectTrack(trackIndex);
        }
        return -1;
    }

    /**
     * 加载外挂字幕文件(.srt/.ass)。加载后会作为一条新轨道出现在
     * getTrackInfo() 里，再用 selectTrack() 选中它才开始显示。
     */
    public int addExternalSubtitle(String path) {
        if (mNativeHandle != null) {
            Log.d(TAG,"addExternalSubtitle " + path);
            return mNativeHandle.addExternalSubtitle(path);
        }
        return -1;
    }

    /**
     * 运行期统计快照。诊断面板按进度回调的节奏拉取即可，不必另开定时器。
     * @return {@link PlayerStats}；播放器未就绪时各字段为默认值
     */
    /**
     * 启播链路里程碑原始 JSON。
     *
     * <p>结构化解析类放在 perf-metrics 步骤4，本步先把通道打通，
     * 便于用 adb 直接把这段 JSON 打出来核对采集点是否齐全。
     */
    public String getStartupTraceJson() {
        if (mNativeHandle != null) {
            return mNativeHandle.getStartupTrace();
        }
        return "{\"valid\":false}";
    }

    /** 稳态读数原始 JSON。结构化解析(含分位数)归 perf-metrics 步骤4 */
    public String getStatsJsonRaw() {
        if (mNativeHandle != null) {
            return mNativeHandle.getStats();
        }
        return "{}";
    }

    /** 最近 10 次 seek 的三阶段耗时与精度原始 JSON */
    public String getSeekTraceJson() {
        if (mNativeHandle != null) {
            return mNativeHandle.getSeekTrace();
        }
        return "{\"count\":0,\"items\":[]}";
    }

    /** 变速与切轨的三阶段耗时原始 JSON */
    public String getSwitchTraceJson() {
        if (mNativeHandle != null) {
            return mNativeHandle.getSwitchTrace();
        }
        return "{}";
    }

    public PlayerStats getStats() {
        if (mNativeHandle == null) {
            return PlayerStats.empty();
        }
        return PlayerStats.parse(mNativeHandle.getStats());
    }

    /** 强制软解。改的是下次 prepare 的策略，当前播放不受影响。 */
    public int setForceSoftwareDecoder(boolean force) {
        if (mNativeHandle != null) {
            return mNativeHandle.setForceSoftwareDecoder(force);
        }
        return -1;
    }

    /** 强制音频走 OpenSL ES。改的是下次 prepare 的策略。 */
    public int setForceSlesAudio(boolean force) {
        if (mNativeHandle != null) {
            return mNativeHandle.setForceSlesAudio(force);
        }
        return -1;
    }

    /**
     * 软解显示端改用 Vulkan 渲染。改的是下次 prepare 的策略。
     *
     * <p>只影响软解：硬解由 MediaCodec 直出 Surface，既不走 GLES 也不走
     * Vulkan。所以要看到效果必须同时打开强制软解，否则这个开关毫无观感变化。
     * Vulkan 初始化失败会自动回退 GLES，播放不会因此中断。
     */
    public int setPreferVulkanRender(boolean prefer) {
        if (mNativeHandle != null) {
            return mNativeHandle.setPreferVulkanRender(prefer);
        }
        return -1;
    }

    public long getDuration(){
        if(mNativeHandle != null){
            Log.d(TAG,"getDuration enter");
            return mNativeHandle.getDuration();
        }
        return -1;
    }

    /** 当前播放位置(毫秒) */
    public long getCurrentPosition(){
        if(mNativeHandle != null){
            return mNativeHandle.getCurrentPosition();
        }
        return -1;
    }

    public int prepare(){
        if(mNativeHandle != null){
            Log.d(TAG,"prepare enter");
            return mNativeHandle.prepare();
        }
        return -1;
    }

    public int prepareAsync(){
        if(mNativeHandle != null){
            Log.d(TAG,"prepareAsync enter");
            return mNativeHandle.prepareAsync();
        }
        return -1;
    }

    ///获取所有底层player信息需要在new之后立即调用
    public int registerListener(IVEPlayerListener listener){
        if(mNativeHandle != null){
            Log.d(TAG,"registerListener enter");
            return mNativeHandle.registerNativeCallback(listener);
        }
        return -1;
    }

    public int release(){
        if(mNativeHandle != null){
            Log.d(TAG,"release enter");
            return mNativeHandle.release();
        }
        return -1;
    }
}
