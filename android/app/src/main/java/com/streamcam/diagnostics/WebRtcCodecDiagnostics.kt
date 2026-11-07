package com.streamcam.diagnostics

import android.content.Context
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.EglBase
import org.webrtc.HardwareVideoEncoderFactory
import org.webrtc.MediaStreamTrack
import org.webrtc.PeerConnectionFactory

/** Loads the pinned WebRTC runtime and reports codecs from a hardware-only send factory. */
internal class WebRtcCodecDiagnostics(context: Context) : AutoCloseable {
    internal val eglBase: EglBase
    internal val peerConnectionFactory: PeerConnectionFactory
    internal val hardwareVideoEncoderFactory: HardwareVideoEncoderFactory
    val report: String

    init {
        initializeRuntime(context.applicationContext)
        eglBase = EglBase.create()
        hardwareVideoEncoderFactory = HardwareVideoEncoderFactory(
            eglBase.eglBaseContext,
            false,
            false
        )
        peerConnectionFactory = PeerConnectionFactory.builder()
            .setVideoEncoderFactory(hardwareVideoEncoderFactory)
            .setVideoDecoderFactory(DefaultVideoDecoderFactory(eglBase.eglBaseContext))
            .createPeerConnectionFactory()
        val codecs = peerConnectionFactory
            .getRtpSenderCapabilities(MediaStreamTrack.MediaType.MEDIA_TYPE_VIDEO)
            .codecs
        val h264 = codecs.filter { it.name.equals("H264", ignoreCase = true) }
        report = buildString {
            appendLine("WebRTC Android SDK: inicializado")
            appendLine("Fábrica de envío: solo codecs H.264 hardware")
            appendLine("H.264 disponible para envío: ${h264.isNotEmpty()}")
            h264.forEach { appendLine("  H.264 parameters: ${it.parameters}") }
        }
    }

    override fun close() {
        peerConnectionFactory.dispose()
        eglBase.release()
    }

    private fun initializeRuntime(context: Context) {
        synchronized(INIT_LOCK) {
            if (initialized) return
            PeerConnectionFactory.initialize(
                PeerConnectionFactory.InitializationOptions.builder(context)
                    .createInitializationOptions()
            )
            initialized = true
        }
    }

    private companion object {
        val INIT_LOCK = Any()
        @Volatile var initialized = false
    }
}
