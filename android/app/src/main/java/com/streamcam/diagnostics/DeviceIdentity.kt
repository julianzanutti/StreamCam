package com.streamcam.diagnostics

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.Signature

/** Per-install identity. Private key remains non-exportable in Android Keystore. */
internal class DeviceIdentity {
    private val alias = "streamcam-device-v1"
    private val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    init {
        if (!store.containsAlias(alias)) {
            val generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore")
            generator.initialize(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_SIGN)
                .setKeySize(2048)
                .setDigests(KeyProperties.DIGEST_SHA256)
                .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1)
                .build())
            generator.generateKeyPair()
        }
    }
    val publicKey: ByteArray get() = store.getCertificate(alias).publicKey.encoded
    val publicKeyBase64: String get() = Base64.encodeToString(publicKey, Base64.NO_WRAP)
    val id: String get() = MessageDigest.getInstance("SHA-256").digest(publicKey).joinToString("") { "%02x".format(it.toInt() and 255) }
    fun sign(challenge: ByteArray): String {
        require(challenge.size in 32..1024)
        val signature = Signature.getInstance("SHA256withRSA")
        signature.initSign(store.getKey(alias, null) as PrivateKey)
        signature.update(challenge)
        return Base64.encodeToString(signature.sign(), Base64.NO_WRAP)
    }
}
