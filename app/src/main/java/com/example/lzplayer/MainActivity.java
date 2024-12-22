package com.example.lzplayer;

import static com.example.lzplayer_core.IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_EOS;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.Toast;

import com.example.lzplayer.databinding.ActivityMainBinding;


import com.example.lzplayer_core.IVEPlayerListener;
import com.example.lzplayer_core.VEPlayer;
import com.zhihu.matisse.Matisse;
import com.zhihu.matisse.MimeType;
import com.zhihu.matisse.engine.impl.GlideEngine;

import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends AppCompatActivity implements View.OnClickListener,SurfaceHolder.Callback {
    private final String TAG = "VEPlayer";
    private SurfaceView glSurfaceView;
    private Button btnPlay;
    private Button btnPause;
    private Button btnSelect;
    private Button btnResume;
    private Button btnStop;
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
        mPlayer = new VEPlayer();
        initView();
    }

    private void requestPermissions(){
        if(!EasyPermissions.hasPermissions(this,Manifest.permission.READ_EXTERNAL_STORAGE)){
            EasyPermissions.requestPermissions(this,"申请文件读取权限",RC_FILE_PERM, Manifest.permission.READ_EXTERNAL_STORAGE);
        }
    }

    private void initView(){
        Log.d(TAG,"initView");

        glSurfaceView=findViewById(R.id.glVideoView);
        glSurfaceView.getHolder().addCallback(this);

        btnSelect = (Button) findViewById(R.id.btnSelectFile);
        btnPlay = (Button) findViewById(R.id.btnPlay);
        btnPlay.setOnClickListener(this);
        btnPause = (Button) findViewById(R.id.btnPause);
        btnPause.setOnClickListener(this);
        btnResume = (Button) findViewById(R.id.btnResume);
        btnResume.setOnClickListener(this);
        btnStop = (Button) findViewById(R.id.btnStop);
        btnStop.setOnClickListener(this);

        btnSeekBar = findViewById(R.id.seek_bar);
        btnSeekBar.setMax(1000);

        btnSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int i, boolean b) {
                Log.d(TAG,"onProgressChanged value:" + i + " b:"+b);
                if(b) {
                    mPlayer.seekTo(i * mDuration / 1000);
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {

            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {

            }
        });

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
    }


    private void registerPlayerCallback(){

        mPlayer.registerListener(new IVEPlayerListener() {
            @Override
            public void onInfo(int type, int msg1, Object obj) {
                if(type == VE_PLAYER_NOTIFY_EVENT_ON_EOS){
                    Toast.makeText(MainActivity.this,"播放结束",Toast.LENGTH_SHORT).show();
                }
            }

            @Override
            public void onError(int type, int msg1, int msg2, String msg3) {

            }

            @Override
            public void onProgress(double progressMs) {
                double pre = progressMs/mDuration;
                int value = (int)(pre*1000);
                Log.d(TAG,"progressMs :" +progressMs + "  pre:"+ progressMs/mDuration + " value:"+value);

                btnSeekBar.setProgress(value);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_CODE_CHOOSE && resultCode == RESULT_OK) {
//            if(mPlayer != null){
////                mPlayer.pause();
//                mPlayer.stop();
//                mPlayer.release();
//                mPlayer = null;
//            }

//            if(mPlayer == null){
//                mPlayer = new VEPlayer();
//            }
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
                registerPlayerCallback();
                mPlayer.init(filePath);
                mPlayer.prepare();
                mPlayer.setLooping(true);
                mDuration = mPlayer.getDuration();
                Log.d(TAG,"mDuration:"+mDuration);
                mPlayer.start();
                break;
            }
            case R.id.btnPause:{
                mPlayer.pause();
                break;
            }
            case R.id.btnResume:{
                Log.d(TAG,"R.id.btnResume");
                mPlayer.resume();
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