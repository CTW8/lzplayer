package com.example.lzplayer.console

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.util.TypedValue
import android.view.View
import kotlin.math.max

/**
 * 字幕叠加层。native 只发文本，排版全在这里做。
 *
 * 描边而非阴影：视频底色不可控，纯阴影在亮场景上会糊掉。
 */
class SubtitleOverlayView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyle: Int = 0
) : View(context, attrs, defStyle) {

    private var lines: List<String> = emptyList()

    /** 控件浮出时把字幕抬高，别让进度条压住 */
    var bottomInsetPx: Int = 0
        set(value) { field = value; invalidate() }

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        style = Paint.Style.FILL
    }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.BLACK
        textAlign = Paint.Align.CENTER
        style = Paint.Style.STROKE
        strokeWidth = dp(2.5f)
    }

    init {
        val size = TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_SP, 15f, resources.displayMetrics
        )
        fill.textSize = size
        stroke.textSize = size
    }

    private fun dp(v: Float) =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, resources.displayMetrics)

    fun setText(text: String?) {
        lines = text?.takeIf { it.isNotBlank() }?.split('\n') ?: emptyList()
        invalidate()
    }

    fun clear() = setText(null)

    override fun onDraw(canvas: Canvas) {
        if (lines.isEmpty()) return
        val lineHeight = fill.textSize * 1.3f
        val bottomPad = max(dp(12f), bottomInsetPx.toFloat() + dp(8f))
        var y = height - bottomPad - (lines.size - 1) * lineHeight
        val cx = width / 2f
        for (line in lines) {
            // 先描边后填充，保证白字在任何底色上都读得出来
            canvas.drawText(line, cx, y, stroke)
            canvas.drawText(line, cx, y, fill)
            y += lineHeight
        }
    }
}
