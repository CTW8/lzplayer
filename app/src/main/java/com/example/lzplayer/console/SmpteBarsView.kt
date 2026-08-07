package com.example.lzplayer.console

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import androidx.core.content.ContextCompat
import com.example.lzplayer.R

/**
 * SMPTE 彩条。测试台里贯穿各处的结构分隔——它是这个领域自己的"测试信号"符号，
 * 用来划分区块而不是装点门面。
 */
class SmpteBarsView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyle: Int = 0
) : View(context, attrs, defStyle) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bars = intArrayOf(
        R.color.smpte_grey, R.color.smpte_yellow, R.color.smpte_cyan, R.color.smpte_green,
        R.color.smpte_magenta, R.color.smpte_red, R.color.smpte_blue
    ).map { ContextCompat.getColor(context, it) }

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat() / bars.size
        bars.forEachIndexed { i, color ->
            paint.color = color
            canvas.drawRect(i * w, 0f, (i + 1) * w, height.toFloat(), paint)
        }
    }
}
