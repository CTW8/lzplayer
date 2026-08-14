package com.example.lzplayer.console

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.Gravity
import android.view.WindowManager
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.ctw.mediaselector.MediaSelectorActivity
import com.ctw.mediaselector.MediaType
import com.example.lzplayer.R
import com.example.lzplayer_core.IMediaPlayerListener
import com.example.lzplayer_core.IVEPlayerListener
import com.example.lzplayer_core.PlayerStats
import com.example.lzplayer_core.TrackInfo
import com.example.lzplayer_core.VEPlayer

/**
 * 播放器测试台。
 *
 * 与消费级播放界面的区别在于职责：这里的每个控件都对应一项已实现的能力，
 * 每项能力都配一个当场可读的读数。平时埋在 logcat 里的东西（解码路径、
 * 音视频偏移、缓冲水位）在这里是一等公民。
 *
 * 事件与读数分两条路：状态变化走 [IVEPlayerListener] 回调（来一条记一条），
 * 数值读数搭既有的进度回调节奏拉取 [VEPlayer.getStats]，不额外开定时器。
 */
class ConsoleActivity : AppCompatActivity(), SurfaceHolder.Callback, IVEPlayerListener {

    companion object {
        private const val TAG = "LZConsole"
        private const val REQ_PICK = 1001
        private const val RC_MEDIA_PERM = 1002
        private val SPEEDS = floatArrayOf(0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f)
        /** 控件在横屏下的自动隐藏延迟 */
        private const val AUTO_HIDE_MS = 3000L
        /** 与 native VEDef.h 的 VE_INFO_DECODER_FALLBACK 对应 */
        private const val VE_INFO_DECODER_FALLBACK = 0x3001

        /**
         * 用 adb 直接带片源起播，省去在输入框里打路径（中文输入法会把
         * 斜杠和空格吃掉，自动化脚本根本没法用）：
         *
         *   adb shell am start -n com.example.lzplayer/.console.ConsoleActivity \
         *        --es source /sdcard/Movies/test.mp4 --ez autoplay true
         */
        const val EXTRA_SOURCE = "source"
        const val EXTRA_AUTOPLAY = "autoplay"
        /**
         * 策略开关也可由 intent 指定，否则只能手点诊断面板，回归脚本没法跑。
         * 与面板开关同义：在建链之前生效。
         *
         *   --ez software true --ez vulkan true
         *
         * 注意 vulkan 只作用于软解，单开它不会有任何变化。
         */
        const val EXTRA_FORCE_SOFTWARE = "software"
        const val EXTRA_PREFER_VULKAN = "vulkan"
    }

    private lateinit var etSource: EditText
    private lateinit var surfaceView: SurfaceView
    private lateinit var subtitleView: SubtitleOverlayView
    private lateinit var chipDecoder: TextView
    private lateinit var chipState: TextView
    private lateinit var bufferScrim: View
    private lateinit var tvBuffering: TextView
    private lateinit var seekBar: SeekBar
    private lateinit var tvPosition: TextView
    private lateinit var tvBuffered: TextView
    private lateinit var tvDuration: TextView
    private lateinit var btnPlayPause: TextView
    private lateinit var btnLoop: TextView
    private lateinit var speedRow: LinearLayout
    private lateinit var controls: View
    private lateinit var stage: FrameLayout
    private lateinit var sourceBar: View
    private lateinit var topBars: View

    private var player: VEPlayer? = null
    private var surfaceReady = false
    private var prepared = false
    /**
     * 已 stop，需要重新打开片源才能再播。
     *
     * 按 MediaPlayer 语义 stop() 之后必须重新 prepare()——native 侧
     * VEPlayerDriver::start() 的白名单里就没有 MEDIA_PLAYER_STOPPED，会直接
     * 返回 -1。此前这个 -1 被 Java 层丢弃、UI 又照常让按钮可点，用户按下去
     * 完全没反应也没有任何提示。这个标志把那条非法路径在 UI 上显式化。
     */
    private var stoppedNeedsReopen = false
    private var playing = false
    private var looping = false
    private var durationMs = 0L
    private var speedIndex = 2      // 1.0x
    private var draggingSeek = false
    /** selectTrack 计时起点，用来在面板里显示切轨耗时 */
    private var trackSwitchStartMs = 0L

    private val ui = Handler(Looper.getMainLooper())
    private val eventLog = EventLog()
    private var latestStats: PlayerStats = PlayerStats.empty()
    private var tracks: Array<TrackInfo> = emptyArray()

    private var trackSheet: TrackSheet? = null
    private var diagSheet: DiagnosticsSheet? = null

    private val hideControls = Runnable { setControlsVisible(false) }

    // ---------------------------------------------------------------- 生命周期

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_console)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        bindViews()
        buildSpeedRow()
        wireControls()

        surfaceView.holder.addCallback(this)
        // 画幅跟着舞台尺寸走：转屏、控件显隐都会改变舞台大小，
        // 靠 post 猜时机会拿到旧尺寸，必须由布局回调驱动
        stage.addOnLayoutChangeListener { _, l, t, r, b, oldL, oldT, oldR, oldB ->
            if ((r - l) != (oldR - oldL) || (b - t) != (oldB - oldT)) {
                applyVideoAspect()
            }
        }
        applyOrientation(resources.configuration.orientation)
        ensureMediaPermissions()
        handleLaunchIntent(intent)
    }

    /**
     * 申请读媒体所需权限。
     *
     * Android 13(API 33) 起按类型拆分：只有 READ_MEDIA_VIDEO 时，纯音频文件
     * 会在 native 侧 open 失败并上报 "demux open failed"，看着像解封装不支持，
     * 实际是权限。这里三类一起要，测试台需要能放任意本地媒体。
     */
    private fun ensureMediaPermissions() {
        val needed = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(
                Manifest.permission.READ_MEDIA_VIDEO,
                Manifest.permission.READ_MEDIA_AUDIO,
            )
        } else {
            arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE)
        }
        val missing = needed.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, missing.toTypedArray(), RC_MEDIA_PERM)
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != RC_MEDIA_PERM) return
        val denied = permissions.filterIndexed { i, _ ->
            grantResults.getOrNull(i) != PackageManager.PERMISSION_GRANTED
        }
        if (denied.isNotEmpty()) {
            eventLog.warn("PERMISSION", "denied: ${denied.joinToString()}")
            Toast.makeText(this, "缺少媒体读取权限，部分文件无法打开", Toast.LENGTH_LONG).show()
        }
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        intent?.let { handleLaunchIntent(it) }
    }

    /** adb 带 --es source 起播时走这里，见 [EXTRA_SOURCE] */
    private fun handleLaunchIntent(i: Intent) {
        val path = i.getStringExtra(EXTRA_SOURCE)?.trim()
        if (path.isNullOrEmpty()) return
        autoPlayWhenPrepared = i.getBooleanExtra(EXTRA_AUTOPLAY, false)
        // 策略必须在 openSource 之前落到字段上：openSource 会新建 native
        // 播放器并把这些字段重新下发一遍
        if (i.hasExtra(EXTRA_FORCE_SOFTWARE)) {
            forceSoftware = i.getBooleanExtra(EXTRA_FORCE_SOFTWARE, false)
            eventLog.warn("POLICY", "强制软解 ${if (forceSoftware) "开" else "关"}（intent）")
        }
        if (i.hasExtra(EXTRA_PREFER_VULKAN)) {
            preferVulkan = i.getBooleanExtra(EXTRA_PREFER_VULKAN, false)
            eventLog.warn("POLICY", "Vulkan 渲染 ${if (preferVulkan) "开" else "关"}（intent）")
        }
        etSource.setText(path)
        eventLog.info("LAUNCH_INTENT", path)
        if (surfaceReady) {
            openSource(path)
        } else {
            // onCreate 阶段 surface 还没创建。此时建链会让硬解工厂因为
            // 拿不到 Surface 而退回软解——自动化跑出来全是软解，回归结论
            // 就废了。等 surfaceChanged 再打开。
            pendingIntentSource = path
        }
    }

    private fun bindViews() {
        etSource = findViewById(R.id.etSource)
        surfaceView = findViewById(R.id.surfaceView)
        subtitleView = findViewById(R.id.subtitleView)
        chipDecoder = findViewById(R.id.chipDecoder)
        chipState = findViewById(R.id.chipState)
        bufferScrim = findViewById(R.id.bufferScrim)
        tvBuffering = findViewById(R.id.tvBuffering)
        seekBar = findViewById(R.id.seekBar)
        tvPosition = findViewById(R.id.tvPosition)
        tvBuffered = findViewById(R.id.tvBuffered)
        tvDuration = findViewById(R.id.tvDuration)
        btnPlayPause = findViewById(R.id.btnPlayPause)
        btnLoop = findViewById(R.id.btnLoop)
        speedRow = findViewById(R.id.speedRow)
        controls = findViewById(R.id.controls)
        stage = findViewById(R.id.stage)
        sourceBar = findViewById(R.id.sourceBar)
        topBars = findViewById(R.id.topBars)
    }

    private fun wireControls() {
        findViewById<View>(R.id.btnPickFile).setOnClickListener { pickLocalFile() }
        findViewById<View>(R.id.btnOpen).setOnClickListener { openSource(etSource.text.toString().trim()) }
        btnPlayPause.setOnClickListener { togglePlay() }
        findViewById<View>(R.id.btnStop).setOnClickListener { stopPlayback() }
        btnLoop.setOnClickListener { toggleLoop() }
        findViewById<View>(R.id.btnTracks).setOnClickListener { showTrackSheet() }
        findViewById<View>(R.id.btnSubtitle).setOnClickListener { showTrackSheet(focusSubtitle = true) }
        findViewById<View>(R.id.btnJump).setOnClickListener { showJumpDialog() }
        findViewById<View>(R.id.btnDiag).setOnClickListener { showDiagnosticsSheet() }

        // 横屏下点画面唤出控件
        stage.setOnClickListener {
            if (isLandscape()) setControlsVisible(controls.visibility != View.VISIBLE)
        }

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser && durationMs > 0) {
                    tvPosition.text = formatTime(progress * durationMs / 1000)
                }
            }
            override fun onStartTrackingTouch(sb: SeekBar) {
                draggingSeek = true
                ui.removeCallbacks(hideControls)
            }
            override fun onStopTrackingTouch(sb: SeekBar) {
                draggingSeek = false
                if (prepared && durationMs > 0) {
                    val target = sb.progress * durationMs / 1000.0
                    eventLog.info("SEEK", "→ ${formatTime(target.toLong())}")
                    trackSwitchStartMs = System.currentTimeMillis()
                    player?.seekTo(target)
                }
                scheduleAutoHide()
            }
        })
    }

    private fun buildSpeedRow() {
        speedRow.removeAllViews()
        SPEEDS.forEachIndexed { i, speed ->
            val tv = TextView(this).apply {
                text = if (speed == 1.0f) "1.0" else speed.toString()
                gravity = android.view.Gravity.CENTER
                setPadding(0, dp(6), 0, dp(6))
                setBackgroundResource(R.drawable.con_btn_bg)
                typeface = android.graphics.Typeface.MONOSPACE
                setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 11f)
                setTextColor(ContextCompat.getColor(this@ConsoleActivity, R.color.con_ink_dim))
                isSelected = i == speedIndex
                setOnClickListener { applySpeed(i) }
            }
            val lp = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            if (i > 0) lp.marginStart = dp(4)
            speedRow.addView(tv, lp)
        }
        refreshSpeedRow()
    }

    private fun refreshSpeedRow() {
        for (i in 0 until speedRow.childCount) {
            val tv = speedRow.getChildAt(i) as TextView
            val on = i == speedIndex
            tv.isSelected = on
            tv.setTextColor(
                ContextCompat.getColor(this, if (on) R.color.con_accent else R.color.con_ink_dim)
            )
            tv.typeface = if (on) android.graphics.Typeface.create(
                android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD
            ) else android.graphics.Typeface.MONOSPACE
        }
    }

    // ---------------------------------------------------------------- 源与播放

    private fun pickLocalFile() {
        val intent = Intent(this, MediaSelectorActivity::class.java).apply {
            putExtra(MediaSelectorActivity.EXTRA_ALLOWED_TYPES, MediaType.VIDEO)
            putExtra(MediaSelectorActivity.EXTRA_MAX_SELECT_COUNT, 1)
        }
        startActivityForResult(intent, REQ_PICK)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_PICK || resultCode != RESULT_OK || data == null) return
        val paths = data.getStringArrayListExtra(MediaSelectorActivity.EXTRA_SELECTED_FILES)
        val path = paths?.firstOrNull()
        if (path.isNullOrEmpty()) {
            toast("没有取到文件路径")
            return
        }
        etSource.setText(path)
        openSource(path)
    }

    private fun openSource(source: String) {
        if (source.isEmpty()) {
            toast("先填个文件路径或 URL")
            return
        }
        releasePlayer()

        eventLog.clear()
        eventLog.info("OPEN", source)
        prepared = false
        playing = false
        durationMs = 0
        subtitleView.clear()
        tracks = emptyArray()
        // 复位进度与按钮：上一轮的时间码和"暂停"字样留在屏上，
        // 会让人以为还在播——测试台的读数必须可信
        btnPlayPause.text = "播放"
        seekBar.progress = 0
        seekBar.secondaryProgress = 0
        tvPosition.text = formatTime(0)
        tvDuration.text = formatTime(0)
        tvBuffered.text = ""
        latestStats = PlayerStats.empty()
        // 先恢复满屏：新片源的画幅要等 PREPARED 拿到轨道信息才知道，
        // 沿用上一个片源的尺寸会闪一下错误画幅
        (surfaceView.layoutParams as FrameLayout.LayoutParams).let {
            it.width = FrameLayout.LayoutParams.MATCH_PARENT
            it.height = FrameLayout.LayoutParams.MATCH_PARENT
            surfaceView.layoutParams = it
        }

        val p = VEPlayer()
        player = p
        p.registerListener(this)
        // openSource 每次都新建 native 播放器，策略开关不会自动继承。
        // 必须在 prepare 之前重新下发，否则面板上拨着"强制软解"、
        // 实际却还在走硬解——开关成了摆设。
        p.setForceSoftwareDecoder(forceSoftware)
        p.setForceSlesAudio(forceSles)
        p.setPreferVulkanRender(preferVulkan)
        stoppedNeedsReopen = false
        startupTraceDumped = false
        if (p.init(source) != 0) {
            eventLog.crit("INIT_FAILED", source)
            toast("打开失败：$source")
            return
        }
        if (surfaceReady) {
            p.setSurface(surfaceView.holder.surface, surfaceView.width, surfaceView.height)
        }
        p.setLooping(looping)
        // 网络源起播要等首包，先把缓冲遮罩挂上，别让界面几秒内毫无反应
        if (!source.startsWith("/")) showBuffering(true, -1)
        p.prepareAsync()
        updateHud()
    }

    /**
     * 起播后拉一次启播里程碑并打进事件流与 logcat。
     *
     * 为什么用延迟而不是等 FIRST_FRAME 事件：那个事件**启播时根本不发**——
     * native 侧 m_NotifyFirstFrame 只在 seek 路径置真。所以这里给渲染链一点
     * 时间把 T6/T7 走完，再主动拉取。步骤5 有了性能面板后改由面板按需拉，
     * 这段临时打印可以去掉。
     */
    private fun dumpStartupTrace() {
        val p = player ?: return
        val json = p.startupTraceJson
        eventLog.info("STARTUP_TRACE", json)
        Log.i(TAG, "STARTUP_TRACE $json")
        // 同时落盘。**不能只依赖 logcat**：部分 ROM(实测 ColorOS)对单进程
        // 日志有配额(LOG_FLOWCTRL "OVER PROC QUOTA")，本工程 native 层的
        // ALOGV 每秒就能打满，应用自己的日志会被静默丢弃——排查时会误以为
        // 代码没执行。落盘的这份是唯一可靠来源，也是后续跑分报告的雏形。
        runCatching {
            val dir = java.io.File(filesDir, "perf").apply { mkdirs() }
            java.io.File(dir, "last-startup.json").writeText(json)
            // 稳态读数原样落一份：步骤4 的解析类还没写，先靠原始 JSON 核对
            java.io.File(dir, "last-stats.json").writeText(p.statsJsonRaw)
        }.onFailure { Log.w(TAG, "write perf files failed: ${it.message}") }
    }

    private fun togglePlay() {
        val p = player ?: return toast("还没打开片源")
        // 区分两种"不能播"：从没 prepare 过 vs 已 stop 需要重开。
        // 合用一句"还没 prepare 完"会把后者说成前者，误导排查方向
        if (stoppedNeedsReopen) {
            eventLog.warn("START_REJECTED", "已 stop，需重新打开片源")
            return toast("已停止。stop 后需重新打开片源才能播放")
        }
        if (!prepared) return toast("还没 prepare 完")
        if (playing) {
            p.pause()
            playing = false
            eventLog.info("PAUSE", "")
        } else {
            // **必须检查返回值**：VEPlayerDriver 的 start/stop/pause 全都返回
            // 状态码，而 Java 侧此前一律忽略——这次的"按了没反应"正是这么来的。
            // 静默失败会以同样方式在别的状态组合上再次出现
            val ret = p.start()
            if (ret != 0) {
                eventLog.crit("START_FAILED", "native 拒绝, ret=$ret")
                toast("播放失败（native 返回 $ret）")
                updateHud()
                return
            }
            playing = true
            eventLog.info("START", "")
            Log.d(TAG, "START dispatched, waiting for first progress to dump trace")
        }
        btnPlayPause.text = if (playing) "暂停" else "播放"
        updateHud()
        scheduleAutoHide()
    }

    private fun stopPlayback() {
        val p = player ?: return
        p.stop()
        playing = false
        // stop 后 native 拒绝 start，这里必须同步失效，不能让按钮继续邀请点击
        prepared = false
        stoppedNeedsReopen = true
        btnPlayPause.text = "播放"
        subtitleView.clear()
        eventLog.info("STOP", "")
        updateHud()
    }

    private fun toggleLoop() {
        looping = !looping
        player?.setLooping(looping)
        btnLoop.text = if (looping) "循环 开" else "循环 关"
        btnLoop.isSelected = looping
    }

    private fun applySpeed(index: Int) {
        val p = player
        val speed = SPEEDS[index]
        if (p == null) {
            speedIndex = index
            refreshSpeedRow()
            return
        }
        val ret = p.setPlaySpeed(speed)
        if (ret != 0) {
            eventLog.warn("SPEED_REJECTED", "${speed}x ret=$ret")
            toast("设置速率失败：$ret")
            return
        }
        speedIndex = index
        refreshSpeedRow()
        eventLog.ok("SPEED", "${speed}x")
        updateHud()
    }

    private fun showJumpDialog() {
        val input = EditText(this).apply {
            hint = "秒，如 42.5"
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
            setPadding(dp(20), dp(16), dp(20), dp(16))
        }
        AlertDialog.Builder(this)
            .setTitle("精准跳转")
            .setView(input)
            .setPositiveButton("跳转") { _, _ ->
                val sec = input.text.toString().toDoubleOrNull() ?: return@setPositiveButton
                trackSwitchStartMs = System.currentTimeMillis()
                eventLog.info("SEEK", "→ ${formatTime((sec * 1000).toLong())}")
                player?.seekTo(sec * 1000.0)
            }
            .setNegativeButton("取消", null)
            .show()
    }

    // ---------------------------------------------------------------- 面板

    private fun showTrackSheet(focusSubtitle: Boolean = false) {
        val p = player ?: return toast("还没打开片源")
        if (!prepared) return toast("还没 prepare 完")
        tracks = p.trackInfo
        trackSheet = TrackSheet(
            context = this,
            tracks = tracks,
            focusSubtitle = focusSubtitle,
            onSelect = { track ->
                trackSwitchStartMs = System.currentTimeMillis()
                eventLog.info("SELECT_TRACK", "轨 ${track.index} ${track.type}")
                p.selectTrack(track.index)
            },
            onDeselectSubtitle = {
                // 关闭字幕：先本地清掉，别等 native 的 CLEAR 事件绕一圈
                val active = tracks.firstOrNull { it.isSubtitle && it.active }
                if (active != null) p.deselectTrack(active.index)
                subtitleView.clear()
                eventLog.info("DESELECT_TRACK", "字幕关闭")
            },
            onLoadExternal = { showExternalSubtitleDialog() }
        ).also { it.show() }
    }

    private fun showExternalSubtitleDialog() {
        val input = EditText(this).apply {
            hint = "/sdcard/Movies/movie.srt"
            inputType = InputType.TYPE_TEXT_VARIATION_URI
            setPadding(dp(20), dp(16), dp(20), dp(16))
        }
        AlertDialog.Builder(this)
            .setTitle("加载外挂字幕")
            .setView(input)
            .setPositiveButton("加载") { _, _ ->
                val path = input.text.toString().trim()
                if (path.isEmpty()) return@setPositiveButton
                val ret = player?.addExternalSubtitle(path) ?: -1
                if (ret == 0) {
                    eventLog.ok("SUBTITLE_LOADED", path)
                    toast("已加载，去轨道面板里选中它")
                } else {
                    eventLog.crit("SUBTITLE_LOAD_FAILED", path)
                    toast("加载失败：$path")
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }

    private fun showDiagnosticsSheet() {
        diagSheet = DiagnosticsSheet(
            context = this,
            eventLog = eventLog,
            // 直接问播放器要：暂停时 onProgress 不再回调，缓存会停在最后一帧的值
            statsProvider = { player?.stats ?: PlayerStats.empty() },
            statsJsonProvider = { player?.statsJsonRaw ?: "{}" },
            startupTraceProvider = { player?.startupTraceJson ?: "{\"valid\":false}" },
            seekTraceProvider = { player?.seekTraceJson ?: "{\"count\":0,\"items\":[]}" },
            forceSoftware = forceSoftware,
            forceSles = forceSles,
            preferVulkan = preferVulkan,
            // 用 player?. 而不是捕获的引用：面板可能跨越一次换源
            onForceSoftware = { on ->
                forceSoftware = on
                player?.setForceSoftwareDecoder(on)
                eventLog.warn("POLICY", "强制软解 ${if (on) "开" else "关"}（下次 prepare 生效）")
            },
            onForceSles = { on ->
                forceSles = on
                player?.setForceSlesAudio(on)
                eventLog.warn("POLICY", "强制 SLES ${if (on) "开" else "关"}（下次 prepare 生效）")
            },
            onPreferVulkan = { on ->
                preferVulkan = on
                player?.setPreferVulkanRender(on)
                eventLog.warn("POLICY", "Vulkan 渲染 ${if (on) "开" else "关"}（下次 prepare 生效，仅软解）")
            }
        ).also { it.show() }
    }

    private var forceSoftware = false
    private var forceSles = false
    private var preferVulkan = false
    /** 每次换源只打印一次启播里程碑 */
    private var startupTraceDumped = false
    /** 由 intent 指定的自动起播，PREPARED 之后触发一次 */
    private var autoPlayWhenPrepared = false
    /** intent 带来的片源，等 surface 就绪后再打开(见 handleLaunchIntent) */
    private var pendingIntentSource: String? = null

    // ---------------------------------------------------------------- 读数刷新

    /**
     * 读数刷新。跟着 native 的进度回调走（500ms 一次），不另开定时器——
     * 多一个定时器就多一份空转唤醒，和这个播放器降 CPU 的目标相悖。
     */
    private fun refreshStats() {
        val p = player ?: return
        latestStats = p.stats
        updateHud()
        diagSheet?.takeIf { it.isShowing }?.update(latestStats)

        // 缓冲遮罩由 native 的 buffering 标志驱动，与事件互为印证
        if (latestStats.buffering) {
            showBuffering(true, -1)
        } else if (bufferScrim.visibility == View.VISIBLE && prepared) {
            showBuffering(false, 0)
        }

        if (latestStats.isNetworkSource && durationMs > 0) {
            val pct = (latestStats.bufferedMs + latestStats.positionMs) * 1000 / durationMs
            seekBar.secondaryProgress = pct.toInt().coerceIn(0, 1000)
            tvBuffered.text = "缓冲 ${latestStats.bufferedMs} ms"
        } else {
            seekBar.secondaryProgress = 1000
            tvBuffered.text = ""
        }
    }

    private fun updateHud() {
        val s = latestStats
        val decoderLabel = when (s.decoder) {
            "hardware" -> "HW · ${s.codec}"
            "software" -> "SW · ${s.codec}"
            else -> "—"
        }
        chipDecoder.text = decoderLabel
        // 回退成软解是需要一眼看到的状态：给它警示色，同时保留 SW 字样
        chipDecoder.setTextColor(
            ContextCompat.getColor(
                this,
                when (s.decoder) {
                    "hardware" -> R.color.con_ok
                    "software" -> R.color.con_warn
                    else -> R.color.con_ink_dim
                }
            )
        )
        val speedText = if (s.speed == 1.0f) "1.0×" else "${s.speed}×"
        chipState.text = "${s.state} · $speedText"
        chipState.setTextColor(
            ContextCompat.getColor(
                this,
                if (s.state == "ERROR") R.color.con_crit else R.color.con_ink
            )
        )
    }

    /**
     * 把 SurfaceView 调成源文件的显示宽高比（fit-inside 到舞台里，四周留黑）。
     *
     * 这是画幅正确的唯一着力点：硬解把画面直接交给 Surface，中间没有任何
     * 缩放环节，Surface 多大画面就被拉成多大。软解走 GLES 时同样受益——
     * Surface 比例对上之后，渲染器里的 fit-inside 自然算出 1:1，不再二次缩放。
     */
    private fun applyVideoAspect() {
        val track = tracks.firstOrNull { it.isVideo } ?: return
        if (track.width <= 0 || track.height <= 0) return
        // 旋转 90/270 后长短边互换，要按摆正后的画幅算
        val swapped = track.rotation == 90 || track.rotation == 270
        val videoW = if (swapped) track.height else track.width
        val videoH = if (swapped) track.width else track.height

        val stageW = stage.width
        val stageH = stage.height
        if (stageW <= 0 || stageH <= 0) {
            // 还没布局(PREPARED 早于首次 layout)：等 layout 回调再来
            return
        }
        val scale = minOf(stageW.toFloat() / videoW, stageH.toFloat() / videoH)
        val lp = surfaceView.layoutParams as FrameLayout.LayoutParams
        val w = (videoW * scale).toInt()
        val h = (videoH * scale).toInt()
        // 去重是必须的：设置 layoutParams 会再触发一轮 layout，
        // 不比较就会无限循环
        if (lp.width == w && lp.height == h) return
        lp.width = w
        lp.height = h
        lp.gravity = Gravity.CENTER
        surfaceView.layoutParams = lp
        Log.d(TAG, "video ${videoW}x$videoH (rot ${track.rotation}) -> surface ${w}x$h in ${stageW}x$stageH")
    }

    private fun showBuffering(show: Boolean, percent: Int) {
        bufferScrim.visibility = if (show) View.VISIBLE else View.GONE
        tvBuffering.text = if (percent >= 0) "缓冲中 · $percent%" else "缓冲中"
    }

    // ---------------------------------------------------------------- 横屏

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        applyOrientation(newConfig.orientation)
    }

    private fun isLandscape() =
        resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

    private fun applyOrientation(orientation: Int) {
        // 画幅由 stage 的布局回调负责重算，这里不用管
        val land = orientation == Configuration.ORIENTATION_LANDSCAPE
        // 横屏进沉浸：源栏收起，控件浮在画面上并自动隐藏。HUD 始终留着——
        // "回退成软解"这类状态变化不能在沉浸模式里丢失。
        sourceBar.visibility = if (land) View.GONE else View.VISIBLE
        topBars.visibility = if (land) View.GONE else View.VISIBLE
        if (land) {
            hideSystemBars()
            scheduleAutoHide()
        } else {
            showSystemBars()
            ui.removeCallbacks(hideControls)
            setControlsVisible(true)
        }
    }

    private fun setControlsVisible(visible: Boolean) {
        controls.visibility = if (visible) View.VISIBLE else View.GONE
        // 控件浮出时把字幕抬上去，别让进度条压住
        subtitleView.bottomInsetPx = if (visible) controls.height else 0
        if (visible) scheduleAutoHide()
    }

    private fun scheduleAutoHide() {
        ui.removeCallbacks(hideControls)
        if (isLandscape() && playing) ui.postDelayed(hideControls, AUTO_HIDE_MS)
    }

    @Suppress("DEPRECATION")
    private fun hideSystemBars() {
        window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        or View.SYSTEM_UI_FLAG_FULLSCREEN
                        or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
    }

    @Suppress("DEPRECATION")
    private fun showSystemBars() {
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_VISIBLE
    }

    // ---------------------------------------------------------------- Surface

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        surfaceReady = true
        player?.setSurface(holder.surface, width, height)
        // intent 带来的片源等的就是这一刻
        pendingIntentSource?.let {
            pendingIntentSource = null
            openSource(it)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        // 传 null 让渲染器停止画向已失效的窗口
        player?.setSurface(null, 0, 0)
    }

    // ---------------------------------------------------------------- 播放器回调

    override fun onInfo(type: Int, msg1: Int, obj: Any?) {
        ui.post { handleInfo(type, msg1, obj) }
    }

    private fun handleInfo(type: Int, msg1: Int, obj: Any?) {
        when (type) {
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_PREPARED -> {
                prepared = true
                durationMs = player?.duration ?: 0
                tvDuration.text = formatTime(durationMs)
                tracks = player?.trackInfo ?: emptyArray()
                showBuffering(false, 0)
                eventLog.ok("PREPARED", "${tracks.size} 轨 · ${formatTime(durationMs)}")
                applyVideoAspect()
                // 速率也不会被新 player 继承。必须等 PREPARED 之后再下发：
                // prepare 之前设的话音频渲染器还没建，sonic 拿不到这个速率，
                // 结果时钟按 N 倍走而声音还是 1 倍，同步直接崩。
                if (SPEEDS[speedIndex] != 1.0f) {
                    applySpeed(speedIndex)
                }
                refreshStats()
                if (autoPlayWhenPrepared) {
                    autoPlayWhenPrepared = false
                    togglePlay()
                }
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_FIRST_FRAME -> {
                eventLog.ok("FIRST_FRAME", "")
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_SEEK_DONE -> {
                val cost = System.currentTimeMillis() - trackSwitchStartMs
                eventLog.ok("SEEK_DONE", "${cost}ms")
                trackSheet?.takeIf { it.isShowing }?.showSwitchCost(cost)
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_TRACK_CHANGED -> {
                val cost = System.currentTimeMillis() - trackSwitchStartMs
                tracks = player?.trackInfo ?: tracks
                eventLog.ok("TRACK_CHANGED", "轨 $msg1 · ${cost}ms")
                trackSheet?.takeIf { it.isShowing }?.let {
                    it.updateTracks(tracks)
                    it.showSwitchCost(cost)
                }
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE -> {
                subtitleView.setText(obj?.toString())
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_SUBTITLE_CLEAR -> {
                subtitleView.clear()
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_START -> {
                showBuffering(true, msg1)
                eventLog.warn("BUFFERING_START", if (msg1 >= 0) "$msg1%" else "")
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_UPDATE -> {
                showBuffering(true, msg1)
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_BUFFERING_END -> {
                showBuffering(false, msg1)
                eventLog.ok("BUFFERING_END", if (msg1 >= 0) "$msg1%" else "")
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_EOS -> {
                eventLog.info("EOS", "")
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_COMPLETION -> {
                playing = false
                btnPlayPause.text = "播放"
                subtitleView.clear()
                eventLog.ok("COMPLETION", "")
            }
            IMediaPlayerListener.VE_PLAYER_NOTIFY_EVENT_ON_INFO -> {
                // 信息类事件共用这条通道，靠 msg1 区分具体是哪一种
                if (msg1 == VE_INFO_DECODER_FALLBACK) {
                    eventLog.warn("DECODER_FALLBACK", "硬解失败 → 软解")
                } else {
                    eventLog.info("INFO", "msg1=$msg1")
                }
            }
            else -> Log.d(TAG, "onInfo type=$type msg1=$msg1 obj=$obj")
        }
        updateHud()
    }

    override fun onError(type: Int, msg1: Int, msg2: Int, msg3: String?) {
        ui.post {
            eventLog.crit("ERROR", "$msg1 ${msg3 ?: ""}")
            playing = false
            btnPlayPause.text = "播放"
            showBuffering(false, 0)
            refreshStats()
            toast("播放出错：${msg3 ?: msg1}")
        }
    }

    override fun onProgress(progressMs: Double) {
        ui.post {
            if (!draggingSeek && durationMs > 0) {
                seekBar.progress = (progressMs * 1000 / durationMs).toInt().coerceIn(0, 1000)
            }
            tvPosition.text = formatTime(progressMs.toLong())
            // 借进度回调的节奏刷读数
            refreshStats()
            // 起播里程碑在首帧上屏后才齐全，而进度已经在推进说明那一刻早已过去。
            // 挂在这里而不是用 postDelayed：进度回调是确定会来的，延迟消息会被
            // 各种 removeCallbacks 之类的清理波及。
            // 每个 tick 覆盖写一次：里程碑是逐步补齐的(T7/T8 可能晚于首个
            // 进度回调)，只在首个 tick 写会得到一份"看着像漏采集"的残缺快照。
            // 文件很小，一秒两次写可以忽略；步骤5 由面板按需拉取后去掉。
            if (progressMs > 0) {
                startupTraceDumped = true
                dumpStartupTrace()
            }
        }
    }

    // ---------------------------------------------------------------- 收尾

    override fun onPause() {
        super.onPause()
        if (playing) {
            player?.pause()
            playing = false
            btnPlayPause.text = "播放"
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacksAndMessages(null)
        releasePlayer()
    }

    private fun releasePlayer() {
        player?.let {
            it.stop()
            it.release()
        }
        player = null
        prepared = false
    }

    // ---------------------------------------------------------------- 小工具

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    private fun formatTime(ms: Long): String {
        if (ms <= 0) return "00:00.000"
        val totalSec = ms / 1000
        return String.format("%02d:%02d.%03d", totalSec / 60, totalSec % 60, ms % 1000)
    }
}
