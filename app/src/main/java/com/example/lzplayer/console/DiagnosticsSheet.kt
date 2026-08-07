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
 * 诊断面板：八项实时读数 + 事件流 + 两个策略开关。
 *
 * 读数由外部按进度回调的节奏推进来（[update]），面板自己不开定时器。
 */
class DiagnosticsSheet(
    context: Context,
    private val eventLog: EventLog,
    private val statsProvider: () -> PlayerStats,
    forceSoftware: Boolean,
    forceSles: Boolean,
    private val onForceSoftware: (Boolean) -> Unit,
    private val onForceSles: (Boolean) -> Unit
) : BottomSheetDialog(context, R.style.Theme_LZConsole_BottomSheet) {

    /** 读数格子：标题建好就不动，只更新值与颜色 */
    private val readouts = LinkedHashMap<String, TextView>()
    private lateinit var logContainer: LinearLayout
    private lateinit var logScroll: ScrollView
    private var swSoftware: TextView? = null
    private var swSles: TextView? = null
    private var forceSoftwareOn = forceSoftware
    private var forceSlesOn = forceSles

    init {
        setContentView(buildContent(context))
        update(statsProvider())
        renderLog(context)
        eventLog.setListener { logContainer.post { renderLog(context) } }
        setOnDismissListener { eventLog.setListener(null) }
    }

    private fun buildContent(ctx: Context): View {
        val root = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(ctx, 16), dp(ctx, 10), dp(ctx, 16), dp(ctx, 20))
        }
        root.addView(grabber(ctx))

        // 标题行
        val header = LinearLayout(ctx).apply { gravity = Gravity.CENTER_VERTICAL }
        header.addView(TextView(ctx).apply {
            text = "诊断"
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setTextColor(color(ctx, R.color.con_ink))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        header.addView(TextView(ctx).apply {
            text = "复制事件流"
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(color(ctx, R.color.con_accent))
            setPadding(dp(ctx, 8), dp(ctx, 6), 0, dp(ctx, 6))
            setOnClickListener { exportLog(ctx) }
        })
        root.addView(header)

        // 读数栅格（两列）
        val grid = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(ctx, 10), 0, 0)
        }
        val labels = listOf(
            "解码路径" to "decoder", "音频后端" to "audioBackend",
            "音视频偏移" to "avOffset", "丢帧 / 总帧" to "frames",
            "包队列 音 / 视" to "queues", "缓冲水位" to "buffered",
            "源类型" to "source", "播放器状态" to "state"
        )
        labels.chunked(2).forEach { pair ->
            val rowView = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL }
            pair.forEach { (label, key) ->
                rowView.addView(readoutCell(ctx, label, key))
            }
            if (pair.size == 1) {
                rowView.addView(View(ctx), LinearLayout.LayoutParams(0, 1, 1f))
            }
            grid.addView(rowView)
        }
        root.addView(grid)

        // 策略开关
        root.addView(sectionLabel(ctx, "测试开关 · 下次 prepare 生效"))
        val swSoftwareView = switchRow(ctx, "强制软解（验 fallback）", forceSoftwareOn) {
            forceSoftwareOn = !forceSoftwareOn
            onForceSoftware(forceSoftwareOn)
            refreshSwitch(ctx, swSoftware, forceSoftwareOn)
        }
        root.addView(swSoftwareView.parent as View)
        swSoftware = swSoftwareView

        val swSlesView = switchRow(ctx, "强制 OpenSL ES", forceSlesOn) {
            forceSlesOn = !forceSlesOn
            onForceSles(forceSlesOn)
            refreshSwitch(ctx, swSles, forceSlesOn)
        }
        root.addView(swSlesView.parent as View)
        swSles = swSlesView

        // 事件流
        root.addView(sectionLabel(ctx, "事件流 · 与 logcat 同名"))
        logContainer = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundResource(R.drawable.con_panel_bg)
            setPadding(dp(ctx, 8), dp(ctx, 6), dp(ctx, 8), dp(ctx, 6))
        }
        logScroll = ScrollView(ctx).apply {
            addView(logContainer)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(ctx, 190)
            )
        }
        root.addView(logScroll)

        return ScrollView(ctx).apply { addView(root) }
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
    }

    private fun set(key: String, value: String, colorRes: Int) {
        readouts[key]?.apply {
            text = value
            setTextColor(color(context, colorRes))
        }
    }

    private fun renderLog(ctx: Context) {
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
