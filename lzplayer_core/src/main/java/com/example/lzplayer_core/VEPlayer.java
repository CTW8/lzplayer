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

    public void setPlaySpeed(float speed) {
        if (mNativeHandle != null) {
            Log.d(TAG,"setPlaySpeed enter");
            mNativeHandle.setPlaySpeed(speed);
        }
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
