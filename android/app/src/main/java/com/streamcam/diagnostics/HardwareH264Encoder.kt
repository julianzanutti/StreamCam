package com.streamcam.diagnostics

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.SystemClock
import android.view.Surface
import java.util.concurrent.atomic.AtomicBoolean

/** Hardware-only H.264 surface encoder used to measure camera + encoder together. */
internal class HardwareH264Encoder(
    private val width: Int,
    private val height: Int,
    private val frameRate: Int,
    private val onStats: (codecName: String, fps: Float, bitrateKbps: Int) -> Unit,
    private val onError: (message: String) -> Unit,
    private val bitrate: Int? = null,
    private val onEncoded: ((ByteArray, Long, Boolean) -> Unit)? = null
) {
    private var codec: MediaCodec? = null
    private var drainThread: Thread? = null
    private val stopping = AtomicBoolean(false)
    var inputSurface: Surface? = null
        private set
    private var started = false

    val codecName: String
        get() = codec?.name ?: "H.264 hardware"

    fun start() {
        check(!started) { "El encoder ya se inició." }
        require(width > 0 && height > 0 && frameRate > 0)

        val target=bitrate ?: targetBitrate(width,height,frameRate)
        val info=MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.firstOrNull { candidate ->
            candidate.isEncoder && candidate.isHardwareAccelerated &&
                candidate.supportedTypes.any {it.equals(MediaFormat.MIMETYPE_VIDEO_AVC,true)} &&
                runCatching {
                    val caps=candidate.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC)
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface in caps.colorFormats &&
                        caps.videoCapabilities.areSizeAndRateSupported(width,height,frameRate.toDouble()) &&
                        caps.videoCapabilities.bitrateRange.contains(target)
                }.getOrDefault(false)
        } ?: error("No hay un encoder H.264 hardware compatible con esta resolución, FPS y bitrate.")
        val capabilities=info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC)
        val rateControl=capabilities.encoderCapabilities
        val mode=when {
            rateControl.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)->MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR
            rateControl.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)->MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR
            else->null
        }
        val baseline=MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline.takeIf {
            capabilities.profileLevels.any { level -> level.profile==it }
        }
        // Some vendor encoders advertise CBR/profile support but reject those optional
        // keys at configure time. Retry without optional keys before reporting failure.
        val modes=listOf(mode,null).distinct()
        val profiles=listOf(baseline,null).distinct()
        stopping.set(false)

        var lastError:Throwable?=null
        for(currentMode in modes) {
            for(profile in profiles) {
                val format=MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC,width,height).apply {
                    setInteger(MediaFormat.KEY_COLOR_FORMAT,MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                    setInteger(MediaFormat.KEY_BIT_RATE,target)
                    setInteger(MediaFormat.KEY_FRAME_RATE,frameRate)
                    setInteger(MediaFormat.KEY_I_FRAME_INTERVAL,2)
                    currentMode?.let {setInteger(MediaFormat.KEY_BITRATE_MODE,it)}
                    profile?.let {setInteger(MediaFormat.KEY_PROFILE,it)}
                }
                val created=MediaCodec.createByCodecName(info.name)
                var surface:Surface?=null
                try {
                    created.configure(format,null,null,MediaCodec.CONFIGURE_FLAG_ENCODE)
                    surface=created.createInputSurface()
                    created.start()
                    inputSurface=surface
                    codec=created
                    started=true
                    drainThread=Thread(::drainOutput,"StreamCam-H264-drain").apply {
                        isDaemon=true
                        start()
                    }
                    return
                } catch(error:Throwable) {
                    lastError=error
                    runCatching {surface?.release()}
                    runCatching {created.release()}
                }
            }
        }
        throw IllegalStateException("El encoder H.264 del teléfono rechazó la configuración de video.",lastError)
    }

    fun stop() {
        if (!started) return
        stopping.set(true)
        drainThread?.interrupt()
        runCatching { drainThread?.join(500) }
        drainThread = null
        val activeCodec = codec
        codec = null
        runCatching { activeCodec?.stop() }
        runCatching { activeCodec?.release() }
        runCatching { inputSurface?.release() }
        inputSurface = null
        started = false
    }

    private fun drainOutput() {
        val activeCodec = codec ?: return
        val bufferInfo = MediaCodec.BufferInfo()
        var intervalStartMs = SystemClock.elapsedRealtime()
        var frames = 0
        var bytes = 0L
        var config = ByteArray(0)
        try {
            while (!stopping.get()) {
                val index = activeCodec.dequeueOutputBuffer(bufferInfo, 10_000)
                if (index >= 0) {
                    if(bufferInfo.size>0 && onEncoded!=null) {
                        val output=activeCodec.getOutputBuffer(index) ?: error("Sin salida del encoder")
                        output.position(bufferInfo.offset); output.limit(bufferInfo.offset+bufferInfo.size)
                        val payload=ByteArray(bufferInfo.size); output.get(payload)
                        if(bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0)config=payload
                        else {
                            val key=bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0
                            onEncoded.invoke(if(key)config+payload else payload,bufferInfo.presentationTimeUs,key)
                        }
                    }
                    if (bufferInfo.size > 0 && bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                        frames++
                        bytes += bufferInfo.size
                    }
                    activeCodec.releaseOutputBuffer(index, false)
                    if(bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0)break
                } else if(index==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED && onEncoded!=null) {
                    config=ByteArray(0)
                    for(name in listOf("csd-0","csd-1")) {
                        val csd=activeCodec.outputFormat.getByteBuffer(name)?.duplicate() ?: continue
                        val bytes=ByteArray(csd.remaining());csd.get(bytes);config+=bytes
                    }
                }
                val now = SystemClock.elapsedRealtime()
                val elapsed = now - intervalStartMs
                if (elapsed >= 1_000L) {
                    onStats(activeCodec.name, frames * 1_000f / elapsed, (bytes * 8 / elapsed).toInt())
                    frames = 0
                    bytes = 0L
                    intervalStartMs = now
                }
            }
        } catch (error: Throwable) {
            if (!stopping.get()) onError(error.message ?: error.javaClass.simpleName)
        }
    }

    private fun targetBitrate(width: Int, height: Int, fps: Int): Int =
        (width.toLong() * height * fps / 120L).coerceIn(1_000_000L, 12_000_000L).toInt()
}
