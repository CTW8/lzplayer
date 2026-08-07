package com.example.lzplayer.console

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 事件流。事件名刻意与 native 的 VEDef.h 常量同名——排查时可以和 logcat 逐条对上，
 * 这是测试台相对普通播放界面最实用的一点。
 */
class EventLog(private val capacity: Int = 200) {

    enum class Level { INFO, OK, WARN, CRIT }

    data class Entry(val timestamp: String, val name: String, val detail: String, val level: Level)

    private val fmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    private val entries = ArrayDeque<Entry>()
    private var listener: (() -> Unit)? = null

    fun setListener(l: (() -> Unit)?) { listener = l }

    val snapshot: List<Entry> get() = synchronized(entries) { entries.toList() }

    fun info(name: String, detail: String) = add(name, detail, Level.INFO)
    fun ok(name: String, detail: String) = add(name, detail, Level.OK)
    fun warn(name: String, detail: String) = add(name, detail, Level.WARN)
    fun crit(name: String, detail: String) = add(name, detail, Level.CRIT)

    fun clear() {
        synchronized(entries) { entries.clear() }
        listener?.invoke()
    }

    private fun add(name: String, detail: String, level: Level) {
        synchronized(entries) {
            entries.addLast(Entry(fmt.format(Date()), name, detail, level))
            // 只留最近的：测试台看的是"刚发生了什么"，不是完整历史
            while (entries.size > capacity) entries.removeFirst()
        }
        listener?.invoke()
    }

    /** 导出成可粘贴的纯文本，方便贴进回归报告 */
    fun export(): String = snapshot.joinToString("\n") {
        "${it.timestamp}  ${it.name.padEnd(18)}${it.detail}"
    }
}
