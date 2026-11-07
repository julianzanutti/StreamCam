package com.streamcam.diagnostics

import android.content.Context
import org.json.JSONObject
import org.webrtc.*
import java.util.concurrent.atomic.AtomicBoolean

/** User-started local WebRTC session. Either media track can be omitted. */
internal class StreamingSession(
    context: Context,
    private val localAddress: String,
    private val video: VideoSettings?,
    private val audioEnabled: Boolean,
    private val send: (JSONObject) -> Unit,
    private val status: (String) -> Unit,
    private val fatal: (String) -> Unit
) : AutoCloseable {
    private val runtime = run {
        check(context.checkSelfPermission(android.Manifest.permission.ACCESS_NETWORK_STATE)==android.content.pm.PackageManager.PERMISSION_GRANTED) {
            "Falta el permiso de estado de red requerido por WebRTC. Actualizá StreamCam."
        }
        context.getSystemService(android.net.ConnectivityManager::class.java).allNetworks
        WebRtcCodecDiagnostics(context)
    }
    private val factory = runtime.peerConnectionFactory
    private val source = video?.let { factory.createVideoSource(false) }
    private val helper = video?.let { SurfaceTextureHelper.create("StreamCamSender", runtime.eglBase.eglBaseContext) }
    private val track = source?.let { factory.createVideoTrack("streamcam-video", it) }
    private val audioSource = if(audioEnabled) factory.createAudioSource(MediaConstraints()) else null
    private val audioTrack = audioSource?.let { factory.createAudioTrack("streamcam-audio", it) }
    private var capturer: CameraVideoCapturer? = null
    private val senders = mutableListOf<RtpSender>()
    private lateinit var peer: PeerConnection
    private var remoteReady = false
    private val candidates = mutableListOf<IceCandidate>()
    private val closed = AtomicBoolean(false)
    private val statsHandler=android.os.Handler(android.os.Looper.getMainLooper())
    private var previousBytes=0L
    private var previousTime=0L
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
                    status("Wi-Fi · H.264 · %.2f Mbps".format(mbps))
                }
                previousBytes=bytes; previousTime=now
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
            override fun onIceConnectionChange(state: PeerConnection.IceConnectionState) { status("WebRTC: $state") }
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
            transceiver.setCodecPreferences(codecs); senders.add(transceiver.sender)
        }
        if(audioTrack!=null) {
            val transceiver=peer.addTransceiver(audioTrack, RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.SEND_ONLY))
            senders.add(transceiver.sender)
        }
        capturer=if(video!=null) {
            val enumerator=Camera2Enumerator(context)
            val camera=enumerator.deviceNames.firstOrNull { if(video.front)enumerator.isFrontFacing(it) else enumerator.isBackFacing(it) }
                ?: error("La cámara seleccionada no está disponible.")
            val cap=enumerator.createCapturer(camera,null) ?: error("No se pudo abrir Camera2.")
            cap.initialize(helper,context.applicationContext,checkNotNull(source).capturerObserver); cap
        } else null
    }
    fun start() {
        video?.let { settings -> checkNotNull(capturer).startCapture(settings.width,settings.height,settings.fps) }
        statsHandler.postDelayed(statsPoll,2000)
        video?.let { settings -> check(peer.setBitrate(300_000,settings.bitrate,settings.bitrate)) { "No se pudo configurar el bitrate." } }
        peer.createOffer(observer(onCreated = { description ->
            peer.setLocalDescription(observer(onSet = {
                val filtered=description.description.lineSequence().filter { line ->
                    !line.startsWith("a=candidate:") || line.removePrefix("a=").split(' ').getOrNull(4)==localAddress
                }.joinToString("\r\n")
                emit(JSONObject().put("type", "rtc.offer").put("sdp", filtered))
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
                    remoteReady=true; candidates.forEach { peer.addIceCandidate(it) }; candidates.clear()
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
        statsHandler.removeCallbacks(statsPoll); runCatching {capturer?.stopCapture()}; capturer?.dispose()
        peer.close();peer.dispose();track?.dispose();source?.dispose();helper?.dispose();audioTrack?.dispose();audioSource?.dispose();runtime.close()
    }
}
