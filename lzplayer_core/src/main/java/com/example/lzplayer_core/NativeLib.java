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
    private HandlerThread mEventThread;
    private static final String TAG = "NativeLib";

    // Used to load the 'lzplayer_core' library on application startup.
    static {
        System.loadLibrary("lzplayer_core");
    }

    public NativeLib(){
        mHandle = createNativeHandle();
    }

    public synchronized int init(String path){
        if(mHandle != 0){
            // 重复 init 时复用已有的事件线程，避免每次都新起一个线程泄漏
            if(mEventThread == null){
                mEventThread = new HandlerThread("veplayer");
                mEventThread.start();
                mEventHandler = new EventHandler(this, mEventThread.getLooper());
            }
            return nativeInit(new WeakReference<NativeLib>(this), mHandle, path);
        }
        return -1;
    }

    public synchronized int setSurface(Surface surface,int width,int height){
        if(mHandle != 0){
            return nativeSetSurface(mHandle,surface,width,height);
        }
        return -1;
    }

    public synchronized int start(){
        if(mHandle != 0){
            return nativeStart(mHandle);
        }
        return -1;
    }

    public synchronized int stop(){
        if(mHandle != 0){
            return nativeStop(mHandle);
        }
        return -1;
    }

    public synchronized int pause(){
        if(mHandle != 0){
            return nativePause(mHandle);
        }
        return -1;
    }

    public synchronized int seekTo(double timestampMs){
        if(mHandle != 0){
            return nativeSeekTo(mHandle,timestampMs);
        }
        return -1;
    }

    public synchronized void setLooping(boolean loop){
        if(mHandle != 0){
            setLooping(mHandle,loop);
        }
    }

    public synchronized void setPlaySpeed(float speed){
        if(mHandle != 0){
            setPlaySpeed(mHandle,speed);
        }
    }

    public synchronized int release(){
        if(mHandle == 0){
            return -1;
        }
        // 先置零再释放：nativeRelease 会 delete 掉底层对象，
        // 句柄不清空的话第二次 release 就是 double free。
        long handle = mHandle;
        mHandle = 0;
        int ret = nativeRelease(handle);

        if(mEventThread != null){
            mEventThread.quitSafely();
            mEventThread = null;
            mEventHandler = null;
        }
        return ret;
    }

    public synchronized long getDuration(){
        if(mHandle != 0){
            return nativeGetDuration(mHandle);
        }
        return -1;
    }

    /** 当前播放位置(毫秒) */
    public synchronized long getCurrentPosition(){
        if(mHandle != 0){
            return nativeGetCurrentPosition(mHandle);
        }
        return -1;
    }

    public synchronized int prepare(){
        if(mHandle != 0){
            return nativePrepare(mHandle);
        }
        return -1;
    }

    public synchronized int resume(){
        if(mHandle != 0){
            Log.d(TAG,"resume");
            return nativeResume(mHandle);
        }
        return -1;
    }

    public synchronized int prepareAsync(){
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
        // 播放器已被回收/释放时，native 侧仍可能有在途回调
        if (mp != null && mp.mEventHandler != null) {
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
                case VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION:{
                    mMediaPlayer.onNativeInfoCallback(VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION, msg.arg1, msg.obj);
                    break;
                }
                case VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE:{
                    mMediaPlayer.onNativeInfoCallback(VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE, msg.arg1, msg.obj);
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
    private native long nativeGetCurrentPosition(long handle);
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