package app.fortrakz

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object Share {
    fun zipDir(dir: File, out: File) {
        out.parentFile?.mkdirs()
        ZipOutputStream(out.outputStream().buffered()).use { z ->
            dir.walkTopDown().filter { it.isFile }.forEach { f ->
                z.putNextEntry(ZipEntry(f.relativeTo(dir.parentFile ?: dir).path))
                f.inputStream().use { it.copyTo(z) }
                z.closeEntry()
            }
        }
    }

    fun shareFile(ctx: Context, file: File, mime: String, title: String) {
        val uri = FileProvider.getUriForFile(ctx, "${ctx.packageName}.files", file)
        val i = Intent(Intent.ACTION_SEND).apply {
            type = mime
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        ctx.startActivity(Intent.createChooser(i, title).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }
}
