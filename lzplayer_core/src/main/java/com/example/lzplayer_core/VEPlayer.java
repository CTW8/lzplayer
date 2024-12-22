package com.example.lzplayer_core;

import android.view.Surface;

public class VEPlayer {
    private IVEPlayerListener mListener;
    private NativeLib mNativeHandle = null;

    public VEPlayer(){
        mNativeHandle = new NativeLib();
    }

    public int init(String path){
        if(mNativeHandle != null){
            return mNativeHandle.init(path);
        }
        return -1;
    }

    public int start(){
        if(mNativeHandle != null){
            return mNativeHandle.start();
        }
        return -1;
    }

    public int setSurface(Surface surface,int w,int h){
        if(mNativeHandle != null){
            return mNativeHandle.setSurface(surface,w,h);
        }
        return -1;
    }

    public int stop(){
        if(mNativeHandle != null){
            return mNativeHandle.stop();
        }
        return -1;
    }

    public int pause(){
        if(mNativeHandle != null){
            return  mNativeHandle.pause();
        }
        return -1;
    }

    public int resume(){
        if(mNativeHandle != null){
            return  mNativeHandle.resume();
        }
        return -1;
    }

    public int seekTo(double timestamp){
        if(mNativeHandle != null){
            return mNativeHandle.seekTo(timestamp);
        }
        return -1;
    }

    public void setLooping(boolean loop) {
        if (mNativeHandle != null) {
             mNativeHandle.setLooping(loop);
        }
    }

    public void setPlaySpeed(float speed) {
        if (mNativeHandle != null) {
            mNativeHandle.setPlaySpeed(speed);
        }
    }

    public long getDuration(){
        if(mNativeHandle != null){
            return mNativeHandle.getDuration();
        }
        return -1;
    }

    public int prepare(){
        if(mNativeHandle != null){
            return mNativeHandle.prepare();
        }
        return -1;
    }

    public int prepareAsync(){
        if(mNativeHandle != null){
            return mNativeHandle.prepareAsync();
        }
        return -1;
    }

    ///获取所有底层player信息需要在new之后立即调用
    public int registerListener(IVEPlayerListener listener){
        if(mNativeHandle != null){
            return mNativeHandle.registerNativeCallback(listener);
        }
        return -1;
    }

    public int release(){
        if(mNativeHandle != null){
            return mNativeHandle.release();
        }
        return -1;
    }
}
