package com.streamcam.diagnostics

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.X509TrustManager

/** A dedicated pinned TLS context for precisely one QR invitation. No global TLS overrides. */
internal class PairingClient(
    private val invitation: PairingInvitation,
    private val onStatus: (String) -> Unit,
    private val onPaired: () -> Unit = {},
    private val onSignal: (JSONObject) -> Unit = {},
    private val onDisconnected: () -> Unit = {},
    private val onUsbCodeReceived: (String) -> Unit = {},
    private val reconnect: Boolean = false
) : AutoCloseable {
    private var socket: WebSocket? = null
    private val localSockets = LocalSocketFactory(invitation.endpoint.host)
    val usb: Boolean get() = invitation.endpoint.host=="127.0.0.1"
    val localAddress: String get() = localSockets.localAddress
    @Volatile private var paired = false
    private val controlNegotiation = ControlNegotiationState()
    @Volatile private var controlV2Ready = false
    @Volatile private var controlV2AckNegotiated = false
    @Volatile private var pendingUsbChallenge: String? = null

    private fun parseControlCapabilities(message: JSONObject, isPaired: Boolean, expectedType: String): Set<String>? {
        if(!isPaired || message.optString("type")!="control.$expectedType" || message.optInt("version")!=2 || message.length()!=3)
            return null
        val array=message.optJSONArray("capabilities") ?: return null
        if(array.length()>16)return null
        val result=LinkedHashSet<String>()
        for(index in 0 until array.length()) {
            val capability=array.opt(index)
            if(capability !is String || capability.isEmpty() || capability.length>64 || !result.add(capability))return null
        }
        return result
    }
    private val trust = object : X509TrustManager {
        override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
        override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) =
            throw java.security.cert.CertificateException("Client certificates unsupported")
        override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {
            if (chain.isEmpty()) throw java.security.cert.CertificateException("Missing server certificate")
            try { invitation.verifyCertificate(chain[0]) }
            catch (e: Exception) { throw java.security.cert.CertificateException("PC identity or invitation invalid", e) }
            android.util.Log.d("StreamCamPairing","TLS certificate pin verified")
        }
    }
    private val tls = SSLContext.getInstance("TLS").apply {
        init(null, arrayOf(trust), SecureRandom())
    }
    private val client = OkHttpClient.Builder()
        .socketFactory(localSockets)
        .sslSocketFactory(tls.socketFactory, trust)
        .hostnameVerifier { host, session ->
            host == invitation.endpoint.host && runCatching {
                invitation.verifyCertificate(session.peerCertificates[0] as X509Certificate)
            }.isSuccess
        }
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .pingInterval(10, TimeUnit.SECONDS)
        .retryOnConnectionFailure(false)
        .followRedirects(false)
        .build()

    fun connect() {
        check(socket == null)
        onStatus("Verificando identidad de la PC…")
        socket = client.newWebSocket(Request.Builder().url(invitation.endpoint.toString()).build(), object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                android.util.Log.d("StreamCamPairing","TLS and WebSocket ready")
                try {
                if(usb) {
                    val identity=DeviceIdentity()
                    webSocket.send(JSONObject().put("version",1).put("type","usb.hello")
                        .put("device_id",identity.id).put("device_key",identity.publicKeyBase64).toString())
                    return
                }
                if(reconnect) {
                    webSocket.send(JSONObject().put("version",1).put("type","reconnect.hello")
                        .put("device_id",DeviceIdentity().id).toString())
                    return
                }
                // TLS pin has been checked before the secret can leave the phone.
                val message = JSONObject().put("version", 1).put("type", "pair.request")
                    .put("token", invitation.token).put("nonce", invitation.nonce)
                val identity = DeviceIdentity()
                val proof = "StreamCam pair v1\n${invitation.pcId}\n${invitation.nonce}".toByteArray(Charsets.UTF_8)
                message.put("device_id", identity.id).put("device_key", identity.publicKeyBase64)
                    .put("device_proof", identity.sign(proof))
                webSocket.send(message.toString())
                onStatus("Esperando confirmación en la PC…")
                } catch (_: Exception) {
                    onStatus("No se pudo usar la identidad segura del teléfono.")
                    webSocket.close(1011,"Device identity unavailable")
                    onDisconnected()
                }
            }
            override fun onMessage(webSocket: WebSocket, text: String) {
                if (text.toByteArray().size > 131072) { webSocket.close(1009, "Message too large"); return }
                val message = runCatching { JSONObject(text) }.getOrNull()
                val type = runCatching { message?.getString("type") }.getOrNull()
                android.util.Log.d("StreamCamPairing","WebSocket message received")
                try { when (type) {
                    "pair.pending" -> onStatus("Confirmá la vinculación en la ventana de StreamCam en Windows.")
                    "usb.challenge" -> {
                        val message=JSONObject(text)
                        val challenge=message.getString("challenge")
                        val code=message.getString("verification_code")
                        check(usb && !paired && pendingUsbChallenge==null && challenge.matches(Regex("[A-Za-z0-9_-]{43}")) && code.matches(Regex("[0-9]{6}")))
                        pendingUsbChallenge=challenge
                        android.util.Log.d("StreamCamPairing","USB challenge validated")
                        onUsbCodeReceived(code)
                    }
                    "reconnect.challenge" -> {
                        val challenge=runCatching { JSONObject(text).getString("challenge") }.getOrNull()
                        if(!reconnect || paired || challenge==null || !challenge.matches(Regex("[A-Za-z0-9_-]{43}"))) {
                            webSocket.close(1002,"Invalid challenge"); return
                        }
                        val identity=DeviceIdentity()
                        val proof="StreamCam reconnect v1\n${invitation.pcId}\n$challenge".toByteArray(Charsets.UTF_8)
                        webSocket.send(JSONObject().put("version",1).put("type","reconnect.proof")
                            .put("device_id",identity.id).put("device_proof",identity.sign(proof)).toString())
                    }
                    "pair.accepted" -> {
                        if (paired) { webSocket.close(1002, "Duplicate acceptance"); return }
                        paired = true
                        controlNegotiation.markPaired()
                        val accepted=checkNotNull(message)
                        if(accepted.has("control_version")) {
                            val capabilities=accepted.optJSONArray("capabilities")
                            if(accepted.optInt("control_version")!=2 || accepted.length()!=3 || capabilities==null || capabilities.length()>16) {
                                webSocket.close(1002,"Unsupported control protocol"); return
                            }
                            val advertised=HashSet<String>()
                            for(index in 0 until capabilities.length()) {
                                val capability=capabilities.opt(index)
                                if(capability !is String || capability.length>64 || !advertised.add(capability)) {
                                    webSocket.close(1002,"Invalid control capability"); return
                                }
                            }
                            controlNegotiation.startLocalHello(setOf(ControlNegotiationState.AUDIO_EFFECTS_ACK))
                            check(webSocket.send(JSONObject().put("type","control.hello").put("version",2)
                                .put("capabilities",org.json.JSONArray().put(ControlNegotiationState.AUDIO_EFFECTS_ACK)).toString()))
                        }
                        // The peer may initiate an optional control-v2 handshake after pairing.
                        onStatus("PC verificada y autorizada."); onPaired()
                    }
                    "control.hello" -> {
                        val hello=checkNotNull(message)
                        val seen=parseControlCapabilities(hello, paired, "hello") ?: run {
                            webSocket.close(1002,"Invalid control negotiation"); return
                        }
                        val intersection=try { controlNegotiation.receiveHello(seen) }
                            catch (_: Exception) { webSocket.close(1002,"Invalid control negotiation"); return }
                        controlV2AckNegotiated=ControlNegotiationState.AUDIO_EFFECTS_ACK in intersection
                        check(webSocket.send(JSONObject().put("type","control.ready").put("version",2)
                            .put("capabilities",if(controlV2AckNegotiated) org.json.JSONArray().put("audio.effects.ack") else org.json.JSONArray()).toString()))
                        controlNegotiation.markLocalReadySent(intersection)
                        controlV2Ready=controlNegotiation.ready
                    }
                    "control.ready" -> {
                        val ready=checkNotNull(message)
                        val negotiated=parseControlCapabilities(ready, paired, "ready") ?: run {
                            webSocket.close(1002,"Invalid control intersection"); return
                        }
                        val intersection=try { controlNegotiation.receiveReady(negotiated) }
                            catch (_: Exception) { webSocket.close(1002,"Invalid control intersection"); return }
                        controlV2AckNegotiated=ControlNegotiationState.AUDIO_EFFECTS_ACK in intersection
                        controlV2Ready=controlNegotiation.ready
                    }
                    "audio.effects" -> {
                        val effects=checkNotNull(message)
                        if(!paired || effects.opt("echoCancellation") !is Boolean ||
                            (effects.has("request_id") && (!controlV2Ready || !controlV2AckNegotiated))) {
                            webSocket.close(1002,"Invalid audio effects command"); return
                        }
                        if(effects.has("request_id") && (!controlV2Ready || !controlV2AckNegotiated || effects.optInt("version")!=2 ||
                            !Regex("[0-9a-fA-F]{32}").matches(effects.optString("request_id")))) {
                            webSocket.close(1002,"Invalid audio effects request"); return
                        }
                        if(!effects.has("request_id") && effects.has("version")) {
                            webSocket.close(1002,"Unexpected audio effects version"); return
                        }
                        onSignal(effects)
                    }
                    "rtc.answer", "rtc.ice" -> runCatching {
                        check(paired); onSignal(JSONObject(text))
                    }.onFailure {
                        webSocket.close(1002, "Invalid signaling")
                    }
                    "pair.rejected" -> { onStatus(if(usb)"La PC rechazó la conexión USB. Reconectá desde Windows." else "La PC rechazó la invitación. Generá otro QR."); webSocket.close(1000, "Rejected") }
                    else -> { onStatus("Respuesta de pairing inválida."); webSocket.close(1002, "Invalid message") }
                } } catch (error: Exception) {
                    android.util.Log.e("StreamCamPairing","Message handling failed: ${error.javaClass.simpleName}")
                    onStatus("No se pudo verificar la reconexión segura.")
                    webSocket.close(1011,"Identity operation failed")
                    onDisconnected()
                }
            }
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                android.util.Log.e("StreamCamPairing","Failure: ${t.javaClass.simpleName}; cause=${t.cause?.javaClass?.simpleName}; frames="+
                    t.stackTrace.take(6).joinToString(" > ") { it.className+"."+it.methodName })
                paired = false
                controlNegotiation.reset();controlV2Ready=false;controlV2AckNegotiated=false
                val message = if(usb)when(t) {
                    is javax.net.ssl.SSLException -> "La identidad de la PC USB no coincide. Volvé a conectar desde Windows."
                    is java.net.SocketTimeoutException -> "La PC USB no respondió. Pulsá Conectar por USB en Windows."
                    else -> "Se perdió la conexión USB. Revisá el cable y volvé a conectar desde Windows."
                } else when(t) {
                    is javax.net.ssl.SSLException -> "No se pudo verificar la identidad TLS. Generá un QR nuevo y revisá la fecha de ambos equipos."
                    is java.net.SocketTimeoutException -> "La PC no respondió a tiempo. Generá otro QR y revisá el firewall y la red local."
                    is java.net.ConnectException -> "La PC no está escuchando. Pulsá Emparejar teléfono y escaneá el QR nuevo antes de que venza."
                    else -> "No se pudo conectar con la PC. Revisá red local, QR vigente y firewall."
                }
                onStatus(message)
                onDisconnected()
            }
            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                paired = false
                controlNegotiation.reset();controlV2Ready=false;controlV2AckNegotiated=false
                onStatus("Conexión con la PC cerrada.")
                onDisconnected()
            }
        })
    }

    fun sendSignal(message: JSONObject) {
        check(paired) { "Pairing requerido." }
        check(socket?.send(message.toString()) == true) { "Conexión de señalización cerrada." }
    }

    fun acknowledgeAudioEffects(command: JSONObject, ok: Boolean) {
        val requestId=command.optString("request_id")
        if(!paired || !controlV2Ready || !controlV2AckNegotiated || !Regex("[0-9a-fA-F]{32}").matches(requestId)) return
        check(socket?.send(JSONObject().put("type","audio.effects.ack").put("version",2)
            .put("request_id",requestId).put("ok",ok).toString())==true) { "Conexión de control cerrada." }
    }

    fun confirmUsbCode() {
        val challenge=pendingUsbChallenge ?: error("No hay una verificación USB pendiente")
        pendingUsbChallenge=null
        val identity=DeviceIdentity()
        val proof="StreamCam USB v1\n${invitation.pcId}\n$challenge".toByteArray(Charsets.UTF_8)
        check(socket?.send(JSONObject().put("version",1).put("type","usb.proof")
            .put("device_id",identity.id).put("device_proof",identity.sign(proof)).toString())==true) { "Conexión USB cerrada" }
        android.util.Log.d("StreamCamPairing","Signed USB proof queued")
        onStatus("Código comparado. Esperando confirmación en Windows…")
    }
    fun cancelUsbCode() {
        pendingUsbChallenge=null
        socket?.close(1000,"Verification cancelled")
    }

    fun sendVideo(data:ByteArray) {
        sendUsbBinary(data)
    }
    fun sendAudio(data:ByteArray) {
        sendUsbBinary(data)
    }
    private fun sendUsbBinary(data:ByteArray) {
        check(paired && usb) { "USB no autorizado" }
        val active=socket ?: error("USB desconectado")
        check(active.queueSize()<2*1024*1024) { "USB saturado" }
        check(active.send(okio.ByteString.of(*data))) { "USB cerrado" }
    }
    override fun close() {
        paired = false; pendingUsbChallenge=null
        controlNegotiation.reset();controlV2Ready=false;controlV2AckNegotiated=false
        socket?.cancel(); socket = null
        client.dispatcher.cancelAll()
        client.connectionPool.evictAll()
        client.dispatcher.executorService.shutdown()
    }
}
