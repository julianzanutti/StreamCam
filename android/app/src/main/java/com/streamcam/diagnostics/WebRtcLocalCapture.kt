package com.streamcam.diagnostics

import android.content.Context
import org.webrtc.Camera2Enumerator
import org.webrtc.CameraVideoCapturer
import org.webrtc.EglBase
import org.webrtc.PeerConnectionFactory
import org.webrtc.SurfaceTextureHelper
import org.webrtc.SurfaceViewRenderer
import org.webrtc.VideoTrack

/** Local-only Camera2 -> WebRTC VideoSource -> renderer smoke test. Creates no peer or socket. */
internal class WebRtcLocalCapture(
    context: Context,
    private val factory: PeerConnectionFactory,
    eglContext: EglBase.Context,
    private val renderer: SurfaceViewRenderer,
    width: Int,
    height: Int,
    frameRate: Int,
    onStatus: (String) -> Unit
) : AutoCloseable {
    private val appContext = context.applicationContext
    private val surfaceTextureHelper: SurfaceTextureHelper
    private val capturer: CameraVideoCapturer
    private val videoSource = factory.createVideoSource(false, true)
    private val videoTrack: VideoTrack = factory.createVideoTrack("streamcam-local-video", videoSource)
    private var started = false

    init {
        require(Camera2Enumerator.isSupported(appContext)) { "WebRTC Camera2Capturer no está disponible en este dispositivo." }
        val enumerator = Camera2Enumerator(appContext)
        val cameraName = enumerator.deviceNames.firstOrNull(enumerator::isBackFacing)
            ?: error("WebRTC no encontró una cámara trasera Camera2.")
        surfaceTextureHelper = SurfaceTextureHelper.create("StreamCamWebRtcCapture", eglContext)
            ?: error("No se pudo crear SurfaceTextureHelper.")
        capturer = enumerator.createCapturer(cameraName, object : CameraVideoCapturer.CameraEventsHandler {
            override fun onCameraError(errorDescription: String) = onStatus("WebRTC cámara: $errorDescription")
            override fun onCameraDisconnected() = onStatus("WebRTC cámara desconectada")
            override fun onCameraFreezed(errorDescription: String) = onStatus("WebRTC cámara congelada: $errorDescription")
            override fun onCameraOpening(cameraName: String) = onStatus("Abriendo cámara WebRTC $cameraName…")
            override fun onFirstFrameAvailable() = onStatus("WebRTC Camera2Capturer entregó el primer cuadro")
            override fun onCameraClosed() = Unit
        }) ?: error("WebRTC no pudo crear Camera2Capturer.")

        renderer.init(eglContext, null)
        renderer.setScalingType(org.webrtc.RendererCommon.ScalingType.SCALE_ASPECT_FIT)
        renderer.setMirror(enumerator.isFrontFacing(cameraName))
        videoTrack.addSink(renderer)
        capturer.initialize(surfaceTextureHelper, appContext, videoSource.capturerObserver)
        capturer.startCapture(width, height, frameRate)
        started = true
    }

    override fun close() {
        runCatching { videoTrack.removeSink(renderer) }
        if (started) runCatching { capturer.stopCapture() }
        started = false
        runCatching { capturer.dispose() }
        videoTrack.dispose()
        videoSource.dispose()
        surfaceTextureHelper.dispose()
        renderer.release()
    }
}
