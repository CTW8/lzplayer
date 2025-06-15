package com.ctw.mediaselector

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.isVisible
import com.bumptech.glide.Glide
import com.bumptech.glide.load.engine.DiskCacheStrategy
import com.bumptech.glide.request.RequestOptions
import com.ctw.mediaselector.databinding.ActivityMediaPreviewBinding
import android.net.Uri
import android.media.MediaPlayer
import android.widget.SeekBar
import android.os.Handler
import android.os.Looper
import java.util.concurrent.TimeUnit
import android.widget.MediaController

class MediaPreviewActivity : AppCompatActivity() {
    
    private lateinit var binding: ActivityMediaPreviewBinding
    private lateinit var mediaFile: MediaFile
    private var mediaPlayer: MediaPlayer? = null
    private var isPlaying = false
    private val handler = Handler(Looper.getMainLooper())
    private var updateSeekBarRunnable: Runnable? = null
    
    // 选择相关的变量
    private var isSelected = false
    private var maxSelectCount = 0
    private var currentSelectCount = 0
    private var allowedTypes = MediaType.ALL
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMediaPreviewBinding.inflate(layoutInflater)
        setContentView(binding.root)
        
        // 获取传递的媒体文件信息
        val filePath = intent.getStringExtra(EXTRA_FILE_PATH)
        val fileName = intent.getStringExtra(EXTRA_FILE_NAME)
        val fileType = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            intent.getSerializableExtra(EXTRA_FILE_TYPE, MediaType::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getSerializableExtra(EXTRA_FILE_TYPE) as? MediaType
        }
        
        // 获取选择相关参数
        isSelected = intent.getBooleanExtra(EXTRA_IS_SELECTED, false)
        maxSelectCount = intent.getIntExtra(EXTRA_MAX_SELECT_COUNT, 0)
        currentSelectCount = intent.getIntExtra(EXTRA_CURRENT_SELECT_COUNT, 0)
        allowedTypes = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            intent.getSerializableExtra(EXTRA_ALLOWED_TYPES, MediaType::class.java) ?: MediaType.ALL
        } else {
            @Suppress("DEPRECATION")
            intent.getSerializableExtra(EXTRA_ALLOWED_TYPES) as? MediaType ?: MediaType.ALL
        }
        
        if (filePath == null || fileName == null || fileType == null) {
            Toast.makeText(this, "参数错误", Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        
        mediaFile = MediaFile(
            id = 0,
            name = fileName,
            path = filePath,
            type = fileType,
            dateAdded = 0,
            isSelected = isSelected
        )
        
        setupToolbar()
        setupSelectionButton()
        setupPreview()
    }
    
    private fun setupToolbar() {
        binding.toolbar.apply {
            title = mediaFile.name
            setNavigationOnClickListener { 
                finishWithResult()
            }
        }
    }
    
    private fun setupSelectionButton() {
        updateSelectionUI()
        
        binding.flSelectionContainer.setOnClickListener {
            if (canToggleSelection()) {
                isSelected = !isSelected
                mediaFile.isSelected = isSelected
                updateSelectionUI()
            }
        }
    }
    
    private fun canToggleSelection(): Boolean {
        // 如果当前已选中，可以取消选择
        if (isSelected) return true
        
        // 检查类型限制
        if (allowedTypes != MediaType.ALL && mediaFile.type != allowedTypes) {
            val typeName = when (allowedTypes) {
                MediaType.IMAGE -> "图片"
                MediaType.VIDEO -> "视频"
                else -> "文件"
            }
            Toast.makeText(this, "只能选择${typeName}文件", Toast.LENGTH_SHORT).show()
            return false
        }
        
        // 检查数量限制
        if (maxSelectCount > 0 && currentSelectCount >= maxSelectCount) {
            Toast.makeText(this, "最多只能选择${maxSelectCount}个文件", Toast.LENGTH_SHORT).show()
            return false
        }
        
        return true
    }
    
    private fun updateSelectionUI() {
        binding.cvSelectionIndicator.apply {
            if (isSelected) {
                setCardBackgroundColor(getColor(R.color.md_theme_light_primary))
                strokeColor = getColor(R.color.md_theme_light_primary)
                binding.ivCheckMark.isVisible = true
            } else {
                setCardBackgroundColor(getColor(android.R.color.transparent))
                strokeColor = getColor(android.R.color.white)
                binding.ivCheckMark.isVisible = false
            }
        }
    }
    
    private fun finishWithResult() {
        val resultIntent = Intent().apply {
            putExtra(EXTRA_FILE_PATH, mediaFile.path)
            putExtra(EXTRA_IS_SELECTED, isSelected)
        }
        setResult(Activity.RESULT_OK, resultIntent)
        finish()
    }
    
    override fun onBackPressed() {
        finishWithResult()
    }
    
    private fun setupPreview() {
        when (mediaFile.type) {
            MediaType.IMAGE -> setupImagePreview()
            MediaType.VIDEO -> setupVideoPreview()
            MediaType.ALL -> {
                // 根据文件扩展名判断类型
                val extension = mediaFile.name.lowercase()
                when {
                    extension.endsWith(".jpg") || extension.endsWith(".jpeg") || 
                    extension.endsWith(".png") || extension.endsWith(".gif") || 
                    extension.endsWith(".webp") -> setupImagePreview()
                    else -> setupVideoPreview()
                }
            }
        }
    }
    
    private fun setupImagePreview() {
        binding.imageView.isVisible = true
        binding.videoContainer.isVisible = false
        binding.videoControls.isVisible = false
        binding.loadingProgress.isVisible = true
        
        val requestOptions = RequestOptions()
            .error(R.drawable.ic_error_thumbnail)
            .diskCacheStrategy(DiskCacheStrategy.RESOURCE)
        
        Glide.with(this)
            .load(Uri.parse("file://${mediaFile.path}"))
            .apply(requestOptions)
            .into(binding.imageView)
        
        binding.loadingProgress.isVisible = false
    }
    
    private fun setupVideoPreview() {
        binding.imageView.isVisible = false
        binding.videoContainer.isVisible = true
        binding.videoControls.isVisible = true
        binding.loadingProgress.isVisible = true
        
        // 确保VideoView获得焦点
        binding.videoView.requestFocus()
        
        setupVideoPlayer()
        setupVideoControls()
    }
    
    private fun setupVideoPlayer() {
        try {
            val videoUri = Uri.parse("file://${mediaFile.path}")
            println("MediaPreview: Loading video from: $videoUri")
            println("MediaPreview: File exists: ${java.io.File(mediaFile.path).exists()}")
            println("MediaPreview: File size: ${java.io.File(mediaFile.path).length()}")
            
            // 设置MediaController
            val mediaController = MediaController(this)
            mediaController.setAnchorView(binding.videoView)
            binding.videoView.setMediaController(mediaController)
            
            // 尝试直接设置视频路径
            binding.videoView.setVideoPath(mediaFile.path)
            
            binding.videoView.setOnPreparedListener { mp ->
                println("MediaPreview: Video prepared")
                mediaPlayer = mp
                binding.loadingProgress.isVisible = false
                
                // 设置视频时长
                val duration = mp.duration
                binding.seekBar.max = duration
                val totalTime = formatTime(duration.toLong())
                binding.tvDuration.text = "00:00 / $totalTime"
                
                println("MediaPreview: Video duration: $duration ms")
                
                // 设置视频缩放模式
                try {
                    mp.setVideoScalingMode(MediaPlayer.VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING)
                } catch (e: Exception) {
                    println("MediaPreview: Failed to set scaling mode: ${e.message}")
                }
                
                // 强制刷新布局
                binding.videoView.requestLayout()
                binding.videoContainer.requestLayout()
                
                mp.setOnCompletionListener {
                    println("MediaPreview: Video playback completed")
                    isPlaying = false
                    binding.btnPlayPause.setImageResource(R.drawable.ic_play_arrow)
                    stopSeekBarUpdate()
                    // 播放完成后重置到开头
                    binding.videoView.seekTo(0)
                    binding.seekBar.progress = 0
                    binding.tvDuration.text = "00:00 / $totalTime"
                }
                
                // 等待一小段时间后自动播放，确保准备完成
                handler.postDelayed({
                    playVideo()
                }, 100)
            }
            
            binding.videoView.setOnErrorListener { _, what, extra ->
                binding.loadingProgress.isVisible = false
                Toast.makeText(this, "视频播放错误: $what, $extra", Toast.LENGTH_SHORT).show()
                true
            }
            
            binding.videoView.setOnInfoListener { _, what, _ ->
                when (what) {
                    MediaPlayer.MEDIA_INFO_VIDEO_RENDERING_START -> {
                        // 视频开始渲染
                        binding.loadingProgress.isVisible = false
                    }
                    MediaPlayer.MEDIA_INFO_BUFFERING_START -> {
                        binding.loadingProgress.isVisible = true
                    }
                    MediaPlayer.MEDIA_INFO_BUFFERING_END -> {
                        binding.loadingProgress.isVisible = false
                    }
                }
                false
            }
            
        } catch (e: Exception) {
            binding.loadingProgress.isVisible = false
            Toast.makeText(this, "无法播放视频文件: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun setupVideoControls() {
        binding.btnPlayPause.setOnClickListener {
            if (isPlaying) {
                pauseVideo()
            } else {
                playVideo()
            }
        }
        
        // 点击视频区域也能控制播放/暂停
        binding.videoView.setOnClickListener {
            if (isPlaying) {
                pauseVideo()
            } else {
                playVideo()
            }
        }
        
        binding.seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    binding.videoView.seekTo(progress)
                }
            }
            
            override fun onStartTrackingTouch(seekBar: SeekBar?) {
                stopSeekBarUpdate()
            }
            
            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                if (isPlaying) {
                    startSeekBarUpdate()
                }
            }
        })
    }
    
    private fun playVideo() {
        try {
            println("MediaPreview: Starting video playback")
            binding.videoView.start()
            isPlaying = true
            binding.btnPlayPause.setImageResource(R.drawable.ic_pause)
            startSeekBarUpdate()
            println("MediaPreview: Video started, isPlaying: ${binding.videoView.isPlaying}")
        } catch (e: Exception) {
            println("MediaPreview: Failed to start video: ${e.message}")
            Toast.makeText(this, "无法播放视频: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun pauseVideo() {
        binding.videoView.pause()
        isPlaying = false
        binding.btnPlayPause.setImageResource(R.drawable.ic_play_arrow)
        stopSeekBarUpdate()
    }
    
    private fun startSeekBarUpdate() {
        updateSeekBarRunnable = object : Runnable {
            override fun run() {
                if (isPlaying && binding.videoView.isPlaying) {
                    val currentPosition = binding.videoView.currentPosition
                    val duration = binding.videoView.duration
                    binding.seekBar.progress = currentPosition
                    // 显示当前时间和总时长
                    val currentTime = formatTime(currentPosition.toLong())
                    val totalTime = formatTime(duration.toLong())
                    binding.tvDuration.text = "$currentTime / $totalTime"
                    handler.postDelayed(this, 1000)
                }
            }
        }
        handler.post(updateSeekBarRunnable!!)
    }
    
    private fun stopSeekBarUpdate() {
        updateSeekBarRunnable?.let {
            handler.removeCallbacks(it)
        }
    }
    
    private fun formatTime(timeMs: Long): String {
        val minutes = TimeUnit.MILLISECONDS.toMinutes(timeMs)
        val seconds = TimeUnit.MILLISECONDS.toSeconds(timeMs) % 60
        return String.format("%02d:%02d", minutes, seconds)
    }
    
    override fun onResume() {
        super.onResume()
        // 如果是视频且之前在播放，恢复播放
        if (mediaFile.type == MediaType.VIDEO && ::mediaFile.isInitialized) {
            binding.videoView.resume()
        }
    }
    
    override fun onPause() {
        super.onPause()
        if (isPlaying) {
            pauseVideo()
        }
        // 暂停VideoView
        if (mediaFile.type == MediaType.VIDEO) {
            binding.videoView.pause()
        }
    }
    
    override fun onStop() {
        super.onStop()
        if (mediaFile.type == MediaType.VIDEO) {
            binding.videoView.stopPlayback()
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        stopSeekBarUpdate()
        if (mediaFile.type == MediaType.VIDEO) {
            binding.videoView.stopPlayback()
        }
        mediaPlayer?.release()
        mediaPlayer = null
    }
    
    companion object {
        const val EXTRA_FILE_PATH = "file_path"
        const val EXTRA_FILE_NAME = "file_name"
        const val EXTRA_FILE_TYPE = "file_type"
        const val EXTRA_IS_SELECTED = "is_selected"
        const val EXTRA_MAX_SELECT_COUNT = "max_select_count"
        const val EXTRA_CURRENT_SELECT_COUNT = "current_select_count"
        const val EXTRA_ALLOWED_TYPES = "allowed_types"
    }
} 