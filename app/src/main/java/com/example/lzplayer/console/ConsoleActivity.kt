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

        /// 跑分序列参数(perf-metrics 步骤7)。三个都是可选的, 不带时行为与
        /// 手工操作完全一致 —— 自动化不能改变被测对象的默认行为
        /** --es seekPercents 10,50,90 : 起播稳定后按序 seek 到这些百分比 */
        const val EXTRA_SEEK_PERCENTS = "seekPercents"
        /** --ei playSeconds 15 : 稳态播多久后收尾 */
        const val EXTRA_PLAY_SECONDS = "playSeconds"
        /** --es caseName fallback-runtime : 报告里的用例名 */
        const val EXTRA_CASE_NAME = "caseName"
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
    /// 常驻仪表带：两行八格。选取标准是"异常时会先动的量"——
    /// 分位数/直方图/线程分布这些需要对照阅读的留在面板里。
    /// 主屏放太多等于什么都没突出，它的唯一任务是让人一秒内决定要不要打开面板
    private lateinit var gaugeStrip: LinearLayout
    /// 源栏折叠态：打开后路径就不再需要，却一直占着顶部一整行(输入框+两个按钮)。
    /// 收成一行摘要，省下的高度给仪表带；点它展开回完整输入
    private lateinit var srcCollapsed: LinearLayout
    private lateinit var srcCollapsedName: TextView
    /// 横屏沉浸态的紧凑 HUD。横屏要满画面核对画幅/色彩/旋转，八格仪表带会
    /// 吃掉画面，所以换成三行叠在左下角——但**读数不能跟着控件一起消失**，
    /// 回归时最需要的就是一边看画面一边看数
    private lateinit var landHud: TextView
    /// 主屏 CPU 采样(两次求差)。面板另有自己的节拍，两边互不干扰
    private var lastCpuJiffies = -1L
    private var lastCpuWallMs = 0L
    private val gaugeValues = LinkedHashMap<String, TextView>()
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

        buildGaugeStrip()
        buildCollapsedSourceBar()
        buildLandscapeHud()
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

    /**
     * 解析跑分序列参数。**非法值一律拒绝并记进事件日志，不静默忽略**：
     * 跑分是无人值守跑的，静默忽略会让报告建立在错误前提上——比如把
     * "seekPercents 写错导致一次 seek 都没做"的那次，当成"seek 全部通过"。
     */
    /**
     * 跑分序列收尾：把一次性快照原子落盘。
     *
     * **必须临时文件 + rename。** 直接 writeText 是"先截断再写"，harness 用
     * adb 拉取时极易读到中间态的空文件——步骤5 的每 tick 写文件脚手架就因此
     * 误导过一次结论。rename 在同一文件系统内是原子的，任意时刻读到的要么是
     * 完整文件、要么没有文件。
     *
     * complete 标志区分"正常收尾"与"中途被打断"：没有它，一份被截断的数据
     * 看起来和正常数据一模一样，而跑分报告最不能容忍的就是这个。
     */
    /**
     * playSeconds 到点后收尾。用 postDelayed 而不是等 EOS：跑分要的是**固定
     * 时长**的稳态样本，等 EOS 会让不同片长的素材采样窗口不一致，跨素材数字
     * 就不可比了。素材比 playSeconds 短时 EOS 先到，那次的 complete 仍为 true
     * ——播完了也是一种正常收尾。
     */
    private fun armBenchmarkFinish() {
        if (playSeconds < 0) return
        val token = ++benchGen
        etSource.postDelayed({
            // 代次校验：期间若换源/重开，旧的收尾不许覆盖新一轮的快照
            if (token == benchGen) {
                dumpBenchmarkSnapshot(true)
            }
        }, playSeconds * 1000L)
        Log.w(TAG, "VEBENCH armed finish in ${playSeconds}s")
    }

    private fun dumpBenchmarkSnapshot(complete: Boolean) {
        val p = player ?: return
        val dir = getExternalFilesDir(null) ?: filesDir
        val name = if (caseName.isNotEmpty()) caseName else "bench"
        val target = java.io.File(dir, "$name-snapshot.json")
        val tmp = java.io.File(dir, "$name-snapshot.json.tmp")
        try {
            val json = buildString {
                append("{\"caseName\":\"").append(name).append("\",")
                append("\"complete\":").append(complete).append(',')
                append("\"forceSoftware\":").append(forceSoftware).append(',')
                append("\"preferVulkan\":").append(preferVulkan).append(',')
                append("\"playSeconds\":").append(playSeconds).append(',')
                append("\"seekPercents\":\"").append(seekPercents.joinToString(",")).append("\",")
                append("\"startup\":").append(orNull(p.startupTraceJson)).append(',')
                append("\"stats\":").append(orNull(p.statsJsonRaw)).append(',')
                append("\"seekTrace\":").append(orNull(p.seekTraceJson))
                append('}')
            }
            tmp.writeText(json)
            if (!tmp.renameTo(target)) {
                // rename 失败就不要留下半成品: 宁可没有文件, 也不要一份
                // 无法判断新旧的文件被 harness 当成本次结果
                tmp.delete()
                Log.w(TAG, "VEBENCH REJECT snapshot rename failed")
                return
            }
            Log.w(TAG, "VEBENCH snapshot=${target.absolutePath} complete=$complete")
        } catch (e: Exception) {
            tmp.delete()
            Log.w(TAG, "VEBENCH REJECT snapshot write failed: ${e.message}")
        }
    }

    /** native 侧取不到时补 null 字面量, 不要塞空串——JSON 会解析失败 */
    private fun orNull(s: String?): String =
        if (s.isNullOrBlank()) "null" else s

    private fun benchArg(ok: Boolean, kv: String) {
        // 同时进 app 事件日志与 logcat: 前者给人看, 后者给 harness 解析。
        // 跑分报告是从 logcat 组装的, 只写事件日志的话"参数被拒绝"这件事
        // 不会出现在报告里, 而那正是最需要被看见的一类
        if (ok) eventLog.info("BENCH_ARG", kv) else eventLog.crit("BENCH_ARG", kv)
        Log.w(TAG, "VEBENCH ${if (ok) "ok" else "REJECT"} $kv")
    }

    private fun parseBenchmarkExtras(i: Intent) {
        seekPercents = emptyList()
        if (i.hasExtra(EXTRA_SEEK_PERCENTS)) {
            val raw = i.getStringExtra(EXTRA_SEEK_PERCENTS).orEmpty()
            val parsed = raw.split(',').map { it.trim() }.filter { it.isNotEmpty() }
            val bad = parsed.filter { it.toIntOrNull()?.let { v -> v in 0..100 } != true }
            if (parsed.isEmpty() || bad.isNotEmpty()) {
                benchArg(false, "seekPercents 非法: \"$raw\"" +
                        (if (bad.isNotEmpty()) " 越界或非数字: ${bad.joinToString()}" else " 为空"))
            } else {
                seekPercents = parsed.map { it.toInt() }
                benchArg(true, "seekPercents=${seekPercents.joinToString()}")
            }
        }
        playSeconds = -1
        if (i.hasExtra(EXTRA_PLAY_SECONDS)) {
            // 缺省值取 -1 而不是 0: 0 是"立刻收尾"这个合法值, 与"没给"不同
            val v = i.getIntExtra(EXTRA_PLAY_SECONDS, -1)
            if (v < 0) {
                benchArg(false, "playSeconds 非法: $v（须 >= 0）")
            } else {
                playSeconds = v
                benchArg(true, "playSeconds=$v")
            }
        }
        caseName = i.getStringExtra(EXTRA_CASE_NAME)?.trim().orEmpty()
        if (caseName.isNotEmpty()) {
            benchArg(true, "caseName=$caseName")
        }
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
        parseBenchmarkExtras(i)
        etSource.setText(path)
        eventLog.info("LAUNCH_INTENT", path)
        // 换源即作废在途收尾, 再按新参数重新武装
        benchGen++
        armBenchmarkFinish()
        if (surfaceReady) {
            openSource(path)
        } else {
            // onCreate 阶段 surface 还没创建。此时建链会让硬解工厂因为
            // 拿不到 Surface 而退回软解——自动化跑出来全是软解，回归结论
            // 就废了。等 surfaceChanged 再打开。
            pendingIntentSource = path
        }
    }

    /**
     * 在画面区与控件区之间插入仪表带。
     *
     * 竖屏下 1080p 横向素材只占中间一条，上下两片黑边一直空着——那正是这些
     * 读数该待的地方。每格可点，直接跳到解释它的那一页。
     */
    private fun buildGaugeStrip() {
        val root = findViewById<LinearLayout>(R.id.root)
        val controlsIdx = root.indexOfChild(findViewById(R.id.controls))
        gaugeStrip = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        // 主行：帧率/丢帧/AV/CPU；次行：启播/同步余量/队列/缓冲
        listOf(
            listOf("fps" to "帧率", "drop" to "丢帧 迟到", "av" to "A/V", "cpu" to "CPU"),
            listOf("startup" to "启播", "margin" to "同步余量", "queue" to "队列 音/视", "buffer" to "缓冲")
        ).forEach { row ->
            val rowView = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            row.forEach { (key, label) -> rowView.addView(gaugeCell(key, label)) }
            gaugeStrip.addView(rowView)
        }
        root.addView(gaugeStrip, controlsIdx)
    }

    /** 仪表格子 → 解释它的那一页。八项分散在三个分页里，用户没理由知道这个映射 */
    private fun buildCollapsedSourceBar() {
        val root = findViewById<LinearLayout>(R.id.root)
        val bar = findViewById<View>(R.id.sourceBar)
        srcCollapsed = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dpi(12), dpi(7), dpi(12), dpi(7))
            visibility = View.GONE
            setOnClickListener { setSourceBarCollapsed(false) }
        }
        srcCollapsed.addView(TextView(this).apply {
            text = "▸"
            typeface = android.graphics.Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 10f)
            setTextColor(ContextCompat.getColor(context, R.color.con_ink_faint))
        })
        srcCollapsedName = TextView(this).apply {
            typeface = android.graphics.Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 10f)
            setTextColor(ContextCompat.getColor(context, R.color.con_ink))
            maxLines = 1
            ellipsize = android.text.TextUtils.TruncateAt.MIDDLE
            layoutParams = LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                .also { it.marginStart = dpi(6) }
        }
        srcCollapsed.addView(srcCollapsedName)
        srcCollapsed.addView(TextView(this).apply {
            text = "换源"
            typeface = android.graphics.Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 10f)
            setTextColor(ContextCompat.getColor(context, R.color.con_accent))
        })
        root.addView(srcCollapsed, root.indexOfChild(bar) + 1)
    }

    private fun buildLandscapeHud() {
        landHud = TextView(this).apply {
            typeface = android.graphics.Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 9f)
            setTextColor(ContextCompat.getColor(context, R.color.con_ink))
            setBackgroundColor(0xD00E1217.toInt())
            setPadding(dpi(6), dpi(4), dpi(8), dpi(4))
            visibility = View.GONE
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).also {
                it.gravity = Gravity.BOTTOM or Gravity.START
                it.setMargins(dpi(8), 0, 0, dpi(8))
            }
        }
        stage.addView(landHud)
    }

    /** 单核归一。首次调用只记基线返回 -1——"没采到"与"占用为 0"不是一回事 */
    private fun sampleCpuPercent(): Double {
        val j = runCatching {
            val f = java.io.File("/proc/self/stat").readText()
            val fields = f.substring(f.lastIndexOf(')') + 2).split(" ")
            fields[11].toLong() + fields[12].toLong()
        }.getOrNull() ?: return -1.0
        val now = android.os.SystemClock.elapsedRealtime()
        val prev = lastCpuJiffies
        val prevWall = lastCpuWallMs
        lastCpuJiffies = j
        lastCpuWallMs = now
        if (prev < 0 || now - prevWall < 200) return -1.0
        return (j - prev) * 10.0 * 100.0 / (now - prevWall)   // USER_HZ=100
    }

    private fun setSourceBarCollapsed(collapsed: Boolean) {
        findViewById<View>(R.id.sourceBar).visibility =
            if (collapsed) View.GONE else View.VISIBLE
        srcCollapsed.visibility = if (collapsed) View.VISIBLE else View.GONE
    }

    private fun pageOf(key: String) = when (key) {
        "cpu" -> "资源"
        "startup" -> "启播"
        else -> "稳态"
    }

    private fun gaugeCell(key: String, label: String): View {
        val cell = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.con_panel_bg)
            setPadding(dpi(8), dpi(5), dpi(8), dpi(5))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                .also { it.setMargins(dpi(1), dpi(1), dpi(1), dpi(1)) }
            setOnClickListener { showDiagnosticsSheet(pageOf(key)) }
        }
        cell.addView(TextView(this).apply {
            text = label
            typeface = android.graphics.Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 8f)
            setTextColor(ContextCompat.getColor(context, R.color.con_ink_faint))
        })
        val v = TextView(this).apply {
            text = "--"
            typeface = android.graphics.Typeface.create(
                android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD)
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 14f)
            setTextColor(ContextCompat.getColor(context, R.color.con_ink))
        }
        cell.addView(v)
        gaugeValues[key] = v
        return cell
    }

    private fun refreshGauges() {
        val s = latestStats
        val posSec = s.positionMs / 1000.0
        val fps = if (posSec > 1.0) s.renderedFrames / posSec else -1.0
        setGauge("fps", if (fps >= 0) "%.1f".format(fps) else "--",
            if (fps >= 0) R.color.con_ok else R.color.con_ink_faint)

        val raw = runCatching { org.json.JSONObject(player?.statsJsonRaw ?: "{}") }.getOrNull()
        // 只显示"迟到被丢"这一类。四类混成一个数既会把正常当缺陷，
        // 也会把真丢帧藏起来——stale 与 seekCatchup 属正常
        val late = raw?.optLong("dropLate", 0) ?: 0
        setGauge("drop", "$late", if (late > 0) R.color.con_warn else R.color.con_ok)

        setGauge("av", "${if (s.avOffsetMs >= 0) "+" else ""}${s.avOffsetMs}",
            if (kotlin.math.abs(s.avOffsetMs) <= 40) R.color.con_ok else R.color.con_warn)
        val cpu = sampleCpuPercent()
        setGauge("cpu", if (cpu >= 0) "%.0f".format(cpu) else "--",
            if (cpu < 0) R.color.con_ink_faint
            else if (cpu < 150) R.color.con_ok else R.color.con_warn)

        val t = runCatching { org.json.JSONObject(player?.startupTraceJson ?: "{}") }.getOrNull()
        val total = t?.optDouble("startupTotalMs", -1.0) ?: -1.0
        setGauge("startup", if (total >= 0) "%.0f".format(total) else "--",
            if (total < 0) R.color.con_ink_faint
            else if (total < 500) R.color.con_ok else R.color.con_warn)

        val m = raw?.optJSONObject("syncMarginMs")
        val n = m?.optLong("n", 0) ?: 0
        // 样本不足显示 --，不给一个由三五个样本算出的数字
        setGauge("margin", if (n >= 30) "%.0f".format(m!!.optDouble("p50")) else "--",
            if (n >= 30) R.color.con_ok else R.color.con_ink_faint)
        setGauge("queue", "${s.audioQueue}/${s.videoQueue}", R.color.con_ink)
        setGauge("buffer", "${s.bufferedMs}", R.color.con_ink)

        if (::landHud.isInitialized && landHud.visibility == View.VISIBLE) {
            landHud.text = buildString {
                append(if (fps >= 0) "%.1ffps".format(fps) else "--fps")
                append("  drop ").append(late).append('\n')
                append("A/V ").append(if (s.avOffsetMs >= 0) "+" else "").append(s.avOffsetMs)
                append("ms  buf ").append(s.bufferedMs).append("ms\n")
                append("cpu ").append(if (cpu >= 0) "%.0f%%".format(cpu) else "--")
                append("  first ").append(if (total >= 0) "%.0fms".format(total) else "--")
            }
        }
    }

    private fun dpi(v: Int) = (v * resources.displayMetrics.density).toInt()

    /** 颜色 + 数值双编码：灰度截图下靠数值本身仍可判读 */
    private fun setGauge(key: String, text: String, colorRes: Int) {
        gaugeValues[key]?.apply {
            this.text = text
            setTextColor(ContextCompat.getColor(context, colorRes))
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

    /**
     * 速率收成一个下拉。
     *
     * 六档横排占满一整行，而它是低频操作——设置不该和主操作抢空间。
     * 容器 speedRow 保留(XML 不动)，里面只放一个当前值。
     */
    private fun buildSpeedRow() {
        speedRow.removeAllViews()
        speedRow.addView(TextView(this).apply {
            typeface = android.graphics.Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(ContextCompat.getColor(context, R.color.con_ink))
            setPadding(dpi(6), dpi(6), dpi(6), dpi(6))
            setOnClickListener { showSpeedPicker() }
            speedLabel = this
        })
        refreshSpeedRow()
    }

    private var speedLabel: TextView? = null

    private fun showSpeedPicker() {
        val names = SPEEDS.map { "%.2fx".format(it).replace(".00x", ".0x") }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("播放速率 · 音调不变")
            .setSingleChoiceItems(names, speedIndex) { d, which ->
                d.dismiss()
                applySpeed(which)
            }
            .show()
    }

    private fun refreshSpeedRow() {
        val v = SPEEDS[speedIndex]
        speedLabel?.text = "%.2fx".format(v).replace(".00x", ".0x") + " ▾"
    }

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
        btnPlayPause.text = "▶"
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
        // 打开之后路径不再需要，收起来把高度让给仪表带
        srcCollapsedName.text = source.substringAfterLast('/')
        setSourceBarCollapsed(true)
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
        // 曾经每个进度 tick 往 filesDir/perf/ 覆盖写两个 JSON，作为绕开本机
        // logcat 配额的观测手段。现已删除：性能面板可以按需拉取，而那套脚手架
        // 用的是 writeText(先截断再写)，被 adb 读到中间态就是空文件——排查
        // stop→play 时已经因此误判过一次。留着弊大于利。
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
        btnPlayPause.text = if (playing) "⏸" else "▶"
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
        btnPlayPause.text = "▶"
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

    private fun showDiagnosticsSheet(initialTab: String = "概览") {
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
            },
            initialTab = initialTab
        ).also { it.show() }
    }

    private var forceSoftware = false
    private var forceSles = false
    private var preferVulkan = false
    /** 每次换源只打印一次启播里程碑 */
    private var startupTraceDumped = false
    /** 由 intent 指定的自动起播，PREPARED 之后触发一次 */
    /** 跑分序列: 待执行的 seek 百分比, 空=不做 seek。见 [EXTRA_SEEK_PERCENTS] */
    private var seekPercents: List<Int> = emptyList()
    /** 跑分序列: 稳态播放秒数, -1=没给(不自动收尾) */
    private var playSeconds: Int = -1
    /** 跑分序列: 报告里的用例名, 空=没给 */
    private var caseName: String = ""
    /** 收尾代次：换源/重开时递增，作废在途的延时收尾 */
    private var benchGen: Int = 0
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
        // 横屏让画面占满：仪表带与折叠源栏收起，读数改由左下紧凑 HUD 承担
        if (::gaugeStrip.isInitialized) {
            gaugeStrip.visibility = if (land) View.GONE else View.VISIBLE
        }
        if (::srcCollapsed.isInitialized && land) {
            srcCollapsed.visibility = View.GONE
        }
        if (::landHud.isInitialized) {
            landHud.visibility = if (land) View.VISIBLE else View.GONE
        }
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
                btnPlayPause.text = "▶"
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
            btnPlayPause.text = "▶"
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
            refreshGauges()
            // 起播里程碑在首帧上屏后才齐全，而进度已经在推进说明那一刻早已过去。
            // 挂在这里而不是用 postDelayed：进度回调是确定会来的，延迟消息会被
            // 各种 removeCallbacks 之类的清理波及。
            // 只在首个有效进度时记一次事件流。面板需要时自己拉最新的，
            // 不必在进度回调里反复取
            if (!startupTraceDumped && progressMs > 0) {
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
            btnPlayPause.text = "▶"
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
