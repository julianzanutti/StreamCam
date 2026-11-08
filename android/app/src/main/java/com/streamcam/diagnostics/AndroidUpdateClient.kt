package com.streamcam.diagnostics

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import okhttp3.Call
import okhttp3.Callback
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import org.json.JSONArray
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.TimeUnit

/** User initiated GitHub release checks and checksum-verified APK downloads. */
object AndroidUpdateClient {
    data class Release(val tag: String, val version: List<Int>, val preview: Boolean, val apkUrl: String, val sha256: String)

    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .callTimeout(2, TimeUnit.MINUTES)
        .build()

    fun check(currentVersion: String, callback: (Result<Release?>) -> Unit) {
        val request = Request.Builder()
            .url("https://api.github.com/repos/julianzanutti/StreamCam/releases?per_page=30")
            .header("Accept", "application/vnd.github+json")
            .header("X-GitHub-Api-Version", "2022-11-28")
            .build()
        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) = callback(Result.failure(e))
            override fun onResponse(call: Call, response: Response) {
                response.use {
                    if (!it.isSuccessful) {
                        callback(Result.failure(IOException("GitHub respondió HTTP ${it.code}")))
                        return
                    }
                    runCatching {
                        val releases = JSONArray(it.body?.string() ?: error("Respuesta vacía"))
                        (0 until releases.length()).mapNotNull { index -> parseRelease(releases.getJSONObject(index)) }
                            .maxWithOrNull(compareBy<Release>({ it.version.joinToString(".").let(::numericVersionKey) }, { !it.preview }))
                    }.fold(
                        onSuccess = { candidate -> callback(Result.success(candidate?.takeIf { isNewer(it, currentVersion) })) },
                        onFailure = { error -> callback(Result.failure(error)) }
                    )
                }
            }
        })
    }

    fun download(context: Context, release: Release, callback: (Result<File>) -> Unit) {
        val request = Request.Builder().url(release.apkUrl).header("Accept", "application/octet-stream").build()
        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) = callback(Result.failure(e))
            override fun onResponse(call: Call, response: Response) {
                response.use { result ->
                    runCatching {
                        if (!result.isSuccessful) error("No se pudo descargar el APK (HTTP ${result.code})")
                        val body = result.body ?: error("Descarga vacía")
                        if (body.contentLength() > MAX_APK_BYTES) error("El APK supera el tamaño permitido")
                        val folder = File(context.cacheDir, "updates").apply { mkdirs() }
                        val temporary = File(folder, "streamcam-update.apk.part")
                        val destination = File(folder, "streamcam-update.apk")
                        val digest = MessageDigest.getInstance("SHA-256")
                        body.byteStream().use { input ->
                            temporary.outputStream().buffered().use { output ->
                                val buffer = ByteArray(64 * 1024)
                                var total = 0L
                                while (true) {
                                    val count = input.read(buffer)
                                    if (count < 0) break
                                    total += count
                                    if (total > MAX_APK_BYTES) error("El APK supera el tamaño permitido")
                                    digest.update(buffer, 0, count)
                                    output.write(buffer, 0, count)
                                }
                            }
                        }
                        val actualHash = digest.digest().joinToString("") { "%02x".format(it) }
                        if (!actualHash.equals(release.sha256, ignoreCase = true)) error("La verificación SHA-256 del APK falló")
                        val archive = if (Build.VERSION.SDK_INT >= 33)
                            context.packageManager.getPackageArchiveInfo(temporary.absolutePath, PackageManager.PackageInfoFlags.of(0))
                        else @Suppress("DEPRECATION") context.packageManager.getPackageArchiveInfo(temporary.absolutePath, 0)
                        if (archive?.packageName != context.packageName) error("El APK no pertenece a StreamCam")
                        val archiveVersion = if (Build.VERSION.SDK_INT >= 28) archive.longVersionCode else @Suppress("DEPRECATION") archive.versionCode.toLong()
                        if (archiveVersion <= appVersionCode(context)) error("El APK no es una actualización más reciente")
                        if (destination.exists() && !destination.delete()) error("No se pudo reemplazar el APK temporal")
                        if (!temporary.renameTo(destination)) error("No se pudo guardar el APK descargado")
                        destination
                    }.fold(
                        onSuccess = { callback(Result.success(it)) },
                        onFailure = { error -> callback(Result.failure(error)) }
                    )
                }
            }
        })
    }

    private fun parseRelease(release: org.json.JSONObject): Release? {
        if (release.optBoolean("draft", true)) return null
        val tag = release.optString("tag_name")
        val match = Regex("^v?(\\d+)\\.(\\d+)\\.(\\d+)(-preview)?$").matchEntire(tag) ?: return null
        val assets = release.optJSONArray("assets") ?: return null
        val apk = (0 until assets.length()).map { assets.getJSONObject(it) }
            .firstOrNull { it.optString("name").endsWith("-android-preview.apk") } ?: return null
        val url = apk.optString("browser_download_url")
        if (!url.startsWith("https://github.com/julianzanutti/StreamCam/releases/download/$tag/") || url.contains('?')) return null
        val digest = apk.optString("digest").removePrefix("sha256:")
        if (!digest.matches(Regex("[0-9a-fA-F]{64}"))) return null
        return Release(tag, match.groupValues.slice(1..3).map(String::toInt), match.groupValues[4].isNotEmpty(), url, digest)
    }

    private fun isNewer(release: Release, current: String): Boolean {
        val currentParts = Regex("^(\\d+)\\.(\\d+)\\.(\\d+)(?:-.*)?$").matchEntire(current)?.groupValues?.drop(1)?.take(3)?.map(String::toInt)
            ?: return true
        for (index in 0..2) {
            if (release.version[index] != currentParts[index]) return release.version[index] > currentParts[index]
        }
        return current.contains("-preview") && !release.preview
    }

    private fun numericVersionKey(value: String): Long = value.split('.').fold(0L) { total, part -> total * 100_000L + part.toLongOrNull().orZero() }
    private fun Long?.orZero() = this ?: 0L
    private fun appVersionCode(context: Context): Long = if (Build.VERSION.SDK_INT >= 28)
        context.packageManager.getPackageInfo(context.packageName, 0).longVersionCode
    else @Suppress("DEPRECATION") context.packageManager.getPackageInfo(context.packageName, 0).versionCode.toLong()

    private const val MAX_APK_BYTES = 180L * 1024L * 1024L
}
