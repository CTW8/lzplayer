package com.example.lzplayer_core;

import static com.example.lzplayer_core.IMediaPlayerListener.*;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.view.Surface;

import androidx.annotation.NonNull;

import java.lang.ref.WeakReference;

public class NativeLib {
    private long mHandle = 0;
    private IVEPlayerListener mListener;
    private Handler mEventHandler;

    // Used to load the 'lzplayer_core' library on application startup.
    static {
        System.loadLibrary("lzplayer_core");
    }

    public NativeLib(){
        mHandle = createNativeHandle();
    }

    public int init(String path){
        if(mHandle != 0){
            HandlerThread handlerThread = new HandlerThread("veplayer");
            handlerThread.start();
            mEventHandler = new EventHandler(handlerThread.getLooper());
            return nativeInit(new WeakReference<NativeLib>(this), mHandle, path);
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

    public long getDuration(){
        if(mHandle != 0){
            return nativeGetDuration(mHandle);
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

    private static void postEventFromNative(Object player_ref ,int type,int msg1,double msg2,String msg3,Object obj){
        final NativeLib mp = (NativeLib)((WeakReference)player_ref).get();
        switch (type){
            case VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS:{
                break;
            }
            case VE_PLAYER_NOTIFY_EVENT_ON_INFO:{
                break;
            }
            case VE_PLAYER_NOTIFY_EVENT_ON_PREPARED:{
                break;
            }
            default:{
                break;
            }
        }
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

    private class EventHandler extends Handler{
        public EventHandler(Looper looper) {
            super(looper);
        }
        @Override
        public void handleMessage(@NonNull Message msg) {
            switch (msg.what){
                case VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS:{
                    break;
                }
                default:{
                    break;
                }
            }
        }
    }

    private static native long createNativeHandle();
    private native int nativeInit(Object mediaplayerThis,long handle,String path);
    private native int nativeSetSurface(long handle,Surface surface,int width,int height);
    private native long nativeGetDuration(long handle);
    private native int nativeStart(long handle);
    private native int nativePause(long handle);
    private native int nativeStop(long handle);
    private native int nativeSeekTo(long handle,long timestamp);
    private native int nativeRelease(long handle);
}