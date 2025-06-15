package com.ctw.mediaselector

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.provider.MediaStore
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.tabs.TabLayout
import android.Manifest
import android.content.pm.PackageManager
import android.widget.Toast
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import android.app.AlertDialog
import android.os.Build
import com.bumptech.glide.Glide
import android.widget.Button
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.ctw.mediaselector.databinding.ActivityMediaSelectorBinding
import kotlinx.coroutines.Dispatchers
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch
import android.net.Uri
import android.content.ContentUris
import kotlinx.coroutines.withContext
import com.ctw.mediaselector.MediaLoader
import androidx.core.view.isVisible
import androidx.activity.result.contract.ActivityResultContracts


class MediaSelectorActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMediaSelectorBinding
    private lateinit var mediaAdapter: MediaAdapter
    internal val selectedItems = mutableSetOf<String>()
    private var currentMediaType = MediaType.ALL
    
    // 选择限制参数
    private var maxSelectCount = 0 // 0表示无限制
    private var allowedTypes = MediaType.ALL
    private var minSelectCount = 0
    
    // 预览页面启动器
    private val previewLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            result.data?.let { data ->
                val filePath = data.getStringExtra(MediaPreviewActivity.EXTRA_FILE_PATH)
                val isSelected = data.getBooleanExtra(MediaPreviewActivity.EXTRA_IS_SELECTED, false)
                
                filePath?.let { path ->
                    // 找到对应的媒体文件并更新选择状态
                    updateFileSelectionFromPreview(path, isSelected)
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMediaSelectorBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // 获取配置参数
        getConfigFromIntent()
        
        setupToolbar()
        setupTabLayout()
        setupRecyclerView()
        setupButtons()
        checkPermissions()
    }
    
    private fun getConfigFromIntent() {
        maxSelectCount = intent.getIntExtra(EXTRA_MAX_SELECT_COUNT, 0)
        minSelectCount = intent.getIntExtra(EXTRA_MIN_SELECT_COUNT, 0)
        allowedTypes = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            intent.getSerializableExtra(EXTRA_ALLOWED_TYPES, MediaType::class.java) ?: MediaType.ALL
        } else {
            @Suppress("DEPRECATION")
            intent.getSerializableExtra(EXTRA_ALLOWED_TYPES) as? MediaType ?: MediaType.ALL
        }
        
        // 如果指定了类型限制，设置默认选中的Tab
        currentMediaType = allowedTypes
    }

    private fun setupToolbar() {
        binding.toolbar.setNavigationOnClickListener {
            finish()
        }
        
        // 更新标题显示选择限制信息
        val titleSuffix = when {
            maxSelectCount > 0 -> " (最多${maxSelectCount}个)"
            minSelectCount > 0 -> " (至少${minSelectCount}个)"
            else -> ""
        }
        binding.toolbar.title = "选择文件$titleSuffix"
    }

    private fun setupTabLayout() {
        // 根据允许的类型设置Tab
        when (allowedTypes) {
            MediaType.ALL -> {
                binding.tabLayout.addTab(binding.tabLayout.newTab().setText("全部"))
                binding.tabLayout.addTab(binding.tabLayout.newTab().setText("视频"))
                binding.tabLayout.addTab(binding.tabLayout.newTab().setText("图片"))
            }
            MediaType.VIDEO -> {
                binding.tabLayout.addTab(binding.tabLayout.newTab().setText("视频"))
                binding.tabLayout.visibility = android.view.View.GONE // 只有一个选项时隐藏Tab
            }
            MediaType.IMAGE -> {
                binding.tabLayout.addTab(binding.tabLayout.newTab().setText("图片"))
                binding.tabLayout.visibility = android.view.View.GONE // 只有一个选项时隐藏Tab
            }
        }

        binding.tabLayout.addOnTabSelectedListener(object : TabLayout.OnTabSelectedListener {
            override fun onTabSelected(tab: TabLayout.Tab?) {
                currentMediaType = when (allowedTypes) {
                    MediaType.ALL -> when (tab?.position) {
                        0 -> MediaType.ALL
                        1 -> MediaType.VIDEO
                        2 -> MediaType.IMAGE
                        else -> MediaType.ALL
                    }
                    else -> allowedTypes
                }
                loadMediaFiles()
            }
            override fun onTabUnselected(tab: TabLayout.Tab?) {}
            override fun onTabReselected(tab: TabLayout.Tab?) {}
        })
    }

    private fun setupRecyclerView() {
        binding.recyclerView.layoutManager = GridLayoutManager(this, 3)
        mediaAdapter = MediaAdapter(
            emptyList(),
            onItemSelected = { mediaFile, isSelected ->
                var shouldUpdate = true
                
                // 如果是要选择文件（而不是取消选择），才检查数量限制
                if (isSelected && !canSelectMore()) {
                    Toast.makeText(this, "最多只能选择${maxSelectCount}个文件", Toast.LENGTH_SHORT).show()
                    shouldUpdate = false
                }
                
                // 如果是要选择文件（而不是取消选择），才检查类型限制
                if (isSelected && !isTypeAllowed(mediaFile.type)) {
                    val typeName = when (allowedTypes) {
                        MediaType.IMAGE -> "图片"
                        MediaType.VIDEO -> "视频"
                        else -> "该类型"
                    }
                    Toast.makeText(this, "只能选择${typeName}文件", Toast.LENGTH_SHORT).show()
                    shouldUpdate = false
                }
                
                if (shouldUpdate) {
                    mediaFile.isSelected = isSelected
                    updateSelectedCount()
                    // 只刷新特定项而不是整个列表，提升响应速度
                    val position = mediaAdapter.currentMediaList.indexOf(mediaFile)
                    if (position != -1) {
                        mediaAdapter.notifyItemChanged(position)
                    }
                }
            },
            onItemClicked = { mediaFile ->
                openPreview(mediaFile)
            }
        )
        binding.recyclerView.adapter = mediaAdapter
    }
    
    private fun canSelectMore(): Boolean {
        return maxSelectCount <= 0 || mediaAdapter.getSelectedCount() < maxSelectCount
    }
    
    private fun isTypeAllowed(type: MediaType): Boolean {
        return allowedTypes == MediaType.ALL || allowedTypes == type
    }

    private fun setupButtons() {
        binding.btnCancel.setOnClickListener { finish() }
        binding.btnConfirm.setOnClickListener {
            val selectedCount = mediaAdapter.getSelectedCount()
            
            // 检查最小选择数量
            if (minSelectCount > 0 && selectedCount < minSelectCount) {
                Toast.makeText(this, "至少需要选择${minSelectCount}个文件", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            
            val selectedFiles = mediaAdapter.getSelectedItems().map { it.path }
            Intent().apply {
                putStringArrayListExtra(EXTRA_SELECTED_FILES, ArrayList(selectedFiles))
            }.let {
                setResult(RESULT_OK, it)
            }
            finish()
        }
    }

    private fun updateSelectedCount() {
        val count = mediaAdapter.getSelectedCount()
        val countText = if (maxSelectCount > 0) {
            "已选择 $count/$maxSelectCount 个文件"
        } else {
            "已选择 $count 个文件"
        }
        binding.tvSelectedCount.text = countText
        
        // 更新确认按钮状态
        val minMet = minSelectCount <= 0 || count >= minSelectCount
        binding.btnConfirm.isEnabled = count > 0 && minMet
    }
    
    private fun openPreview(mediaFile: MediaFile) {
        Intent(this, MediaPreviewActivity::class.java).apply {
            putExtra(MediaPreviewActivity.EXTRA_FILE_PATH, mediaFile.path)
            putExtra(MediaPreviewActivity.EXTRA_FILE_NAME, mediaFile.name)
            putExtra(MediaPreviewActivity.EXTRA_FILE_TYPE, mediaFile.type)
            putExtra(MediaPreviewActivity.EXTRA_IS_SELECTED, mediaFile.isSelected)
            putExtra(MediaPreviewActivity.EXTRA_MAX_SELECT_COUNT, maxSelectCount)
            putExtra(MediaPreviewActivity.EXTRA_CURRENT_SELECT_COUNT, mediaAdapter.getSelectedCount())
            putExtra(MediaPreviewActivity.EXTRA_ALLOWED_TYPES, allowedTypes)
        }.let {
            previewLauncher.launch(it)
        }
    }
    
    private fun updateFileSelectionFromPreview(filePath: String, isSelected: Boolean) {
        // 在当前列表中找到对应文件并更新选择状态
        val currentList = mediaAdapter.currentMediaList.toMutableList()
        val fileIndex = currentList.indexOfFirst { it.path == filePath }
        
        if (fileIndex != -1) {
            currentList[fileIndex].isSelected = isSelected
            mediaAdapter.updateData(currentList)
            updateSelectedCount()
        }
    }

    private fun showLoadingState(show: Boolean) {
        binding.llLoadingState.isVisible = show
        binding.recyclerView.isVisible = !show
        binding.llEmptyState.isVisible = false
    }

    private fun showEmptyState(show: Boolean) {
        binding.llEmptyState.isVisible = show
        binding.recyclerView.isVisible = !show
        binding.llLoadingState.isVisible = false
    }

    private fun checkPermissions() {
        val requiredPermissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(
                Manifest.permission.READ_MEDIA_IMAGES,
                Manifest.permission.READ_MEDIA_VIDEO
            )
        } else {
            arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE)
        }

        if (hasPermissions(*requiredPermissions)) {
            loadMediaFiles()
        } else {
            requestNeededPermissions(requiredPermissions)
        }
    }

    private fun hasPermissions(vararg permissions: String): Boolean {
        return permissions.all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun loadMediaFiles() {
        showLoadingState(true)
        lifecycleScope.launch {
            try {
                val files = withContext(Dispatchers.IO) {
                    MediaLoader.loadMediaFiles(this@MediaSelectorActivity, currentMediaType)
                }
                
                if (files.isEmpty()) {
                    showEmptyState(true)
                } else {
                    showLoadingState(false)
                    mediaAdapter.updateData(files)
                    updateSelectedCount()
                }
            } catch (e: Exception) {
                showEmptyState(true)
                Toast.makeText(this@MediaSelectorActivity, "加载媒体文件失败", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun requestNeededPermissions(permissions: Array<String>) {
        ActivityCompat.requestPermissions(this, permissions, PERMISSION_REQUEST_CODE)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSION_REQUEST_CODE && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            loadMediaFiles()
        } else {
            Toast.makeText(this, "需要权限才能使用此功能", Toast.LENGTH_SHORT).show()
            finish()
        }
    }

    companion object {
        private const val PERMISSION_REQUEST_CODE = 1001
        const val REQUEST_CODE_MEDIA_SELECT = 1001
        const val EXTRA_SELECTED_FILES = "selected_files"
        const val EXTRA_MAX_SELECT_COUNT = "max_select_count"
        const val EXTRA_MIN_SELECT_COUNT = "min_select_count"
        const val EXTRA_ALLOWED_TYPES = "allowed_types"
    }
}