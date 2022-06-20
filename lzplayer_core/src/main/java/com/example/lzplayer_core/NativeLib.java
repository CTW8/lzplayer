package com.example.lzplayer_core;

public class NativeLib {

    // Used to load the 'lzplayer_core' library on application startup.
    static {
        System.loadLibrary("lzplayer_core");
    }

    /**
     * A native method that is implemented by the 'lzplayer_core' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
}