package com.example.lzplayer.console

import android.content.Context
import android.graphics.Typeface
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.content.ContextCompat
import com.example.lzplayer.R
import com.example.lzplayer_core.TrackInfo
import com.google.android.material.bottomsheet.BottomSheetDialog

/**
 * 轨道面板。
 *
 * 视频轨只读并置灰——本期不支持切换，灰掉比藏起来诚实。
 * 切轨后在标题右侧显示耗时，直接对照"硬解下 < 200ms"的验收线。
 */
class TrackSheet(
    context: Context,
    private var tracks: Array<TrackInfo>,
    private val focusSubtitle: Boolean,
    private val onSelect: (TrackInfo) -> Unit,
    private val onDeselectSubtitle: () -> Unit,
    private val onLoadExternal: () -> Unit
) : BottomSheetDialog(context, R.style.Theme_LZConsole_BottomSheet) {

    private lateinit var container: LinearLayout
    private lateinit var costLabel: TextView

    init {
        setContentView(buildContent(context))
    }

    private fun buildContent(ctx: Context): View {
        val root = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(ctx, 16), dp(ctx, 10), dp(ctx, 16), dp(ctx, 20))
        }

        root.addView(grabber(ctx))

        val header = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(TextView(ctx).apply {
            text = "轨道"
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setTextColor(color(ctx, R.color.con_ink))
        })
        costLabel = TextView(ctx).apply {
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            setTextColor(color(ctx, R.color.con_ok))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                .also { it.marginStart = dp(ctx, 8) }
        }
        header.addView(costLabel)
        header.addView(TextView(ctx).apply {
            text = "加载外挂字幕"
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setTextColor(color(ctx, R.color.con_accent))
            setPadding(dp(ctx, 8), dp(ctx, 6), 0, dp(ctx, 6))
            setOnClickListener { onLoadExternal(); dismiss() }
        })
        root.addView(header)

        container = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(ctx, 10), 0, 0)
        }
        val scroll = ScrollView(ctx).apply {
            addView(container)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(ctx, 420)
            )
        }
        root.addView(scroll)
        renderTracks(ctx)
        if (focusSubtitle) {
            // 从"字幕"入口进来的：字幕组在最下面，直接滚过去，
            // 省得每次都手动划
            scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
        }
        return root
    }

    fun updateTracks(newTracks: Array<TrackInfo>) {
        tracks = newTracks
        renderTracks(context)
    }

    /** 切轨/seek 完成后把耗时显示在标题旁 */
    fun showSwitchCost(costMs: Long) {
        costLabel.text = "切换完成 ${costMs}ms"
        costLabel.setTextColor(
            color(context, if (costMs <= 200) R.color.con_ok else R.color.con_warn)
        )
    }

    private fun renderTracks(ctx: Context) {
        container.removeAllViews()

        addGroup(ctx, "视频", tracks.filter { it.isVideo }, R.color.con_track_video, enabled = false)
        addGroup(ctx, "音频", tracks.filter { it.isAudio }, R.color.con_track_audio, enabled = true)

        val subs = tracks.filter { it.isSubtitle }
        container.addView(groupLabel(ctx, "字幕"))
        // "关闭"作为一个可选项而不是开关：和轨道选择是同一件事，放同一组里
        val noneActive = subs.none { it.active }
        container.addView(row(ctx, "关闭", "", R.color.con_track_subtitle, noneActive, true) {
            onDeselectSubtitle(); dismiss()
        })
        subs.forEach { t ->
            container.addView(
                row(ctx, trackName(t), t.spec(), R.color.con_track_subtitle, t.active, true) {
                    onSelect(t); dismiss()
                }
            )
        }
        if (subs.isEmpty()) {
            container.addView(hint(ctx, "这个片源没有内嵌字幕轨，可以加载外挂字幕"))
        }
    }

    private fun addGroup(
        ctx: Context, label: String, list: List<TrackInfo>, stripeColor: Int, enabled: Boolean
    ) {
        container.addView(groupLabel(ctx, label))
        if (list.isEmpty()) {
            container.addView(hint(ctx, "无"))
            return
        }
        list.forEach { t ->
            container.addView(
                row(ctx, trackName(t), t.spec(), stripeColor, t.active, enabled) {
                    onSelect(t); dismiss()
                }
            )
        }
        if (!enabled) {
            container.addView(hint(ctx, "视频轨切换本期不支持"))
        }
    }

    private fun trackName(t: TrackInfo): String {
        val base = if (t.index >= 0x10000) "外挂" else "轨 ${t.index}"
        return if (t.language.isNotEmpty()) "$base · ${t.language}"
        else if (t.title.isNotEmpty()) "$base · ${t.title}" else base
    }

    private fun groupLabel(ctx: Context, text: String) = TextView(ctx).apply {
        this.text = text
        typeface = Typeface.MONOSPACE
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
        letterSpacing = 0.1f
        setTextColor(color(ctx, R.color.con_ink_faint))
        setPadding(0, dp(ctx, 12), 0, dp(ctx, 5))
    }

    private fun hint(ctx: Context, text: String) = TextView(ctx).apply {
        this.text = text
        typeface = Typeface.MONOSPACE
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
        setTextColor(color(ctx, R.color.con_ink_faint))
        setPadding(dp(ctx, 4), dp(ctx, 2), 0, dp(ctx, 2))
    }

    private fun row(
        ctx: Context, name: String, spec: String, stripeColor: Int,
        active: Boolean, enabled: Boolean, onClick: () -> Unit
    ): View {
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundResource(R.drawable.con_track_item)
            isActivated = active
            setPadding(dp(ctx, 8), dp(ctx, 8), dp(ctx, 8), dp(ctx, 8))
            alpha = if (enabled) 1f else 0.45f
            if (enabled) setOnClickListener { onClick() }
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.bottomMargin = dp(ctx, 5) }
        }
        // 轨道类型条纹：沿用剪辑软件的颜色约定，扫一眼就知道在看哪类轨
        row.addView(View(ctx).apply {
            setBackgroundColor(color(ctx, stripeColor))
            layoutParams = LinearLayout.LayoutParams(dp(ctx, 3), dp(ctx, 18))
                .also { it.marginEnd = dp(ctx, 8) }
        })
        row.addView(TextView(ctx).apply {
            text = name
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setTextColor(color(ctx, if (active) R.color.con_accent else R.color.con_ink))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        if (spec.isNotEmpty()) {
            row.addView(TextView(ctx).apply {
                text = spec
                typeface = Typeface.MONOSPACE
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
                setTextColor(color(ctx, R.color.con_ink_dim))
            })
        }
        return row
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
