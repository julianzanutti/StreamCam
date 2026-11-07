package com.streamcam.diagnostics

import org.json.JSONObject
import java.net.URI
import java.time.Instant
import java.security.MessageDigest
import java.security.cert.X509Certificate

/** Validated invitation. Never include this object in diagnostics: it contains a secret. */
internal class PairingInvitation private constructor(
    val endpoint: URI,
    val pcId: String,
    private val keyPin: ByteArray,
    val token: String,
    val nonce: String,
    val expiresAt: Instant
) {
    fun verifyCertificate(certificate: X509Certificate) {
        check(Instant.now().isBefore(expiresAt)) { "El QR venció. Generá otro en la PC." }
        val actual = MessageDigest.getInstance("SHA-256").digest(certificate.publicKey.encoded)
        check(MessageDigest.isEqual(keyPin, actual)) { "La identidad de la PC no coincide con el QR." }
    }

    override fun toString() = "PairingInvitation(redacted)"
    fun rememberedJson(): String = JSONObject().put("endpoint",endpoint.toString()).put("pc_id",pcId)
        .put("pc_key_sha256",keyPin.joinToString("") { "%02x".format(it.toInt() and 255) }).toString()

    companion object {
        fun remembered(text: String): PairingInvitation {
            val peer=JSONObject(text)
            require(peer.length()==3)
            return parse(peer.put("version",1).put("token","A".repeat(43)).put("nonce","A".repeat(22))
                .put("expires_at",Instant.now().plusSeconds(120).toString()).toString())
        }
        fun parse(text: String, now: Instant = Instant.now()): PairingInvitation {
            require(text.toByteArray(Charsets.UTF_8).size <= 2048) { "QR demasiado grande." }
            val json = JSONObject(text)
            val fields = setOf("version", "endpoint", "pc_id", "pc_key_sha256", "token", "expires_at", "nonce")
            require(json.keys().asSequence().toSet() == fields) { "Campos de QR inválidos." }
            require(json.get("version") == 1) { "Versión de QR no compatible." }
            fun string(name: String): String {
                val value = json.get(name)
                require(value is String) { "Campo de QR inválido." }
                return value
            }
            val endpoint = URI(string("endpoint"))
            require(endpoint.scheme == "wss" && endpoint.port in 1..65535 && endpoint.path == "/pair" &&
                endpoint.rawUserInfo == null && endpoint.rawQuery == null && endpoint.rawFragment == null) {
                "Dirección de pairing inválida."
            }
            // Numeric RFC1918 IPv4 only: no DNS resolution or implicit connection while parsing.
            val octets = endpoint.host?.split('.')?.map { part ->
                require(part.matches(Regex("0|[1-9][0-9]{0,2}"))) { "Se requiere una dirección local IPv4." }
                part.toInt().also { require(it in 0..255) }
            } ?: error("Dirección ausente.")
            require(octets.size == 4 && (octets == listOf(127,0,0,1) || octets[0] == 10 ||
                octets[0] == 172 && octets[1] in 16..31 || octets[0] == 192 && octets[1] == 168)) {
                "La PC debe estar en una red privada local."
            }
            val id = string("pc_id")
            require(id.matches(Regex("[A-Za-z0-9_-]{16,64}"))) { "Identidad inválida." }
            val pin = string("pc_key_sha256")
            require(pin.matches(Regex("[0-9a-f]{64}"))) { "Pin inválido." }
            val token = string("token")
            val nonce = string("nonce")
            require(token.matches(Regex("[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]"))) { "Token inválido." }
            require(nonce.matches(Regex("[A-Za-z0-9_-]{21}[AQgw]"))) { "Nonce inválido." }
            val expires = Instant.parse(string("expires_at"))
            require(expires.isAfter(now) && !expires.isAfter(now.plusSeconds(120))) { "QR vencido o con vigencia inválida." }
            return PairingInvitation(endpoint, id, pin.chunked(2).map { it.toInt(16).toByte() }.toByteArray(), token, nonce, expires)
        }
    }
}
