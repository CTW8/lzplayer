package com.ctw.verecorder

class NativeLib {

    /**
     * A native method that is implemented by the 'verecorder' native library,
     * which is packaged with this application.
     */
    external fun stringFromJNI(): String

    companion object {
        // Used to load the 'verecorder' library on application startup.
        init {
            System.loadLibrary("verecorder")
        }
    }
}