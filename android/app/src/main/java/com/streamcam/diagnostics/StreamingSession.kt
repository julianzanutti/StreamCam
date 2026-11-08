package com.streamcam.diagnostics

import android.content.Context
import org.json.JSONObject
import org.webrtc.*
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** User-started local WebRTC session. Either media track can be omitted. */
internal class StreamingSession(
    private val context: Context,
    private val localAddress: String,
    private val video: VideoSettings?,
    private val audioEnabled: Boolean,
    private val audioSettings:AudioInputSettings,
    private val send: (JSONObject) -> Unit,
    private val status: (String) -> Unit,
    private val fatal: (String) -> Unit
) : AutoCloseable {
    private val runtime = run {
        check(context.checkSelfPermission(android.Manifest.permission.ACCESS_NETWORK_STATE)==android.content.pm.PackageManager.PERMISSION_GRANTED) {
            "Falta el permiso de estado de red requerido por WebRTC. Actualizá StreamCam."
        }
        context.getSystemService(android.net.ConnectivityManager::class.java).allNetworks
        WebRtcCodecDiagnostics(context,audioSettings.takeIf { audioEnabled })
    }
    private val factory = runtime.peerConnectionFactory
    private val source = video?.let { factory.createVideoSource(false) }
    private val helper = video?.let { SurfaceTextureHelper.create("StreamCamSender", runtime.eglBase.eglBaseContext) }
    private val track = source?.let { factory.createVideoTrack("streamcam-video", it) }
    private val audioSource = if(audioEnabled) factory.createAudioSource(MediaConstraints().apply {
        mandatory.add(MediaConstraints.KeyValuePair("googEchoCancellation",audioSettings.echoCancellation.toString()))
        mandatory.add(MediaConstraints.KeyValuePair("googNoiseSuppression",audioSettings.noiseSuppression.toString()))
        mandatory.add(MediaConstraints.KeyValuePair("googAutoGainControl",audioSettings.automaticGainControl.toString()))
    }) else null
    private val audioTrack = audioSource?.let { factory.createAudioTrack("streamcam-audio", it) }
    private var capturer: CameraVideoCapturer? = null
    private val senders = mutableListOf<RtpSender>()
    private lateinit var peer: PeerConnection
    private var remoteReady = false
    private val candidates = mutableListOf<IceCandidate>()
    private val closed = AtomicBoolean(false)
    private var echoCanceler: org.webrtc.audio.JavaAudioDeviceModule.AudioRecordStateCallback? = null
    private val statsHandler=android.os.Handler(android.os.Looper.getMainLooper())
    private val restartHandler=android.os.Handler(android.os.Looper.getMainLooper())
    private var restartAttempts=0
    private var restartPending=false
    private var remoteDescriptionReady=false
    private var lastOfferSentAt=0L
    private val restartRunnable=Runnable { restartIce() }
    private val restartTimeout=Runnable {
        if(!closed.get() && remoteDescriptionReady && restartPending && restartAttempts>0 &&
            android.os.SystemClock.elapsedRealtime()-lastOfferSentAt>=10_000L) {
            if(restartAttempts>=2) { restartPending=false; fatal("Se perdió la conexión Wi‑Fi. Detené y volvé a iniciar la transmisión.") }
            else { restartPending=false; scheduleRestart(0) }
        }
    }
    private var previousBytes=0L
    private var previousTime=0L
    private var previousVideoFrames=0L
    private val capturedVideoFrames=AtomicLong(0)
    private val actualVideoSize=AtomicReference("esperando primer cuadro")
    private val videoProbeSink=object:VideoSink {
        override fun onFrame(frame:VideoFrame) {
            actualVideoSize.set("${frame.buffer.width} × ${frame.buffer.height}")
            capturedVideoFrames.incrementAndGet()
        }
    }
    private val statsPoll=object:Runnable {
        override fun run() {
            if(closed.get())return
            peer.getStats { report -> statsHandler.post { if(!closed.get()) {
                val videoStats=report.statsMap.values.firstOrNull { it.type=="outbound-rtp" &&
                    (it.members["kind"]=="video" || it.members["mediaType"]=="video") }
                val bytes=(videoStats?.members?.get("bytesSent") as? Number)?.toLong() ?: 0L
                val now=android.os.SystemClock.elapsedRealtime()
                if(video!=null && previousTime>0 && bytes>=previousBytes) {
                    val mbps=(bytes-previousBytes)*8.0/((now-previousTime).coerceAtLeast(1)*1000)
                    val fps=(capturedVideoFrames.get()-previousVideoFrames)*1000.0/(now-previousTime).coerceAtLeast(1)
                    status("Wi-Fi · H.264 · cámara ${video.cameraId} · ${actualVideoSize.get()} · %.1f fps captura · %.2f Mbps".format(fps,mbps))
                }
                previousBytes=bytes; previousTime=now
                previousVideoFrames=capturedVideoFrames.get()
            }}}
            statsHandler.postDelayed(this,2000)
        }
    }
    private fun emit(message: JSONObject) {
        if(!closed.get()) runCatching { send(message) }.onFailure { status("La señalización se cerró. Detené y reconectá la sesión.") }
    }
    init {
        check(video!=null || audioEnabled) { "Elegí video, audio o ambos." }
        val config = PeerConnection.RTCConfiguration(emptyList()).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_ONCE
            tcpCandidatePolicy = PeerConnection.TcpCandidatePolicy.DISABLED
        }
        peer = factory.createPeerConnection(config, object : PeerConnection.Observer {
            override fun onSignalingChange(state: PeerConnection.SignalingState) = Unit
            override fun onIceConnectionChange(state: PeerConnection.IceConnectionState) {
                status("WebRTC: $state")
                when(state) {
                    PeerConnection.IceConnectionState.CONNECTED, PeerConnection.IceConnectionState.COMPLETED -> {
                        restartHandler.removeCallbacks(restartRunnable)
                        restartAttempts=0; restartPending=false
                    }
                    PeerConnection.IceConnectionState.DISCONNECTED -> scheduleRestart(2500)
                    PeerConnection.IceConnectionState.FAILED -> scheduleRestart(0)
                    else -> Unit
                }
            }
            override fun onIceConnectionReceivingChange(receiving: Boolean) = Unit
            override fun onIceGatheringChange(state: PeerConnection.IceGatheringState) = Unit
            override fun onIceCandidate(candidate: IceCandidate) {
                val parts=candidate.sdp.split(' ')
                if(parts.size<8 || parts[4]!=localAddress || parts[6]!="typ" || parts[7]!="host") return
                emit(JSONObject().put("type", "rtc.ice").put("mid", candidate.sdpMid)
                    .put("mline", candidate.sdpMLineIndex).put("candidate", candidate.sdp))
            }
            override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>) = Unit
            override fun onAddStream(stream: MediaStream) = Unit
            override fun onRemoveStream(stream: MediaStream) = Unit
            override fun onDataChannel(channel: DataChannel) = Unit
            override fun onRenegotiationNeeded() = Unit
        }) ?: error("No se pudo crear la sesión WebRTC.")
        if(track!=null) {
            val transceiver=peer.addTransceiver(track, RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.SEND_ONLY))
            val codecs=factory.getRtpSenderCapabilities(MediaStreamTrack.MediaType.MEDIA_TYPE_VIDEO).codecs.filter { it.name.equals("H264",true) }
            check(codecs.isNotEmpty()) { "H.264 hardware no disponible." }
            transceiver.setCodecPreferences(codecs); senders.add(transceiver.sender); track.addSink(videoProbeSink)
        }
        if(audioTrack!=null) {
            val transceiver=peer.addTransceiver(audioTrack, RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.SEND_ONLY))
            senders.add(transceiver.sender)
        }
        capturer=if(video!=null) {
            val enumerator=Camera2Enumerator(context)
            val camera=video.cameraId.takeIf { it in enumerator.deviceNames }
                ?: error("La cámara seleccionada ya no está disponible. Elegí otra cámara.")
            val cap=enumerator.createCapturer(camera,null) ?: error("No se pudo abrir Camera2.")
            cap.initialize(helper,context.applicationContext,checkNotNull(source).capturerObserver); cap
        } else null
    }
    fun start() {
        video?.requireSupported(context,usb=false)
        video?.let { settings -> checkNotNull(capturer).startCapture(settings.width,settings.height,settings.fps) }
        statsHandler.postDelayed(statsPoll,2000)
        video?.let { settings -> check(peer.setBitrate(300_000,settings.bitrate,settings.bitrate)) { "No se pudo configurar el bitrate." } }
        createAndSendOffer()
    }
    private fun scheduleRestart(delayMs:Long) {
        if(closed.get() || !remoteDescriptionReady || restartPending)return
        if(restartAttempts>=2) { fatal("Se perdió la conexión Wi‑Fi. Detené y volvé a iniciar la transmisión."); return }
        restartPending=true
        restartHandler.removeCallbacks(restartRunnable)
        restartHandler.postDelayed(restartRunnable,delayMs)
    }
    private fun restartIce() {
        if(closed.get() || !restartPending || !remoteDescriptionReady)return
        if(restartAttempts>=2) { restartPending=false; fatal("Se perdió la conexión Wi‑Fi. Detené y volvé a iniciar la transmisión."); return }
        restartAttempts++
        status("Recuperando Wi‑Fi (intento $restartAttempts de 2)…")
        runCatching { peer.restartIce(); createAndSendOffer() }.onFailure {
            restartPending=false
            fatal("No se pudo recuperar la conexión Wi‑Fi. Detené y volvé a iniciar la transmisión.")
        }
    }
    private fun createAndSendOffer() {
        peer.createOffer(observer(onCreated = { description ->
            peer.setLocalDescription(observer(onSet = {
                val filtered=description.description.lineSequence().filter { line ->
                    !line.startsWith("a=candidate:") || line.removePrefix("a=").split(' ').getOrNull(4)==localAddress
                }.joinToString("\r\n")
                emit(JSONObject().put("type", "rtc.offer").put("sdp", filtered))
                lastOfferSentAt=android.os.SystemClock.elapsedRealtime()
                restartHandler.removeCallbacks(restartRunnable)
                if(restartAttempts>0) restartHandler.postDelayed(restartTimeout,10_000)
            }), description)
        }), MediaConstraints())
    }
    fun receive(message: JSONObject) {
        if(closed.get())return
        when(message.getString("type")) {
            "rtc.answer" -> peer.setRemoteDescription(observer(onSet = {
                synchronized(candidates) {
                    video?.let { settings ->
                        val sender=senders.firstOrNull() ?: error("Falta sender de video")
                        val parameters=sender.parameters
                        parameters.encodings.forEach { it.maxBitrateBps=settings.bitrate; it.maxFramerate=settings.fps }
                        check(sender.setParameters(parameters)) { "No se pudo configurar la calidad." }
                    }
                    remoteDescriptionReady=true; remoteReady=true
                    // Keep the restart deadline armed until ICE actually reconnects.
                    if(restartAttempts>0) restartPending=true
                    if(restartAttempts==0) { restartPending=false; restartHandler.removeCallbacks(restartRunnable) }
                    candidates.forEach { peer.addIceCandidate(it) }; candidates.clear()
                }
            }), SessionDescription(SessionDescription.Type.ANSWER,message.getString("sdp")))
            "rtc.ice" -> synchronized(candidates) {
                val candidate=IceCandidate(message.getString("mid"),message.getInt("mline"),message.getString("candidate"))
                if(remoteReady)peer.addIceCandidate(candidate) else {check(candidates.size<64);candidates.add(candidate)}
            }
        }
    }
    private fun observer(onCreated:(SessionDescription)->Unit={},onSet:()->Unit={})=object:SdpObserver {
        override fun onCreateSuccess(description:SessionDescription) {if(!closed.get())runCatching {onCreated(description)}.onFailure {fatal("Falló la configuración de medios. Detené y volvé a iniciar.")}}
        override fun onSetSuccess() {if(!closed.get())runCatching {onSet()}.onFailure {fatal("La PC no pudo aplicar la configuración de medios.")}}
        override fun onCreateFailure(error:String) {if(!closed.get())fatal("Falló la negociación de medios.")}
        override fun onSetFailure(error:String) {if(!closed.get())fatal("La PC rechazó la configuración de medios.")}
    }
    override fun close() {
        if(!closed.compareAndSet(false,true))return
        statsHandler.removeCallbacks(statsPoll); restartHandler.removeCallbacksAndMessages(null)
        runCatching {track?.removeSink(videoProbeSink)};runCatching {capturer?.stopCapture()}; capturer?.dispose()
        peer.close();peer.dispose();track?.dispose();source?.dispose();helper?.dispose();audioTrack?.dispose();audioSource?.dispose();runtime.close()
    }
}
