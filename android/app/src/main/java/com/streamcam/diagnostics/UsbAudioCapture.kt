package com.streamcam.diagnostics

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTimestamp
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.NoiseSuppressor
import android.media.audiofx.AutomaticGainControl
import android.os.SystemClock
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean

/** Opt-in mono 48 kHz PCM capture for USB. Sends bounded 20 ms packets over the paired TLS socket. */
internal class UsbAudioCapture(
    private val settings:AudioInputSettings,
    private val send: (ByteArray) -> Unit,
    private val onError: (String) -> Unit,
    private val onMicrophones: (String) -> Unit
) : AutoCloseable {
    private val stopping = AtomicBoolean(false)
    private var recorder: AudioRecord? = null
    private var captureRate = PcmResampler.OUTPUT_RATE
    private var worker: Thread? = null

    fun start() {
        check(worker == null) { "El micrófono ya está iniciado." }
        val (audio,rate)=openRecorder()
        captureRate=rate
        recorder = audio
        settings.device?.let { preferred->
            if(!audio.setPreferredDevice(preferred)) {
                audio.release();recorder=null
                throw IllegalStateException("Android no pudo seleccionar esa entrada de micrófono. Elegí Automático y volvé a iniciar.")
            }
        }
        val effects = mutableListOf<android.media.audiofx.AudioEffect>()
        runCatching { if (settings.echoCancellation && AcousticEchoCanceler.isAvailable()) AcousticEchoCanceler.create(audio.audioSessionId)?.let { effects.add(it); it.enabled = true } }
        runCatching { if (settings.noiseSuppression && NoiseSuppressor.isAvailable()) NoiseSuppressor.create(audio.audioSessionId)?.let { effects.add(it); it.enabled = true } }
        runCatching { if (settings.automaticGainControl && AutomaticGainControl.isAvailable()) AutomaticGainControl.create(audio.audioSessionId)?.let { effects.add(it); it.enabled = true } }
        try {
            audio.startRecording()
            check(audio.recordingState == AudioRecord.RECORDSTATE_RECORDING) { "Android no permitió iniciar el micrófono." }
            onMicrophones("USB · ${captureRate/1000} kHz · ${activeMicrophoneSummary(audio)}")
            worker = Thread({ readLoop(effects) }, "StreamCam-USB-audio").apply { isDaemon = true; start() }
        } catch (error: Exception) {
            effects.forEach { runCatching { it.release() } }
            recorder = null
            runCatching { audio.stop() }
            audio.release()
            throw error
        }
    }

    @Suppress("DEPRECATION")
    private fun activeMicrophoneSummary(audio:AudioRecord):String = runCatching {
        val active=audio.activeMicrophones
        when(active.size) {
            0 -> "Android no informó cápsulas activas; captura USB continúa con la ruta seleccionada."
            1 -> "USB · Android informa 1 micrófono activo: ${active.first().description.take(80)}"
            else -> "USB · Android informa ${active.size} micrófonos activos (${active.joinToString { it.description.take(40) }})."
        }
    }.getOrElse { "USB · captura activa; este teléfono no informa sus micrófonos activos." }

    private fun openRecorder():Pair<AudioRecord,Int> {
        var lastError:Exception?=null
        for(rate in SUPPORTED_RATES) {
            val minBuffer=AudioRecord.getMinBufferSize(rate,CHANNEL,ENCODING)
            if(minBuffer<=0)continue
            val audio=try {
                AudioRecord(settings.source,rate,CHANNEL,ENCODING,maxOf(minBuffer*2,(rate/25)*2))
            } catch(error:SecurityException) {
                throw IllegalStateException("Permití el acceso al micrófono antes de iniciar audio.",error)
            } catch(error:Exception) {
                lastError=error;continue
            }
            if(audio.state!=AudioRecord.STATE_INITIALIZED) {
                audio.release();continue
            }
            settings.device?.let { preferred->
                if(!audio.setPreferredDevice(preferred)) {
                    audio.release()
                    throw IllegalStateException("Android no pudo seleccionar esa entrada de micrófono. Elegí Automático y volvé a iniciar.")
                }
            }
            return audio to rate
        }
        throw IllegalStateException("No se pudo abrir el micrófono en 48, 44.1, 32, 24, 16, 12 ni 8 kHz. Revisá la entrada elegida y cerrá otras apps de audio.",lastError)
    }

    private fun captureTimestampUs(audio:AudioRecord,blockStartFrame:Long,blockFrames:Int,rate:Int):Long {
        val timestamp=AudioTimestamp()
        val status=runCatching { audio.getTimestamp(timestamp,AudioTimestamp.TIMEBASE_MONOTONIC) }
            .getOrDefault(AudioRecord.ERROR_INVALID_OPERATION)
        if(status==AudioRecord.SUCCESS && timestamp.framePosition>=blockStartFrame) {
            val framesBeforeTimestamp=timestamp.framePosition-blockStartFrame
            if(framesBeforeTimestamp<=rate.toLong()*2) {
                val startNanos=timestamp.nanoTime-framesBeforeTimestamp*1_000_000_000L/rate
                if(startNanos>0)return startNanos/1_000L
            }
        }
        val blockDurationUs=blockFrames.toLong()*1_000_000L/rate
        return (SystemClock.elapsedRealtimeNanos()/1_000L-blockDurationUs).coerceAtLeast(0)
    }

    private fun readLoop(effects: List<android.media.audiofx.AudioEffect>) {
        val blockFrames=captureRate/50
        val scratch = ByteArray(blockFrames*2)
        val assembler=Pcm16BlockAssembler(blockFrames)
        var framesRead=0L
        try {
            while (!stopping.get()) {
                val audio = recorder ?: break
                val count = audio.read(scratch, 0, assembler.remainingBytes, AudioRecord.READ_BLOCKING)
                if (count <= 0) {
                    if (!stopping.get()) onError("Se interrumpió la captura del micrófono. Detené e iniciá de nuevo.")
                    break
                }
                val completeBlock=assembler.append(scratch,0,count) ?: continue
                val timestampUs=captureTimestampUs(audio,framesRead,blockFrames,captureRate)
                framesRead+=blockFrames
                val output=PcmResampler.to48kMono(completeBlock,completeBlock.size,captureRate)
                // The shared SCA1 contract is 32 bytes followed by exactly the PCM payload.
                val packet = UsbAudioPacket.encode(output,output.size,output.size/2,timestampUs)
                runCatching { send(packet) }.onFailure {
                    if (!stopping.get()) onError("Se perdió el audio USB. Revisá el cable y volvé a iniciar.")
                    stopping.set(true)
                }
            }
        } catch(error:Exception) {
            if(!stopping.get())onError("Se interrumpió la captura del micrófono. Detené y volvé a iniciar.")
        } finally {
            effects.forEach { runCatching { it.release() } }
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
        val SUPPORTED_RATES = listOf(48_000,44_100,32_000,24_000,16_000,12_000,8_000)
        const val CHANNEL = AudioFormat.CHANNEL_IN_MONO
        const val ENCODING = AudioFormat.ENCODING_PCM_16BIT
    }
}

/** Wire-format codec kept Android-free so the exact USB audio header can be unit tested. */
internal object UsbAudioPacket {
    const val HEADER_BYTES = 32
    private const val MAGIC = 0x31414353 // SCA1

    fun encode(pcm: ByteArray, payloadBytes: Int, samples: Int, timestampUs: Long): ByteArray {
        require(payloadBytes in 2..pcm.size && payloadBytes % 2 == 0)
        require(samples == payloadBytes / 2 && samples <= 960)
        return ByteBuffer.allocate(HEADER_BYTES + payloadBytes).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(MAGIC).putInt(1).putInt(48_000).putInt(1).putInt(samples)
            .putLong(timestampUs).putInt(0).put(pcm, 0, payloadBytes).array()
    }
}
