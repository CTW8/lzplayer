package com.example.lzplayer_core;

public interface IVEPlayerListener {
    void onInfo(int type,int msg1,Object obj);
    void onError(int type,int msg1,int msg2,String msg3);
    void onProgress(int progress);
}
