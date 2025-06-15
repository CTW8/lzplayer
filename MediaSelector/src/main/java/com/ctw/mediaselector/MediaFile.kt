package com.ctw.mediaselector

data class MediaFile(
    val id: Long,
    val name: String,
    val path: String,  // 文件路径
    val type: MediaType,
    val dateAdded: Long,
    var isSelected: Boolean = false
) 