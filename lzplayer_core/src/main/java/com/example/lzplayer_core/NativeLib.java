package com.example.lzplayer_core;

import static com.example.lzplayer_core.IMediaPlayerListener.*;

import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import java.lang.ref.WeakReference;

public class NativeLib {
    private long mHandle = 0;
    private IVEPlayerListener mListener;
    private EventHandler mEventHandler;
    private static final String TAG = "NativeLib";

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
            mEventHandler = new EventHandler(this,handlerThread.getLooper());
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
        Log.e(TAG, "🚀🚀🚀 NativeLib.start() ENTRY - This should always appear! 🚀🚀🚀");
        Log.d(TAG, "NativeLib.start() called, mHandle: " + mHandle);
        if(mHandle != 0){
            Log.e(TAG, "🚀🚀🚀 About to call nativeStart with handle: " + mHandle + " 🚀🚀🚀");
            int result = nativeStart(mHandle);
            Log.e(TAG, "🚀🚀🚀 nativeStart returned: " + result + " 🚀🚀🚀");
            Log.d(TAG, "NativeLib.start() result: " + result);
            return result;
        }
        Log.e(TAG, "NativeLib.start() failed - mHandle is 0");
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

    public int seekTo(double timestampMs){
        if(mHandle != 0){
            return nativeSeekTo(mHandle,timestampMs);
        }
        return -1;
    }

    public void setLooping(boolean loop){
        if(mHandle != 0){
            setLooping(mHandle,loop);
        }
    }

    public void setPlaySpeed(float speed){
        if(mHandle != 0){
            setPlaySpeed(mHandle,speed);
        }
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

    public int prepare(){
        if(mHandle != 0){
            return nativePrepare(mHandle);
        }
        return -1;
    }

    public int resume(){
        if(mHandle != 0){
            Log.d(TAG,"resume");
            return nativeResume(mHandle);
        }
        return -1;
    }

    public int prepareAsync(){
        if(mHandle != 0){
            return nativePrepareAsync(mHandle);
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

    private static void postEventFromNative(Object player_ref ,int what, int arg1, double arg2,Object obj){
        final NativeLib mp = (NativeLib)((WeakReference)player_ref).get();
        if (mp.mEventHandler != null) {
            Log.d(TAG,"postEventFromNative what:" + what + " arg1:" + arg1 + " arg2:" + arg2 + " obj:" + obj);
            Message m = Message.obtain();
            m.what = what;
            m.arg1 = arg1;
            Bundle data = new Bundle();
            data.putDouble("arg2",arg2);
            m.setData(data);
            m.obj = obj;
            mp.mEventHandler.sendMessage(m);
        }
    }

    public void onNativeInfoCallback(int type,int msg1,Object obj){
        if(mListener != null){
            mListener.onInfo(type,msg1,obj);
        }
    }

    public void onNativeErrorCallback(int type,int msg1,String msg3){
        if(mListener != null){
            String errorMsg = (msg3 != null) ? msg3 : "Unknown error";
            mListener.onError(type,msg1,0,errorMsg);
        }
    }

    public void onNativeProgress(double progress){
        if(mListener != null){
            mListener.onProgress(progress);
        }
    }

    private class EventHandler extends Handler{
        private NativeLib mMediaPlayer;

        public EventHandler(NativeLib mp, Looper looper) {
            super(looper);
            mMediaPlayer = mp;
        }

        @Override
        public void handleMessage(@NonNull Message msg) {
            Bundle data = msg.getData();
            double arg2 = data != null ? data.getDouble("arg2", 0.0) : 0.0;
            
            switch (msg.what){
                case VE_PLAYER_NOTIFY_EVENT_ON_PROGRESS:{
                    mMediaPlayer.onNativeProgress(arg2);
                    break;
                }
                case VE_PLAYER_NOTIFY_EVENT_ON_ERROR:{
                    String errorMsg = null;
                    if (msg.obj != null) {
                        errorMsg = msg.obj.toString();
                    }
                    mMediaPlayer.onNativeErrorCallback(msg.what, msg.arg1, errorMsg);
                    break;
                }
                case VE_PLAYER_NOTIFY_EVENT_ON_PREPARED:{
                    mMediaPlayer.onNativeInfoCallback(VE_PLAYER_NOTIFY_EVENT_ON_PREPARED, msg.arg1, msg.obj);
                    break;
                }
                case VE_PLAYER_NOTIFY_EVENT_ON_EOS:{
                    mMediaPlayer.onNativeInfoCallback(VE_PLAYER_NOTIFY_EVENT_ON_EOS, msg.arg1, msg.obj);
                    break;
                }
                case VE_PLAYER_NOTIFY_EVENT_ON_FIRST_FRAME:{
                    mMediaPlayer.onNativeInfoCallback(VE_PLAYER_NOTIFY_EVENT_ON_FIRST_FRAME, msg.arg1, msg.obj);
                    break;
                }
                case VE_PLAYER_NOTIFY_EVENT_ON_INFO:{
                    mMediaPlayer.onNativeInfoCallback(msg.what, msg.arg1, msg.obj);
                    break;
                }
                default:
                    Log.w(TAG, "Unknown message type: " + msg.what);
                    break;
            }
        }
    }

    private static native long createNativeHandle();
    private native int nativeInit(Object mediaplayerThis,long handle,String path);
    private native int nativeSetSurface(long handle,Surface surface,int width,int height);
    private native long nativeGetDuration(long handle);
    private native int nativePrepare(long handle);
    private native int nativePrepareAsync(long handle);
    private native int nativeStart(long handle);
    private native int nativePause(long handle);
    private native int nativeResume(long handle);
    private native int nativeStop(long handle);
    private native int setLooping(long handle,boolean loop);
    private native int setPlaySpeed(long handle,float speed);
    private native int nativeSeekTo(long handle,double timestampMs);
    private native int nativeRelease(long handle);
}