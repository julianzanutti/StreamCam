package com.streamcam.diagnostics

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.NoiseSuppressor
import android.os.SystemClock
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean

/** Opt-in mono 48 kHz PCM capture for USB. Sends bounded 20 ms packets over the paired TLS socket. */
internal class UsbAudioCapture(
    private val send: (ByteArray) -> Unit,
    private val onError: (String) -> Unit
) : AutoCloseable {
    private val stopping = AtomicBoolean(false)
    private var recorder: AudioRecord? = null
    private var worker: Thread? = null

    fun start() {
        check(worker == null) { "El micrófono ya está iniciado." }
        val minBuffer = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL, ENCODING)
        check(minBuffer > 0) { "El teléfono no admite captura PCM a 48 kHz." }
        val audio = try {
            AudioRecord(MediaRecorder.AudioSource.MIC, SAMPLE_RATE, CHANNEL, ENCODING,
                maxOf(minBuffer * 2, SAMPLES_PER_PACKET * 2 * 4))
        } catch (error: SecurityException) {
            throw IllegalStateException("Permití el acceso al micrófono antes de iniciar audio.", error)
        }
        check(audio.state == AudioRecord.STATE_INITIALIZED) {
            audio.release(); "No se pudo abrir el micrófono. Revisá el permiso y cerrá otras apps de audio."
        }
        recorder = audio
        runCatching { if (AcousticEchoCanceler.isAvailable()) AcousticEchoCanceler.create(audio.audioSessionId)?.enabled = true }
        runCatching { if (NoiseSuppressor.isAvailable()) NoiseSuppressor.create(audio.audioSessionId)?.enabled = true }
        audio.startRecording()
        check(audio.recordingState == AudioRecord.RECORDSTATE_RECORDING) {
            audio.release(); recorder = null; "Android no permitió iniciar el micrófono."
        }
        worker = Thread(::readLoop, "StreamCam-USB-audio").apply { isDaemon = true; start() }
    }

    private fun readLoop() {
        val pcm = ByteArray(SAMPLES_PER_PACKET * 2)
        while (!stopping.get()) {
            val audio = recorder ?: return
            val count = audio.read(pcm, 0, pcm.size, AudioRecord.READ_BLOCKING)
            if (count <= 0) {
                if (!stopping.get()) onError("Se interrumpió la captura del micrófono. Detené e iniciá de nuevo.")
                return
            }
            val aligned = count - count % 2
            if (aligned == 0) continue
            val samples = aligned / 2
            val packet = ByteBuffer.allocate(32 + aligned).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(MAGIC).putInt(1).putInt(SAMPLE_RATE).putInt(1).putInt(samples)
                .putLong(SystemClock.elapsedRealtimeNanos() / 1_000L).putLong(0)
                .put(pcm, 0, aligned).array()
            runCatching { send(packet) }.onFailure {
                if (!stopping.get()) onError("Se perdió el audio USB. Revisá el cable y volvé a iniciar.")
                return
            }
        }
    }

    override fun close() {
        if (!stopping.compareAndSet(false, true)) return
        val audio = recorder; recorder = null
        runCatching { audio?.stop() }
        runCatching { worker?.interrupt(); worker?.join(500) }
        worker = null
        runCatching { audio?.release() }
    }

    private companion object {
        const val SAMPLE_RATE = 48_000
        const val SAMPLES_PER_PACKET = 960
        const val CHANNEL = AudioFormat.CHANNEL_IN_MONO
        const val ENCODING = AudioFormat.ENCODING_PCM_16BIT
        const val MAGIC = 0x31414353 // SCA1
    }
}
