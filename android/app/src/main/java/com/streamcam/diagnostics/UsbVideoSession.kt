package com.streamcam.diagnostics

import android.content.Context
import android.hardware.camera2.*
import android.os.Handler
import android.os.HandlerThread
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean

/** Optional H.264 video and PCM microphone over the paired TLS WebSocket carried by adb reverse. */
internal class UsbVideoSession(private val context:Context,private val video:VideoSettings?,audioEnabled:Boolean,
    private val sendVideo:(ByteArray)->Unit,private val sendAudio:(ByteArray)->Unit,
    private val status:(String)->Unit,private val fatal:(String)->Unit):AutoCloseable {
    private val closed=AtomicBoolean(false)
    private val failed=AtomicBoolean(false)
    private val thread=video?.let { HandlerThread("StreamCamUsbCamera").apply { start() } }
    private val handler=thread?.looper?.let(::Handler)
    private var camera:CameraDevice?=null
    private var session:CameraCaptureSession?=null
    private val encoder=video?.let { settings -> HardwareH264Encoder(settings.width,settings.height,settings.fps,
        onStats={_,fps,kbps->status("USB ADB · H.264 · %.1f fps · %.2f Mbps".format(fps,kbps/1000.0))},
        onError={fail("Falló el encoder USB. Detené y reconectá.")},bitrate=settings.bitrate,
        onEncoded={data,time,_->
            if(!closed.get()) {
                require(data.size<=2*1024*1024)
                val packet=ByteBuffer.allocate(20+data.size).order(ByteOrder.LITTLE_ENDIAN)
                    .putInt(0x31484353).putInt(settings.width).putInt(settings.height).putLong(time).put(data).array()
                sendVideo(packet)
            }
        }) }
    private val audio=if(audioEnabled)UsbAudioCapture(sendAudio) { message->fail(message) } else null

    private fun fail(message:String) {
        if(!closed.get() && failed.compareAndSet(false,true))fatal(message)
    }
    @Suppress("MissingPermission", "DEPRECATION")
    fun start() {
        audio?.start()
        val settings=video
        if(settings==null) { status("USB ADB · micrófono · PCM 48 kHz"); return }
        settings.requireSupported(context)
        val activeEncoder=checkNotNull(encoder)
        activeEncoder.start()
        val manager=context.getSystemService(CameraManager::class.java)
        val facing=if(settings.front)CameraCharacteristics.LENS_FACING_FRONT else CameraCharacteristics.LENS_FACING_BACK
        val id=manager.cameraIdList.firstOrNull { manager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING)==facing }
            ?: error("Cámara no disponible")
        manager.openCamera(id,object:CameraDevice.StateCallback() {
            override fun onOpened(device:CameraDevice) {
                if(closed.get()) { device.close(); return }
                camera=device
                val surface=activeEncoder.inputSurface ?: return
                runCatching { device.createCaptureSession(listOf(surface),object:CameraCaptureSession.StateCallback() {
                    override fun onConfigured(capture:CameraCaptureSession) {
                        if(closed.get()) {capture.close();return}
                        session=capture
                        runCatching {
                            val request=device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
                                addTarget(surface)
                                set(CaptureRequest.CONTROL_MODE,CameraMetadata.CONTROL_MODE_AUTO)
                                val chars=manager.getCameraCharacteristics(id)
                                val af=chars.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES) ?: intArrayOf()
                                if(CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO in af)
                                    set(CaptureRequest.CONTROL_AF_MODE,CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                                chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                                    ?.filter { it.upper==settings.fps }?.minByOrNull { it.upper-it.lower }
                                    ?.let {set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE,it)}
                            }.build()
                            capture.setRepeatingRequest(request,null,checkNotNull(handler))
                        }.onFailure { fail("No se pudo configurar la cámara USB. Detené y reintentá.") }
                    }
                    override fun onConfigureFailed(capture:CameraCaptureSession) {fail("La cámara no admite este formato USB.")}
                },checkNotNull(handler)) }.onFailure {fail("No se pudo abrir la sesión de cámara USB.")}
            }
            override fun onDisconnected(device:CameraDevice) {device.close();fail("Cámara USB desconectada.")}
            override fun onError(device:CameraDevice,error:Int) {device.close();fail("Error de cámara USB: $error")}
        },checkNotNull(handler))
    }
    override fun close() {
        if(!closed.compareAndSet(false,true))return
        audio?.close()
        val activeThread=thread
        if(activeThread!=null) {
            val done=java.util.concurrent.CountDownLatch(1)
            handler?.post {runCatching {session?.close();camera?.close()};done.countDown()}
            done.await(2,java.util.concurrent.TimeUnit.SECONDS)
            encoder?.stop();activeThread.quitSafely()
        }
    }
}
