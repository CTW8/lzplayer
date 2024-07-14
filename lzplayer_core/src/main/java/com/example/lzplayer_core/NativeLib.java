package com.example.lzplayer_core;

import android.view.Surface;

public class NativeLib {
    private long mHandle = 0;
    private IVEPlayerListener mListener;

    // Used to load the 'lzplayer_core' library on application startup.
    static {
        System.loadLibrary("lzplayer_core");
    }

    public NativeLib(){
        mHandle = createNativeHandle();
    }

    public int init(String path){
        if(mHandle != 0){
            return nativeInit(mHandle,path);
        }
        return -1;
    }

    public int setSurface(Surface surface,int width,int height){
        if(mHandle != 0){
            return nativeSetSurface(mHandle,surface,width,height);
        }
        return -1;
    }

    public int start(){
        if(mHandle != 0){
            return nativeStart(mHandle);
        }
        return -1;
    }

    public int stop(){
        if(mHandle != 0){
            return nativeStop(mHandle);
        }
        return -1;
    }

    public int pause(){
        if(mHandle != 0){
            return nativePause(mHandle);
        }
        return -1;
    }

    public int seekTo(long timestamp){
        if(mHandle != 0){
            return nativeSeekTo(mHandle,timestamp);
        }
        return -1;
    }

    public int release(){
        if(mHandle != 0){
            return nativeRelease(mHandle);
        }
        return -1;
    }

    public int registerNativeCallback(IVEPlayerListener callback){
        if(callback == null){
            return -1;
        }
        mListener = callback;
        return 1;
    }

    public void onNativeInfoCallback(int type,int msg1,double msg2,String msg3,Object obj){
        if(mListener != null){
            mListener.onInfo(type,msg1,msg2,msg3,obj);
        }
    }

    public void onNativeErrorCallback(int type,int msg1,String msg3){
        if(mListener != null){
            mListener.onError(type,msg1,0,msg3);
        }
    }

    public void onNativeProgress(int progress){
        if(mListener != null){
            mListener.onProgress(progress);
        }
    }

    private static native long createNativeHandle();
    private native int nativeInit(long handle,String path);
    private native int nativeSetSurface(long handle,Surface surface,int width,int height);

    private native int nativeStart(long handle);
    private native int nativePause(long handle);
    private native int nativeStop(long handle);
    private native int nativeSeekTo(long handle,long timestamp);
    private native int nativeRelease(long handle);
}