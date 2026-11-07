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
    private val reconnect: Boolean = false
) : AutoCloseable {
    private var socket: WebSocket? = null
    private val localSockets = LocalSocketFactory(invitation.endpoint.host)
    val usb: Boolean get() = invitation.endpoint.host=="127.0.0.1"
    val localAddress: String get() = localSockets.localAddress
    @Volatile private var paired = false
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
                val type = runCatching { JSONObject(text).getString("type") }.getOrNull()
                try { when (type) {
                    "pair.pending" -> onStatus("Confirmá la vinculación en la ventana de StreamCam en Windows.")
                    "usb.challenge" -> {
                        val challenge=JSONObject(text).getString("challenge")
                        check(usb && !paired && challenge.matches(Regex("[A-Za-z0-9_-]{43}")))
                        val identity=DeviceIdentity()
                        val proof="StreamCam USB v1\n${invitation.pcId}\n$challenge".toByteArray(Charsets.UTF_8)
                        webSocket.send(JSONObject().put("version",1).put("type","usb.proof")
                            .put("device_id",identity.id).put("device_proof",identity.sign(proof)).toString())
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
                        paired = true; onStatus("PC verificada y autorizada."); onPaired()
                    }
                    "rtc.answer", "rtc.ice" -> runCatching {
                        check(paired); onSignal(JSONObject(text))
                    }.onFailure {
                        webSocket.close(1002, "Invalid signaling")
                    }
                    "pair.rejected" -> { onStatus(if(usb)"La PC rechazó la conexión USB. Reconectá desde Windows." else "La PC rechazó la invitación. Generá otro QR."); webSocket.close(1000, "Rejected") }
                    else -> { onStatus("Respuesta de pairing inválida."); webSocket.close(1002, "Invalid message") }
                } } catch (_: Exception) {
                    onStatus("No se pudo verificar la reconexión segura.")
                    webSocket.close(1011,"Identity operation failed")
                    onDisconnected()
                }
            }
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                android.util.Log.e("StreamCamPairing","Failure: ${t.javaClass.simpleName}; cause=${t.cause?.javaClass?.simpleName}; frames="+
                    t.stackTrace.take(6).joinToString(" > ") { it.className+"."+it.methodName })
                paired = false
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
                onStatus("Conexión con la PC cerrada.")
                onDisconnected()
            }
        })
    }

    fun sendSignal(message: JSONObject) {
        check(paired) { "Pairing requerido." }
        check(socket?.send(message.toString()) == true) { "Conexión de señalización cerrada." }
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
        paired = false
        socket?.cancel(); socket = null
        client.dispatcher.cancelAll()
        client.connectionPool.evictAll()
        client.dispatcher.executorService.shutdown()
    }
}
