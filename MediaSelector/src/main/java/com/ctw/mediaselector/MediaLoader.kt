package com.ctw.mediaselector

import android.content.Context
import android.provider.MediaStore
import android.content.ContentUris
import android.net.Uri

object MediaLoader {
    fun loadMediaFiles(context: Context, mediaType: MediaType): List<MediaFile> {
        val projection = arrayOf(
            MediaStore.MediaColumns._ID,
            MediaStore.MediaColumns.DISPLAY_NAME,
            MediaStore.MediaColumns.DATA,
            MediaStore.MediaColumns.MIME_TYPE,
            MediaStore.MediaColumns.DATE_ADDED
        )

        val (collection, selection) = when (mediaType) {
            MediaType.IMAGE -> Pair<Uri, String>(
                MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                "${MediaStore.Images.Media.MIME_TYPE} LIKE 'image/%'"
            )
            MediaType.VIDEO -> Pair<Uri, String>(
                MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
                "${MediaStore.Video.Media.MIME_TYPE} LIKE 'video/%'"
            )
            MediaType.ALL -> Pair<Uri, String>(
                MediaStore.Files.getContentUri("external"),
                "${MediaStore.Files.FileColumns.MEDIA_TYPE} IN (" +
                    "${MediaStore.Files.FileColumns.MEDIA_TYPE_IMAGE}," +
                    "${MediaStore.Files.FileColumns.MEDIA_TYPE_VIDEO})"
            )
        }

        return context.contentResolver.query(
            collection,
            projection,
            selection,
            null,
            "${MediaStore.MediaColumns.DATE_ADDED} DESC"
        )?.use { cursor ->
            val result = mutableListOf<MediaFile>()
            while (cursor.moveToNext()) {
                result.add(
                    MediaFile(
                        id = cursor.getLong(cursor.getColumnIndexOrThrow(MediaStore.MediaColumns._ID)),
                        name = cursor.getString(cursor.getColumnIndexOrThrow(MediaStore.MediaColumns.DISPLAY_NAME)),
                        path = cursor.getString(cursor.getColumnIndexOrThrow(MediaStore.MediaColumns.DATA)),
                        type = when {
                            cursor.getString(cursor.getColumnIndexOrThrow(MediaStore.MediaColumns.MIME_TYPE))
                                .startsWith("image/") -> MediaType.IMAGE
                            else -> MediaType.VIDEO
                        },
                        dateAdded = cursor.getLong(cursor.getColumnIndexOrThrow(MediaStore.MediaColumns.DATE_ADDED))
                    )
                )
            }
            result
        } ?: emptyList()
    }
} 