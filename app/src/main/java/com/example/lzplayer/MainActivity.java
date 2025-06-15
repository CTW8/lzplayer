package com.example.lzplayer;

import androidx.activity.result.ActivityResult;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.ctw.mediaselector.MediaSelectorActivity;
import com.ctw.mediaselector.MediaType;
import com.example.lzplayer_core.IVEPlayerListener;
import com.example.lzplayer_core.VEPlayer;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends AppCompatActivity implements View.OnClickListener, SurfaceHolder.Callback, IVEPlayerListener {
    private static final String TAG = "LZPlayer";
    
    // Player states as constants instead of enum
    private static final int STATE_IDLE = 0;
    private static final int STATE_INITIALIZED = 1;
    private static final int STATE_PREPARED = 2;
    private static final int STATE_STARTED = 3;
    private static final int STATE_PAUSED = 4;
    private static final int STATE_STOPPED = 5;
    private static final int STATE_ERROR = 6;
    
    // UI Components
    private Button btnSelectFile, btnPlay, btnPause, btnStop;
    private TextView tvSelectedFile, tvStatus, tvDuration;
    private SeekBar seekBar;
    private SurfaceView surfaceView;
    private SurfaceHolder surfaceHolder;
    
    // Player Components
    private VEPlayer vePlayer;
    private String currentFilePath = "";
    private boolean isPlayerPrepared = false;
    private boolean isPlaying = false;
    private boolean isPaused = false;
    private long videoDuration = 0;
    
    // Progress tracking
    private Handler progressHandler;
    private Runnable progressRunnable;
    private boolean isUserSeeking = false;
    
    // Permissions
    private static final int RC_FILE_PERM = 123;
    private ActivityResultLauncher<Intent> mediaSelectLauncher;
    
    // Current state
    private int currentState = STATE_IDLE;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);

        initViews();
        initPlayer();
        initMediaSelector();
        requestPermissions();
        
        progressHandler = new Handler(Looper.getMainLooper());
        setupProgressTracking();
    }

    private void initViews() {
        Log.d(TAG, "initViews");
        
        // Find views
        btnSelectFile = findViewById(R.id.btnSelectFile);
        btnPlay = findViewById(R.id.btnPlay);
        btnPause = findViewById(R.id.btnPause);
        btnStop = findViewById(R.id.btnStop);
        tvSelectedFile = findViewById(R.id.tvSelectedFile);
        tvStatus = findViewById(R.id.tvStatus);
        tvDuration = findViewById(R.id.tvDuration);
        seekBar = findViewById(R.id.seekBar);
        surfaceView = findViewById(R.id.surfaceView);
        
        // Set click listeners
        btnSelectFile.setOnClickListener(this);
        btnPlay.setOnClickListener(this);
        btnPause.setOnClickListener(this);
        btnStop.setOnClickListener(this);
        
        // Setup surface
        surfaceHolder = surfaceView.getHolder();
        surfaceHolder.addCallback(this);
        
        // Setup seek bar with simple listener
        seekBar.setOnSeekBarChangeListener(new SimpleSeekBarListener());
    }

    private void initPlayer() {
        Log.d(TAG, "initPlayer");
        currentState = STATE_IDLE;
        updateStatus("Ready to select video");
    }

    private void initMediaSelector() {
        mediaSelectLauncher = registerForActivityResult(
            new ActivityResultContracts.StartActivityForResult(), 
            new SimpleActivityResultCallback()
        );
    }

    private void setupProgressTracking() {
        progressRunnable = new SimpleProgressRunnable();
    }

    private void selectedFiles(List<String> files) {
        if (files != null && !files.isEmpty()) {
            currentFilePath = files.get(0);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.GINGERBREAD) {
                if (currentFilePath != null && !currentFilePath.isEmpty()) {
                    String fileName = currentFilePath.substring(currentFilePath.lastIndexOf("/") + 1);
                    tvSelectedFile.setText("Selected: " + fileName);
                    Log.d(TAG, "Selected file: " + currentFilePath);

                    resetPlayer();

                    if (initializeVideo(currentFilePath)) {
                        updateStatus("Video loaded, ready to prepare");
                    } else {
                        updateStatus("Failed to load video");
                    }
                }
            }
        }
    }

    private boolean initializeVideo(String filePath) {
        try {
            if (vePlayer != null) {
                try {
                    vePlayer.release();
                } catch (Exception e) {
                    Log.w(TAG, "Error releasing previous player", e);
                }
                vePlayer = null;
            }
            
            vePlayer = new VEPlayer();
            
            int listenerResult = vePlayer.registerListener(this);
            if (listenerResult < 0) {
                Log.e(TAG, "Failed to register listener");
                return false;
            }
            
            int result = vePlayer.init(filePath);
            if (result == 0) {
                currentState = STATE_INITIALIZED;
                Log.d(TAG, "VEPlayer initialized successfully");
                return true;
            } else {
                Log.e(TAG, "Failed to initialize VEPlayer: " + result);
                currentState = STATE_ERROR;
                return false;
            }
        } catch (Exception e) {
            Log.e(TAG, "Exception initializing video", e);
            currentState = STATE_ERROR;
        }
        return false;
    }

    private void prepareVideo() {
        if (vePlayer != null && currentState == STATE_INITIALIZED) {
            try {
                if (surfaceHolder != null && surfaceHolder.getSurface() != null) {
                    int width = surfaceView.getWidth();
                    int height = surfaceView.getHeight();
                    if (width > 0 && height > 0) {
                        int surfaceResult = vePlayer.setSurface(surfaceHolder.getSurface(), width, height);
                        Log.d(TAG, "Surface set result: " + surfaceResult + ", size: " + width + "x" + height);
                    }
                }
                
                int result = vePlayer.prepareAsync();
                if (result == 0) {
                    updateStatus("Preparing video...");
                    Log.d(TAG, "PrepareAsync called successfully");
                } else {
                    Log.e(TAG, "Failed to prepare video: " + result);
                    currentState = STATE_ERROR;
                    updateStatus("Failed to prepare video");
                }
            } catch (Exception e) {
                Log.e(TAG, "Exception preparing video", e);
                currentState = STATE_ERROR;
                updateStatus("Error preparing video");
            }
        }
    }

    private void startPlayback() {
        if (vePlayer != null && (currentState == STATE_PREPARED || currentState == STATE_PAUSED)) {
            try {
                int result = vePlayer.start();
                if (result == 0) {
                    currentState = STATE_STARTED;
                    isPlaying = true;
                    isPaused = false;
                    updateControlButtons();
                    updateStatus("Playing");
                    startProgressTracking();
                    Log.d(TAG, "Playback started");
                } else {
                    Log.e(TAG, "Failed to start playback: " + result);
                    updateStatus("Failed to start playback");
                }
            } catch (Exception e) {
                Log.e(TAG, "Exception starting playback", e);
                updateStatus("Error starting playback");
            }
        } else {
            Log.w(TAG, "Cannot start playback, current state: " + currentState);
        }
    }

    private void pausePlayback() {
        if (vePlayer != null && currentState == STATE_STARTED) {
            try {
                int result = vePlayer.pause();
                if (result == 0) {
                    currentState = STATE_PAUSED;
                    isPlaying = false;
                    isPaused = true;
                    updateControlButtons();
                    updateStatus("Paused");
                    stopProgressTracking();
                    Log.d(TAG, "Playback paused");
                } else {
                    Log.e(TAG, "Failed to pause playback: " + result);
                }
            } catch (Exception e) {
                Log.e(TAG, "Exception pausing playback", e);
            }
        }
    }

    private void resumePlayback() {
        if (vePlayer != null && currentState == STATE_PAUSED) {
            try {
                int result = vePlayer.resume();
                if (result == 0) {
                    currentState = STATE_STARTED;
                    isPlaying = true;
                    isPaused = false;
                    updateControlButtons();
                    updateStatus("Playing");
                    startProgressTracking();
                    Log.d(TAG, "Playback resumed");
                } else {
                    Log.e(TAG, "Failed to resume playback: " + result);
                }
            } catch (Exception e) {
                Log.e(TAG, "Exception resuming playback", e);
            }
        }
    }

    private void stopPlayback() {
        if (vePlayer != null && (currentState == STATE_STARTED || currentState == STATE_PAUSED)) {
            try {
                int result = vePlayer.stop();
                if (result == 0) {
                    currentState = STATE_STOPPED;
                    isPlaying = false;
                    isPaused = false;
                    updateControlButtons();
                    updateStatus("Stopped");
                    stopProgressTracking();
                    seekBar.setProgress(0);
                    Log.d(TAG, "Playback stopped");
                } else {
                    Log.e(TAG, "Failed to stop playback: " + result);
                }
            } catch (Exception e) {
                Log.e(TAG, "Exception stopping playback", e);
            }
        }
    }

    private void resetPlayer() {
        stopProgressTracking();
        isPlayerPrepared = false;
        isPlaying = false;
        isPaused = false;
        videoDuration = 0;
        seekBar.setProgress(0);
        seekBar.setEnabled(false);
        tvDuration.setText("00:00");
        updateControlButtons();
        currentState = STATE_IDLE;
    }

    private void updateControlButtons() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                btnPlay.setEnabled(isPlayerPrepared && !isPlaying);
                btnPause.setEnabled(isPlaying && !isPaused);
                btnStop.setEnabled(isPlayerPrepared && (isPlaying || isPaused));
                
                if (isPaused) {
                    btnPlay.setText("Resume");
                } else {
                    btnPlay.setText("Play");
                }
            }
        });
    }

    private void updateStatus(final String status) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (tvStatus != null) {
                    tvStatus.setText(status);
                }
                Log.d(TAG, "Status: " + status);
            }
        });
    }

    private void startProgressTracking() {
        progressHandler.removeCallbacks(progressRunnable);
        progressHandler.post(progressRunnable);
    }

    private void stopProgressTracking() {
        progressHandler.removeCallbacks(progressRunnable);
    }

    private String formatTime(long timeMs) {
        long minutes = 0;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.GINGERBREAD) {
            minutes = TimeUnit.MILLISECONDS.toMinutes(timeMs);
        }
        long seconds = TimeUnit.MILLISECONDS.toSeconds(timeMs) % 60;
        return String.format("%02d:%02d", minutes, seconds);
    }

    private void requestPermissions() {
        if (!EasyPermissions.hasPermissions(this, Manifest.permission.READ_EXTERNAL_STORAGE)) {
            EasyPermissions.requestPermissions(this, "申请文件读取权限", RC_FILE_PERM, Manifest.permission.READ_EXTERNAL_STORAGE);
        }
    }

    @Override
    public void onClick(View view) {
        int id = view.getId();
        
        if (id == R.id.btnSelectFile) {
            Intent intent = new Intent(MainActivity.this, MediaSelectorActivity.class);
            intent.putExtra(MediaSelectorActivity.EXTRA_ALLOWED_TYPES, MediaType.VIDEO);
            intent.putExtra(MediaSelectorActivity.EXTRA_MAX_SELECT_COUNT, 1);
            mediaSelectLauncher.launch(intent);
            
        } else if (id == R.id.btnPlay) {
            if (isPaused) {
                resumePlayback();
            } else if (currentState == STATE_INITIALIZED) {
                prepareVideo();
            } else {
                startPlayback();
            }
            
        } else if (id == R.id.btnPause) {
            pausePlayback();
            
        } else if (id == R.id.btnStop) {
            stopPlayback();
        }
    }

    // SurfaceHolder.Callback methods
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.d(TAG, "Surface created");
        if (currentState == STATE_INITIALIZED && vePlayer != null) {
            prepareVideo();
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.d(TAG, "Surface changed: " + width + "x" + height);
        if (vePlayer != null && width > 0 && height > 0) {
            try {
                vePlayer.setSurface(holder.getSurface(), width, height);
            } catch (Exception e) {
                Log.e(TAG, "Error setting surface", e);
            }
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d(TAG, "Surface destroyed");
    }

    // IVEPlayerListener methods
    @Override
    public void onInfo(int type, int msg1, Object obj) {
        Log.d(TAG, "onInfo: type=" + type + ", msg1=" + msg1 + ", obj=" + obj);
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (type == 0x102) { // VE_PLAYER_NOTIFY_EVENT_ON_PREPARED
                    isPlayerPrepared = true;
                    currentState = STATE_PREPARED;
                    if (vePlayer != null) {
                        videoDuration = vePlayer.getDuration();
                        if (videoDuration > 0) {
                            seekBar.setEnabled(true);
                            tvDuration.setText(formatTime(videoDuration));
                        }
                    }
                    updateControlButtons();
                    updateStatus("Ready to play");
                    Log.d(TAG, "Player prepared, duration: " + videoDuration + "ms");
                } else if (type == 0x103) { // VE_PLAYER_NOTIFY_EVENT_ON_EOS
                    updateStatus("Playback completed");
                    stopPlayback();
                }
            }
        });
    }

    @Override
    public void onError(int type, int msg1, int msg2, String msg3) {
        final String errorMsg = (msg3 != null && !msg3.isEmpty()) ? msg3 : "Unknown error";
        Log.e(TAG, "onError: type=" + type + ", msg1=" + msg1 + ", msg2=" + msg2 + ", msg3=" + errorMsg);
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                currentState = STATE_ERROR;
                updateStatus("Error: " + errorMsg);
                Toast.makeText(MainActivity.this, "Player error: " + errorMsg, Toast.LENGTH_LONG).show();
            }
        });
    }

    @Override
    public void onProgress(double progressMs) {
        if (!isUserSeeking && videoDuration > 0 && progressMs >= 0) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    int progress = (int) ((progressMs / videoDuration) * 100);
                    seekBar.setProgress(Math.max(0, Math.min(100, progress)));
                    updateStatus("Playing - " + formatTime((long)progressMs) + " / " + formatTime(videoDuration));
                }
            });
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (isPlaying) {
            pausePlayback();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        Log.d(TAG, "onDestroy");
        
        stopProgressTracking();
        
        if (vePlayer != null) {
            try {
                vePlayer.stop();
                vePlayer.release();
            } catch (Exception e) {
                Log.e(TAG, "Error releasing player", e);
            }
            vePlayer = null;
        }
        
        if (surfaceHolder != null) {
            surfaceHolder.removeCallback(this);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(TAG, "onResume");
    }

    // Simple classes to replace anonymous inner classes
    private class SimpleSeekBarListener implements SeekBar.OnSeekBarChangeListener {
        @Override
        public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            if (fromUser && isPlayerPrepared) {
                isUserSeeking = true;
                double seekTime = (progress / 100.0) * videoDuration;
                updateStatus("Seeking to " + formatTime((long)seekTime));
            }
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
            isUserSeeking = true;
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
            if (isPlayerPrepared && vePlayer != null) {
                double seekTime = (seekBar.getProgress() / 100.0) * videoDuration;
                vePlayer.seekTo(seekTime);
                Log.d(TAG, "Seeking to: " + seekTime + "ms");
            }
            isUserSeeking = false;
        }
    }

    private class SimpleActivityResultCallback implements androidx.activity.result.ActivityResultCallback<ActivityResult> {
        @Override
        public void onActivityResult(ActivityResult result) {
            if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                Intent data = result.getData();
                ArrayList<String> selectedFilesList = data.getStringArrayListExtra("selected_files");
                if (selectedFilesList != null && !selectedFilesList.isEmpty()) {
                    selectedFiles(selectedFilesList);
                }
            }
        }
    }

    private class SimpleProgressRunnable implements Runnable {
        @Override
        public void run() {
            if (isPlaying && !isUserSeeking && isPlayerPrepared) {
                progressHandler.postDelayed(this, 1000);
            }
        }
    }
}