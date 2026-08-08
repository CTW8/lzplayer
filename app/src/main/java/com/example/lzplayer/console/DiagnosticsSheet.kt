package com.example.lzplayer.console

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Typeface
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import com.example.lzplayer.R
import com.example.lzplayer_core.PlayerStats
import com.google.android.material.bottomsheet.BottomSheetDialog

/**
 * 诊断面板：八项实时读数 + 事件流 + 三个策略开关。
 *
 * 读数由外部按进度回调的节奏推进来（[update]），面板自己不开定时器。
 */
class DiagnosticsSheet(
    context: Context,
    private val eventLog: EventLog,
    private val statsProvider: () -> PlayerStats,
    /// 稳态分位数与启播里程碑走原始 JSON：结构化解析类归 perf-metrics 步骤4，
    /// 面板先直接读 JSON，免得为了一个中间层把这一步也堵住
    private val statsJsonProvider: () -> String,
    private val startupTraceProvider: () -> String,
    forceSoftware: Boolean,
    forceSles: Boolean,
    preferVulkan: Boolean,
    private val onForceSoftware: (Boolean) -> Unit,
    private val onForceSles: (Boolean) -> Unit,
    private val onPreferVulkan: (Boolean) -> Unit
) : BottomSheetDialog(context, R.style.Theme_LZConsole_BottomSheet) {

    /** 读数格子：标题建好就不动，只更新值与颜色 */
    private val readouts = LinkedHashMap<String, TextView>()
    private lateinit var logContainer: LinearLayout
    private lateinit var logScroll: ScrollView
    private var swSoftware: TextView? = null
    private var swSles: TextView? = null
    private var swVulkan: TextView? = null
    /// 六个分页的内容容器，切页只改可见性，不重建视图
    private val pages = LinkedHashMap<String, LinearLayout>()
    private val tabViews = LinkedHashMap<String, TextView>()
    private var currentTab = "概览"
    /// 大字读数
    private val bigReadouts = LinkedHashMap<String, TextView>()
    private val bigJudges = LinkedHashMap<String, TextView>()
    private lateinit var waterfallHost: LinearLayout
    private lateinit var startupLegend: LinearLayout
    private lateinit var steadyHost: LinearLayout
    private lateinit var resourceHost: LinearLayout
    private lateinit var seekHost: LinearLayout
    private var forceSoftwareOn = forceSoftware
    private var forceSlesOn = forceSles
    private var preferVulkanOn = preferVulkan

    /// 面板自己的采样节拍。**只在面板显示期间跑**——设计 §5.4 要求"面板打开
    /// 时才采"，既是为了 CPU 需要两次采样求差(暂停/播完后进度回调已停)，
    /// 也是为了不打开时完全不产生测量开销。
    private val ticker = object : Runnable {
        override fun run() {
            if (!isShowing) return
            refreshActivePage()
            tickHandler.postDelayed(this, 1000)
        }
    }
    private val tickHandler = android.os.Handler(android.os.Looper.getMainLooper())

    init {
        setContentView(buildContent(context))
        update(statsProvider())
        renderLog(context)
        eventLog.setListener { logContainer.post { renderLog(context) } }
        setOnDismissListener {
            eventLog.setListener(null)
            tickHandler.removeCallbacks(ticker)
        }
        setOnShowListener { tickHandler.post(ticker) }
    }

    private fun buildContent(ctx: Context): View {
        val root = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(ctx, 16), dp(ctx, 10), dp(ctx, 16), dp(ctx, 20))
        }
        root.addView(grabber(ctx))

        // 标题行：SMPTE 彩条 + 标题 + 动作
        val header = LinearLayout(ctx).apply { gravity = Gravity.CENTER_VERTICAL }
        header.addView(smpteBar(ctx))
        header.addView(TextView(ctx).apply {
            text = "性能"
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setTextColor(color(ctx, R.color.con_ink))
            setPadding(dp(ctx, 8), 0, 0, 0)
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        header.addView(TextView(ctx).apply {
            text = "导出"
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(color(ctx, R.color.con_accent))
            setPadding(dp(ctx, 8), dp(ctx, 6), 0, dp(ctx, 6))
            setOnClickListener { exportLog(ctx) }
        })
        root.addView(header)

        // 分页 tab
        val tabBar = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(ctx, 8), 0, dp(ctx, 6))
        }
        listOf("概览", "启播", "Seek", "稳态", "资源", "日志").forEach { name ->
            val tv = TextView(ctx).apply {
                text = name
                typeface = Typeface.MONOSPACE
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
                gravity = Gravity.CENTER
                setPadding(0, dp(ctx, 6), 0, dp(ctx, 6))
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                setOnClickListener { selectTab(ctx, name) }
            }
            tabViews[name] = tv
            tabBar.addView(tv)
        }
        root.addView(tabBar)
        root.addView(View(ctx).apply {
            setBackgroundColor(color(ctx, R.color.con_line))
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(ctx, 1))
        })

        listOf("概览", "启播", "Seek", "稳态", "资源", "日志").forEach { name ->
            val page = LinearLayout(ctx).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, dp(ctx, 10), 0, 0)
                visibility = if (name == currentTab) View.VISIBLE else View.GONE
            }
            pages[name] = page
            root.addView(page)
        }

        buildOverviewPage(ctx, pages["概览"]!!)
        buildStartupPage(ctx, pages["启播"]!!)
        buildSeekPage(ctx, pages["Seek"]!!)
        buildSteadyPage(ctx, pages["稳态"]!!)
        buildResourcePage(ctx, pages["资源"]!!)
        buildLogPage(ctx, pages["日志"]!!)
        selectTab(ctx, currentTab)

        return ScrollView(ctx).apply { addView(root) }
    }

    private fun selectTab(ctx: Context, name: String) {
        currentTab = name
        pages.forEach { (k, v) -> v.visibility = if (k == name) View.VISIBLE else View.GONE }
        tabViews.forEach { (k, v) ->
            val on = k == name
            v.setTextColor(color(ctx, if (on) R.color.con_accent else R.color.con_ink_faint))
            v.typeface = if (on) Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
                         else Typeface.MONOSPACE
        }
        // 只有可见页需要刷新，切页时重算一次
        refreshActivePage()
    }

    // ------------------------------------------------------------ 概览页

    private fun buildOverviewPage(ctx: Context, page: LinearLayout) {
        // 四个大字读数：字号比其余大一档，供截图远距离判读
        listOf(
            "startup" to "启播总耗时", "fps" to "帧率 实际/名义",
            "drop" to "丢帧率", "cpu" to "进程 CPU"
        ).chunked(2).forEach { pair ->
            val row = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL }
            pair.forEach { (key, label) -> row.addView(bigCell(ctx, label, key)) }
            page.addView(row)
        }

        val grid = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(ctx, 8), 0, 0)
        }
        val labels = listOf(
            "解码路径" to "decoder", "音频后端" to "audioBackend",
            "音视频偏移" to "avOffset", "丢帧 / 总帧" to "frames",
            "包队列 音 / 视" to "queues", "缓冲水位" to "buffered",
            "源类型" to "source", "播放器状态" to "state"
        )
        labels.chunked(2).forEach { pair ->
            val rowView = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL }
            pair.forEach { (label, key) -> rowView.addView(readoutCell(ctx, label, key)) }
            if (pair.size == 1) rowView.addView(View(ctx), LinearLayout.LayoutParams(0, 1, 1f))
            grid.addView(rowView)
        }
        page.addView(grid)

        page.addView(sectionLabel(ctx, "测试开关 · 下次 prepare 生效"))
        val swSoftwareView = switchRow(ctx, "强制软解（验 fallback）", forceSoftwareOn) {
            forceSoftwareOn = !forceSoftwareOn
            onForceSoftware(forceSoftwareOn)
            refreshSwitch(ctx, swSoftware, forceSoftwareOn)
        }
        page.addView(swSoftwareView.parent as View)
        swSoftware = swSoftwareView

        val swSlesView = switchRow(ctx, "强制 OpenSL ES", forceSlesOn) {
            forceSlesOn = !forceSlesOn
            onForceSles(forceSlesOn)
            refreshSwitch(ctx, swSles, forceSlesOn)
        }
        page.addView(swSlesView.parent as View)
        swSles = swSlesView

        // Vulkan 只作用于软解：硬解由 MediaCodec 直出 Surface，不经过任何
        // 渲染器。开关名里点明这个前提，否则很容易误判成"开了没反应"
        val swVulkanView = switchRow(ctx, "Vulkan 渲染（需同时强制软解）", preferVulkanOn) {
            preferVulkanOn = !preferVulkanOn
            onPreferVulkan(preferVulkanOn)
            refreshSwitch(ctx, swVulkan, preferVulkanOn)
        }
        page.addView(swVulkanView.parent as View)
        swVulkan = swVulkanView
    }

    // ------------------------------------------------------------ 启播页

    private fun buildStartupPage(ctx: Context, page: LinearLayout) {
        waterfallHost = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        page.addView(waterfallHost)
        page.addView(sectionLabel(ctx, "分段明细 · T 编号对应设计文档 §5.2"))
        startupLegend = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        page.addView(startupLegend)
    }

    // ------------------------------------------------------------ Seek 页

    private fun buildSeekPage(ctx: Context, page: LinearLayout) {
        seekHost = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        page.addView(seekHost)
    }

    // ------------------------------------------------------------ 稳态页

    private fun buildSteadyPage(ctx: Context, page: LinearLayout) {
        steadyHost = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        page.addView(steadyHost)
    }

    // ------------------------------------------------------------ 资源页

    private fun buildResourcePage(ctx: Context, page: LinearLayout) {
        resourceHost = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        page.addView(resourceHost)
    }

    // ------------------------------------------------------------ 日志页

    private fun buildLogPage(ctx: Context, page: LinearLayout) {
        page.addView(sectionLabel(ctx, "事件流 · 与 VEDef.h 常量同名"))
        logContainer = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.con_panel_bg)
            setPadding(dp(ctx, 8), dp(ctx, 6), dp(ctx, 8), dp(ctx, 6))
        }
        logScroll = ScrollView(ctx).apply {
            addView(logContainer)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(ctx, 320)
            )
        }
        page.addView(logScroll)
    }

    /** 外部按进度节奏推读数进来 */
    fun update(s: PlayerStats) {
        set("decoder", when (s.decoder) {
            "hardware" -> "MediaCodec 硬解"
            "software" -> "FFmpeg 软解"
            else -> "—"
        }, when (s.decoder) {
            "hardware" -> R.color.con_ok
            "software" -> R.color.con_warn
            else -> R.color.con_ink
        })
        set("audioBackend", s.audioBackend,
            if (s.audioBackend == "AAudio") R.color.con_ok else R.color.con_ink)
        // 同步窗口是 40ms，超出就该注意了
        set("avOffset", "${if (s.avOffsetMs >= 0) "+" else ""}${s.avOffsetMs} ms",
            if (kotlin.math.abs(s.avOffsetMs) <= 40) R.color.con_ok else R.color.con_warn)
        set("frames", "${s.droppedFrames} / ${s.renderedFrames}",
            if (s.droppedFrames * 20 > s.renderedFrames + 1) R.color.con_warn else R.color.con_ink)
        set("queues", "${s.audioQueue} / ${s.videoQueue}", R.color.con_ink)
        set("buffered", "${s.bufferedMs} ms",
            if (s.bufferedMs < 300 && s.isNetworkSource) R.color.con_warn else R.color.con_ink)
        set("source", when (s.source) {
            "local" -> "本地文件"
            "network" -> "HTTP 渐进式"
            else -> "—"
        }, R.color.con_ink)
        set("state", s.state, if (s.state == "ERROR") R.color.con_crit else R.color.con_ink)
        refreshActivePage()
    }

    /** 只刷当前可见页：六页全刷会把主线程拖卡，而且看不见的页刷了没意义 */
    private fun refreshActivePage() {
        val ctx = context ?: return
        when (currentTab) {
            "概览" -> refreshOverviewBig(ctx)
            "启播" -> refreshStartup(ctx)
            "稳态" -> refreshSteady(ctx)
            "资源" -> refreshResource(ctx)
            "Seek" -> refreshSeek(ctx)
        }
    }

    // ---------------------------------------------------------- 概览大字读数

    private fun refreshOverviewBig(ctx: Context) {
        val t = parseJson(startupTraceProvider())
        val total = t?.optDouble("startupTotalMs", -1.0) ?: -1.0
        if (total >= 0) {
            // 判定线来自设计文档 C1：< 500 良 / < 1000 可
            val (c, j) = when {
                total < 500 -> R.color.con_ok to "良"
                total < 1000 -> R.color.con_warn to "可"
                else -> R.color.con_crit to "偏慢"
            }
            setBig("startup", "%.0f".format(total), "ms", c, "$j · 线 500/1000")
        } else {
            setBig("startup", "--", "", R.color.con_ink_faint, "未起播")
        }

        val s = statsProvider()
        val stats = parseJson(statsJsonProvider())
        // 实际帧率：用总渲染帧数与播放位置估算。滑动窗口帧率归步骤4，
        // 这里先给一个不会误导的平均值并标明口径
        val posSec = s.positionMs / 1000.0
        val fps = if (posSec > 1.0) s.renderedFrames / posSec else -1.0
        if (fps >= 0) {
            setBig("fps", "%.1f".format(fps), "fps", R.color.con_ok, "均值口径")
        } else {
            setBig("fps", "--", "", R.color.con_ink_faint, "样本不足")
        }

        val totalFrames = s.renderedFrames + s.droppedFrames
        if (totalFrames > 0) {
            val rate = s.droppedFrames * 100.0 / totalFrames
            setBig("drop", "%.2f".format(rate), "%",
                if (rate < 1.0) R.color.con_ok else R.color.con_warn,
                "${s.droppedFrames} / $totalFrames")
        } else {
            setBig("drop", "--", "", R.color.con_ink_faint, "无帧")
        }

        val cpu = sampleProcessCpu()
        if (cpu >= 0) {
            setBig("cpu", "%.1f".format(cpu), "%",
                if (cpu < 15) R.color.con_ok else R.color.con_warn, "单核归一")
        } else {
            setBig("cpu", "--", "", R.color.con_ink_faint, "首次采样")
        }
    }

    // ---------------------------------------------------------- 启播瀑布图

    private fun refreshStartup(ctx: Context) {
        waterfallHost.removeAllViews()
        startupLegend.removeAllViews()
        val t = parseJson(startupTraceProvider())
        if (t == null || !t.optBoolean("valid", false)) {
            waterfallHost.addView(hintText(ctx, "还没有完整的启播记录。打开一个片源并起播后再看这一页。"))
            return
        }

        val segs = listOf(
            Triple("排队等待", "queueWaitMs", R.color.con_ink_faint),
            Triple("打开容器", "containerOpenMs", R.color.con_track_video),
            Triple("解析流信息", "streamInfoMs", R.color.con_accent),
            Triple("建立轨道表", "trackListMs", R.color.con_ink_dim),
            Triple("建链(接线)", "buildChainMs", R.color.con_warn),
            Triple("首帧解码", "firstFrameDecodeMs", R.color.con_ok),
            Triple("首帧上屏", "firstFramePresentMs", R.color.con_track_subtitle)
        ).map { (label, key, c) -> Triple(label, t.optDouble(key, -1.0), c) }

        val present = segs.filter { it.second >= 0 }
        val sum = present.sumOf { it.second }
        val total = t.optDouble("startupTotalMs", -1.0)
        val maxSeg = present.maxByOrNull { it.second }

        // 总耗时 + 来源
        waterfallHost.addView(TextView(ctx).apply {
            val basis = when (t.optString("totalBasis")) {
                "audio" -> "音频基准(无视频轨)"
                "video" -> "视频基准"
                else -> "—"
            }
            text = "启播总耗时 %.1f ms   ·   %s   ·   %s".format(
                total, t.optString("decodePath", "-"), basis)
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            setTextColor(color(ctx, R.color.con_ink))
            setPadding(0, 0, 0, dp(ctx, 8))
        })

        // 堆叠条：按耗时占比分配宽度
        if (sum > 0) {
            val bar = LinearLayout(ctx).apply {
                orientation = LinearLayout.HORIZONTAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(ctx, 26))
            }
            present.forEach { (_, ms, colorRes) ->
                bar.addView(View(ctx).apply {
                    setBackgroundColor(color(ctx, colorRes))
                    layoutParams = LinearLayout.LayoutParams(0,
                        LinearLayout.LayoutParams.MATCH_PARENT, (ms / sum).toFloat())
                })
            }
            waterfallHost.addView(bar)
        }

        // 明细：最大项标 ← 最大项，省去人工比对
        present.forEach { (label, ms, colorRes) ->
            val isMax = maxSeg != null && label == maxSeg.first
            startupLegend.addView(legendRow(ctx, label, "%.1f ms".format(ms), colorRes,
                if (isMax) "← 最大项" else ""))
        }

        // configure 是**跨段的叠加区间**，不参与上面的分段求和：
        // 解码器 prepare 是异步的，它落在 start 之后的首帧窗口里
        val cfg = t.optDouble("decoderConfigureMs", -1.0)
        if (cfg >= 0) {
            val overlap = t.optBoolean("configureOverlapsFirstFrame", false)
            startupLegend.addView(legendRow(ctx, "解码器 configure",
                "%.1f ms".format(cfg), R.color.con_crit,
                if (overlap) "叠加在首帧窗口内" else "在建链内"))
        }

        val gap = t.optDouble("startGapMs", -1.0)
        if (gap >= 0) {
            startupLegend.addView(legendRow(ctx, "就绪→起播",
                "%.1f ms".format(gap), R.color.con_ink_faint, "不计入总耗时"))
        }
        val sound = t.optDouble("firstSoundMs", -1.0)
        if (sound >= 0) {
            startupLegend.addView(legendRow(ctx, "首声耗时",
                "%.1f ms".format(sound), R.color.con_track_audio, "T5→T8"))
        }
        // 自洽性：分段之和应等于总耗时，差值大说明有未计入的空隙
        startupLegend.addView(hintText(ctx,
            "分段之和 %.1f ms，与总耗时差 %.2f ms（应 < 5ms，否则采集点有漏）"
                .format(sum, kotlin.math.abs(sum - total))))
    }

    // ---------------------------------------------------------- 稳态页

    private fun refreshSteady(ctx: Context) {
        steadyHost.removeAllViews()
        val d = parseJson(statsJsonProvider())
        if (d == null) {
            steadyHost.addView(hintText(ctx, "读数不可用。"))
            return
        }
        steadyHost.addView(sectionLabel(ctx, "耗时分布 ms · 样本 < 30 显示 --"))
        steadyHost.addView(percentileHeader(ctx))
        listOf(
            "videoDecodeMs" to "视频解码(软解)",
            "codecLatencyMs" to "codec 延迟(硬解)",
            "audioDecodeMs" to "音频解码",
            "presentMs" to "上屏",
            "syncMarginMs" to "同步余量"
        ).forEach { (key, label) ->
            val h = d.optJSONObject(key)
            steadyHost.addView(percentileRow(ctx, label, h))
        }
        steadyHost.addView(hintText(ctx,
            "codec 延迟含背压等待，不是解码 CPU 成本，不能和视频解码那行比。" +
            "看它是否持续增长即可：增长=产能不足。"))
        steadyHost.addView(hintText(ctx,
            "同步余量是丢帧前兆：正=帧提前就绪。丢帧还是 0 但 p95 逼近 0 时，" +
            "再多一点负载就会开始掉帧。"))

        steadyHost.addView(sectionLabel(ctx, "队列水位 当前 / 峰值"))
        steadyHost.addView(legendRow(ctx, "音频包",
            "${d.optInt("audioQueue")} / ${d.optInt("audioQueuePeak")}",
            R.color.con_track_audio, ""))
        steadyHost.addView(legendRow(ctx, "视频包",
            "${d.optInt("videoQueue")} / ${d.optInt("videoQueuePeak")}",
            R.color.con_track_video, ""))
        steadyHost.addView(legendRow(ctx, "帧队列",
            "— / ${d.optInt("frameQueuePeak")}",
            R.color.con_ink_dim, "硬解为 0 属正常(无独立显示端)"))
        steadyHost.addView(legendRow(ctx, "缓冲水位",
            "${d.optLong("bufferedMs")} ms", R.color.con_accent, ""))
    }

    // ---------------------------------------------------------- 资源页

    private fun refreshResource(ctx: Context) {
        resourceHost.removeAllViews()
        val cpu = sampleProcessCpu()
        val cores = Runtime.getRuntime().availableProcessors()
        resourceHost.addView(sectionLabel(ctx, "进程"))
        resourceHost.addView(legendRow(ctx, "CPU 单核归一",
            if (cpu >= 0) "%.1f %%".format(cpu) else "--", R.color.con_accent, ""))
        // 两个口径都给：只给一个必然有人读错
        resourceHost.addView(legendRow(ctx, "CPU 整机占比",
            if (cpu >= 0) "%.2f %%".format(cpu / cores) else "--",
            R.color.con_ink_dim, "$cores 核"))
        resourceHost.addView(legendRow(ctx, "RSS",
            "${readRssKb() / 1024} MB", R.color.con_ink_dim, ""))
        resourceHost.addView(legendRow(ctx, "Native heap",
            "${android.os.Debug.getNativeHeapAllocatedSize() / 1048576} MB",
            R.color.con_ink_dim, ""))

        resourceHost.addView(sectionLabel(ctx, "线程 CPU · 名字同 ALooper::setName()"))
        val threads = sampleThreadCpu()
        if (threads.isEmpty()) {
            resourceHost.addView(hintText(ctx, "首次采样，再切回来即可看到线程占用。"))
            return
        }
        threads.sortedByDescending { it.second }.take(10).forEach { (name, pct) ->
            resourceHost.addView(legendRow(ctx, name, "%.1f %%".format(pct),
                R.color.con_accent, ""))
        }
    }

    // ---------------------------------------------------------- Seek 页

    private fun refreshSeek(ctx: Context) {
        seekHost.removeAllViews()
        // seek 追踪是 perf-metrics 步骤3，native 侧 getSeekTraceJson 还没接通。
        // 明写出来而不是留个空白页——空白页会被当成"功能坏了"
        seekHost.addView(hintText(ctx,
            "seek 三阶段耗时与精度尚未接通（perf-metrics 步骤3）。\n" +
            "接通后这里会列出最近 10 次 seek 的：暂停 / 定位 / 预热 三段耗时、" +
            "seek 后首帧耗时，以及精度（首帧实际 pts − 请求位置，带符号）。"))
    }

    // ---------------------------------------------------------- CPU 采样

    private var lastProcJiffies = -1L
    private var lastProcWallMs = 0L
    private val lastThreadJiffies = HashMap<String, Long>()
    private var lastThreadWallMs = 0L

    /**
     * 进程 CPU 占用，单核归一。
     *
     * 只在面板打开(以及跑分)期间采样——不打开不采，避免测量本身影响被测对象。
     * 首次调用只记基线，返回 -1。
     */
    private fun sampleProcessCpu(): Double {
        val jiffies = readSelfJiffies() ?: return -1.0
        val now = android.os.SystemClock.elapsedRealtime()
        val prev = lastProcJiffies
        val prevWall = lastProcWallMs
        lastProcJiffies = jiffies
        lastProcWallMs = now
        if (prev < 0 || now - prevWall < 200) return -1.0
        // Android 上 USER_HZ 固定 100，一个 jiffy = 10ms
        val cpuMs = (jiffies - prev) * 10.0
        return cpuMs * 100.0 / (now - prevWall)
    }

    private fun readSelfJiffies(): Long? = runCatching {
        val f = java.io.File("/proc/self/stat").readText()
        // utime 与 stime 是第 14、15 个字段；comm 可能含空格，从末尾的 ')' 起算
        val fields = f.substring(f.lastIndexOf(')') + 2).split(" ")
        fields[11].toLong() + fields[12].toLong()
    }.getOrNull()

    private fun readRssKb(): Long = runCatching {
        val parts = java.io.File("/proc/self/statm").readText().trim().split(" ")
        parts[1].toLong() * 4   // 页数 × 4KB
    }.getOrElse { 0L }

    private fun sampleThreadCpu(): List<Pair<String, Double>> {
        val now = android.os.SystemClock.elapsedRealtime()
        val prevWall = lastThreadWallMs
        val out = ArrayList<Pair<String, Double>>()
        val dirs = java.io.File("/proc/self/task").listFiles() ?: return out
        val fresh = HashMap<String, Long>()
        dirs.forEach { d ->
            runCatching {
                val stat = java.io.File(d, "stat").readText()
                val name = stat.substring(stat.indexOf('(') + 1, stat.lastIndexOf(')'))
                val fields = stat.substring(stat.lastIndexOf(')') + 2).split(" ")
                val j = fields[11].toLong() + fields[12].toLong()
                fresh[name] = j
                val prev = lastThreadJiffies[name]
                if (prev != null && prevWall > 0 && now - prevWall >= 200) {
                    val pct = (j - prev) * 10.0 * 100.0 / (now - prevWall)
                    if (pct > 0.05) out.add(name to pct)
                }
            }
        }
        lastThreadJiffies.clear()
        lastThreadJiffies.putAll(fresh)
        lastThreadWallMs = now
        return out
    }

    // ---------------------------------------------------------- 构件

    private fun parseJson(raw: String): org.json.JSONObject? = runCatching {
        org.json.JSONObject(raw)
    }.getOrNull()

    private fun bigCell(ctx: Context, label: String, key: String): View {
        val cell = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.con_panel_bg)
            setPadding(dp(ctx, 10), dp(ctx, 8), dp(ctx, 10), dp(ctx, 8))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                .also { it.setMargins(0, 0, dp(ctx, 4), dp(ctx, 4)) }
        }
        cell.addView(TextView(ctx).apply {
            text = label
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
            letterSpacing = 0.08f
            setTextColor(color(ctx, R.color.con_ink_faint))
        })
        val v = TextView(ctx).apply {
            text = "--"
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 22f)
            setTextColor(color(ctx, R.color.con_ink))
        }
        cell.addView(v)
        bigReadouts[key] = v
        // 判定文字冗余：灰度截图与色觉障碍下同样可读
        val j = TextView(ctx).apply {
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
            setTextColor(color(ctx, R.color.con_ink_faint))
        }
        cell.addView(j)
        bigJudges[key] = j
        return cell
    }

    private fun setBig(key: String, value: String, unit: String, colorRes: Int, judge: String) {
        bigReadouts[key]?.apply {
            text = if (unit.isEmpty()) value else "$value $unit"
            setTextColor(color(context, colorRes))
        }
        bigJudges[key]?.text = judge
    }

    private fun legendRow(ctx: Context, name: String, value: String,
                          colorRes: Int, hint: String): View {
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(ctx, 4), 0, dp(ctx, 4))
        }
        row.addView(View(ctx).apply {
            setBackgroundColor(color(ctx, colorRes))
            layoutParams = LinearLayout.LayoutParams(dp(ctx, 8), dp(ctx, 8))
                .also { it.marginEnd = dp(ctx, 8) }
        })
        row.addView(TextView(ctx).apply {
            text = name
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(color(ctx, R.color.con_ink))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        row.addView(TextView(ctx).apply {
            text = value
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(color(ctx, R.color.con_ink))
        })
        if (hint.isNotEmpty()) {
            row.addView(TextView(ctx).apply {
                text = hint
                typeface = Typeface.MONOSPACE
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
                setTextColor(color(ctx, R.color.con_warn))
                setPadding(dp(ctx, 6), 0, 0, 0)
            })
        }
        return row
    }

    private fun percentileHeader(ctx: Context): View {
        val row = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL }
        listOf("" to 2f, "p50" to 1f, "p95" to 1f, "max" to 1f, "n" to 1f)
            .forEach { (t, w) ->
                row.addView(TextView(ctx).apply {
                    text = t
                    typeface = Typeface.MONOSPACE
                    setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
                    gravity = if (w == 2f) Gravity.START else Gravity.END
                    setTextColor(color(ctx, R.color.con_ink_faint))
                    layoutParams = LinearLayout.LayoutParams(0,
                        LinearLayout.LayoutParams.WRAP_CONTENT, w)
                })
            }
        return row
    }

    private fun percentileRow(ctx: Context, label: String,
                              h: org.json.JSONObject?): View {
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(ctx, 3), 0, dp(ctx, 3))
        }
        val n = h?.optLong("n", 0) ?: 0
        val enough = n >= 30
        fun cell(text: String, w: Float, bold: Boolean = false) {
            row.addView(TextView(ctx).apply {
                this.text = text
                typeface = if (bold) Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
                           else Typeface.MONOSPACE
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                gravity = if (w == 2f) Gravity.START else Gravity.END
                setTextColor(color(ctx,
                    if (enough) R.color.con_ink else R.color.con_ink_faint))
                layoutParams = LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, w)
            })
        }
        cell(label, 2f)
        // 样本不足一律 --：三五个样本算出的 p95 会被当成结论
        cell(if (enough) "%.2f".format(h!!.optDouble("p50")) else "--", 1f, true)
        cell(if (enough) "%.2f".format(h!!.optDouble("p95")) else "--", 1f, true)
        cell(if (enough) "%.2f".format(h!!.optDouble("max")) else "--", 1f)
        cell("$n", 1f)
        return row
    }

    private fun hintText(ctx: Context, text: String) = TextView(ctx).apply {
        this.text = text
        typeface = Typeface.MONOSPACE
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
        setTextColor(color(ctx, R.color.con_ink_dim))
        setPadding(0, dp(ctx, 6), 0, dp(ctx, 2))
    }

    private fun smpteBar(ctx: Context) = LinearLayout(ctx).apply {
        orientation = LinearLayout.HORIZONTAL
        layoutParams = LinearLayout.LayoutParams(dp(ctx, 34), dp(ctx, 12))
        listOf(
            R.color.con_ink, R.color.con_warn, R.color.con_accent, R.color.con_ok,
            R.color.con_track_subtitle, R.color.con_crit, R.color.con_track_video
        ).forEach { c ->
            addView(View(ctx).apply {
                setBackgroundColor(color(ctx, c))
                layoutParams = LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.MATCH_PARENT, 1f)
            })
        }
    }

    private fun set(key: String, value: String, colorRes: Int) {
        readouts[key]?.apply {
            text = value
            setTextColor(color(context, colorRes))
        }
    }

    private fun renderLog(ctx: Context) {
        if (!::logContainer.isInitialized) {
            return
        }
        logContainer.removeAllViews()
        eventLog.snapshot.forEach { e ->
            val row = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL }
            row.addView(TextView(ctx).apply {
                text = e.timestamp
                typeface = Typeface.MONOSPACE
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
                setTextColor(color(ctx, R.color.con_ink_faint))
            })
            row.addView(TextView(ctx).apply {
                text = e.name
                typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
                setTextColor(color(ctx, when (e.level) {
                    EventLog.Level.OK -> R.color.con_ok
                    EventLog.Level.WARN -> R.color.con_warn
                    EventLog.Level.CRIT -> R.color.con_crit
                    else -> R.color.con_accent
                }))
                layoutParams = LinearLayout.LayoutParams(dp(ctx, 132), LinearLayout.LayoutParams.WRAP_CONTENT)
                    .also { it.marginStart = dp(ctx, 8) }
            })
            row.addView(TextView(ctx).apply {
                text = e.detail
                typeface = Typeface.MONOSPACE
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
                setTextColor(color(ctx, R.color.con_ink_dim))
            })
            logContainer.addView(row)
        }
        logScroll.post { logScroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun exportLog(ctx: Context) {
        val text = eventLog.export()
        if (text.isEmpty()) {
            Toast.makeText(ctx, "事件流是空的", Toast.LENGTH_SHORT).show()
            return
        }
        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("LZPlayer 事件流", text))
        Toast.makeText(ctx, "已复制 ${eventLog.snapshot.size} 条，可直接贴进回归报告",
            Toast.LENGTH_SHORT).show()
    }

    // ---------------------------------------------------------------- 构件

    private fun readoutCell(ctx: Context, label: String, key: String): View {
        val cell = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.con_panel_bg)
            setPadding(dp(ctx, 8), dp(ctx, 6), dp(ctx, 8), dp(ctx, 6))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                .also { it.setMargins(0, 0, dp(ctx, 4), dp(ctx, 4)) }
        }
        cell.addView(TextView(ctx).apply {
            text = label
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)
            letterSpacing = 0.08f
            setTextColor(color(ctx, R.color.con_ink_faint))
        })
        val value = TextView(ctx).apply {
            text = "—"
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            setTextColor(color(ctx, R.color.con_ink))
        }
        cell.addView(value)
        readouts[key] = value
        return cell
    }

    private fun sectionLabel(ctx: Context, text: String) = TextView(ctx).apply {
        this.text = text
        typeface = Typeface.MONOSPACE
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
        letterSpacing = 0.1f
        setTextColor(color(ctx, R.color.con_ink_faint))
        setPadding(0, dp(ctx, 12), 0, dp(ctx, 5))
    }

    /** 返回开关指示器本身，父容器整行可点 */
    private fun switchRow(ctx: Context, label: String, on: Boolean, onToggle: () -> Unit): TextView {
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundResource(R.drawable.con_panel_bg)
            setPadding(dp(ctx, 8), dp(ctx, 7), dp(ctx, 8), dp(ctx, 7))
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.bottomMargin = dp(ctx, 5) }
            setOnClickListener { onToggle() }
        }
        row.addView(TextView(ctx).apply {
            text = label
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(color(ctx, R.color.con_ink))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        // 用文字而不是纯色块表示开关状态：灰度截图下同样可读
        val indicator = TextView(ctx).apply {
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        }
        row.addView(indicator)
        refreshSwitch(ctx, indicator, on)
        return indicator
    }

    private fun refreshSwitch(ctx: Context, indicator: TextView?, on: Boolean) {
        indicator ?: return
        indicator.text = if (on) "开" else "关"
        indicator.setTextColor(color(ctx, if (on) R.color.con_warn else R.color.con_ink_faint))
    }

    private fun grabber(ctx: Context) = View(ctx).apply {
        setBackgroundColor(color(ctx, R.color.con_line))
        layoutParams = LinearLayout.LayoutParams(dp(ctx, 32), dp(ctx, 3)).also {
            it.gravity = Gravity.CENTER_HORIZONTAL
            it.bottomMargin = dp(ctx, 8)
        }
    }

    private fun dp(ctx: Context, v: Int) = (v * ctx.resources.displayMetrics.density).toInt()
    private fun color(ctx: Context, res: Int) = ContextCompat.getColor(ctx, res)
}
