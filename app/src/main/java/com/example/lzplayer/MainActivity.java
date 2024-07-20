package com.example.lzplayer;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.media.MediaPlayer;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.TextView;

import com.example.lzplayer.databinding.ActivityMainBinding;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;


import com.example.lzplayer_core.IVEPlayerListener;
import com.example.lzplayer_core.VEPlayer;
import com.zhihu.matisse.Matisse;
import com.zhihu.matisse.MimeType;
import com.zhihu.matisse.engine.impl.GlideEngine;
import com.zhihu.matisse.engine.impl.PicassoEngine;
import com.zhihu.matisse.filter.Filter;
import com.zhihu.matisse.internal.entity.CaptureStrategy;

import java.io.IOException;

import pub.devrel.easypermissions.EasyPermissions;

import com.example.lzplayer_core.NativeLib;

public class MainActivity extends AppCompatActivity implements View.OnClickListener,SurfaceHolder.Callback {
    private final String TAG = "VEPlayer";
    private SurfaceView glSurfaceView;
    private Button btnPlay;
    private Button btnPause;
    private Button btnSelect;
    private final int REQUEST_CODE_CHOOSE = 200;
    private SeekBar btnSeekBar;

    private String filePath;

    private Surface mSurface;
    private VEPlayer mPlayer;
    private long mDuration=1;

    private static final int RC_FILE_PERM = 123;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(ActivityMainBinding.inflate(getLayoutInflater()).getRoot());

        requestPermissions();
        initView();
    }

    private void requestPermissions(){
        if(!EasyPermissions.hasPermissions(this,Manifest.permission.READ_EXTERNAL_STORAGE)){
            EasyPermissions.requestPermissions(this,"申请文件读取权限",RC_FILE_PERM, Manifest.permission.READ_EXTERNAL_STORAGE);
        }
    }

    private void initView(){
        Log.d(TAG,"initView");
        mPlayer = new VEPlayer();

        glSurfaceView=findViewById(R.id.glVideoView);
        glSurfaceView.getHolder().addCallback(this);

        btnSelect = (Button) findViewById(R.id.btnSelectFile);
        btnPlay = (Button) findViewById(R.id.btnPlay);
        btnPlay.setOnClickListener(this);
        btnPause = (Button) findViewById(R.id.btnPause);
        btnPause.setOnClickListener(this);
        btnSeekBar = findViewById(R.id.seek_bar);
        btnSeekBar.setMax(1000);

        btnSelect.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Matisse.from(MainActivity.this)
                        .choose(MimeType.ofVideo(), false)
                        .countable(true)
                        .maxSelectable(1)
//                        .addFilter(new GifSizeFilter(320, 320, 5 * Filter.K * Filter.K))
                        .gridExpectedSize(
                                getResources().getDimensionPixelSize(R.dimen.grid_expected_size))
                        .restrictOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
                        .thumbnailScale(0.85f)
                        .imageEngine(new GlideEngine())
                        .setOnSelectedListener((uriList, pathList) -> {
                            Log.e("onSelected", "onSelected: pathList=" + pathList);
                        })
                        .showSingleMediaType(true)
                        .originalEnable(true)
                        .maxOriginalSize(10)
                        .autoHideToolbarOnSingleTap(true)
                        .setOnCheckedListener(isChecked -> {
                            Log.e("isChecked", "onCheck: isChecked=" + isChecked);
                        })
                        .forResult(REQUEST_CODE_CHOOSE);
            }
        });

        mPlayer.registerListener(new IVEPlayerListener() {
            @Override
            public void onInfo(int type, int msg1, double msg2, String msg3, Object obj) {

            }

            @Override
            public void onError(int type, int msg1, int msg2, String msg3) {

            }

            @Override
            public void onProgress(int progress) {
                Log.d(TAG,"progress :" + (int)(progress*1000.0f/(mDuration*1000)) + "  value:"+(progress*1.0f)/mDuration);
                btnSeekBar.setProgress((int)(progress*1000.0f/(mDuration*1000)));
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_CHOOSE && resultCode == RESULT_OK) {
            filePath=Matisse.obtainPathResult(data).get(0);
            Log.d("Matisse", "Uris: " + Matisse.obtainResult(data)+" size:"+ Matisse.obtainResult(data).size());
            Log.d("Matisse", "Paths: " + Matisse.obtainPathResult(data));
            Log.e("Matisse", "Use the selected photos with original: "+String.valueOf(Matisse.obtainOriginalState(data)));
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        // Forward results to EasyPermissions
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    public void onClick(View view) {
        switch (view.getId()){
            case R.id.btnPlay:{
                mPlayer.init(filePath);
                mDuration = mPlayer.getDuration();
                Log.d(TAG,"mDuration:"+mDuration);
                mPlayer.start();
                break;
            }
            case R.id.btnPause:{
                mPlayer.pause();
                break;
            }
            case R.id.btnStop:{
                mPlayer.stop();
                break;
            }
            default:
                break;
        }
    }


    @Override
    public void surfaceCreated(@NonNull SurfaceHolder surfaceHolder) {
        Log.d(TAG,"###Jack surfaceCreated");

    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder surfaceHolder, int i, int i1, int i2) {
        Log.d(TAG,"###Jack surfaceChanged");
        mSurface = surfaceHolder.getSurface();
        mPlayer.setSurface(mSurface,i1,i2);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder surfaceHolder) {
        Log.d(TAG,"###Jack surfaceDestroyed");
    }

    @Override
    protected void onResume() {
        super.onResume();
    }
}