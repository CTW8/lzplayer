package com.example.lzplayer_core;

import android.view.Surface;

public class VEPlayer {
    private NativeLib mNativeHandle = null;

    public VEPlayer(){
        mNativeHandle = new NativeLib();
    }

    public int init(String path, Surface surface){
        if(mNativeHandle != null){
            return mNativeHandle.init(path,surface);
        }
        return -1;
    }

    public int start(){
        if(mNativeHandle != null){
            return mNativeHandle.start();
        }
        return -1;
    }

    public int setSurface(Surface surface){
        if(mNativeHandle != null){
            return mNativeHandle.setSurface(surface);
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

    public int seekTo(long timestamp){
        if(mNativeHandle != null){
            return mNativeHandle.seekTo(timestamp);
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
