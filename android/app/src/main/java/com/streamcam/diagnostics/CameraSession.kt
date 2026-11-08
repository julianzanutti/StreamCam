package com.streamcam.diagnostics

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.util.concurrent.Executors

/** Application-scoped session; the foreground service owns capture, never an Activity. */
internal object CameraSession {
    private val main=Handler(Looper.getMainLooper())
    private val captureCloser=Executors.newSingleThreadExecutor { task->Thread(task,"StreamCam-capture-close").apply {isDaemon=true} }
    private var closeGeneration=0L
    private var appContext:Context?=null
    var observer:(()->Unit)?=null
    var client:PairingClient?=null; private set
    var streaming:AutoCloseable?=null; private set
    var authorized=false; private set
    var message="Abrí StreamCam en Windows y escaneá el QR para vincular tu teléfono."; private set
    var badge="Sin conectar"; private set
    var usbVerificationCode:String?=null; private set
    var starting=false
    var closing=false; private set
    val active get()=streaming!=null || starting || closing
    var mode=CaptureMode.VIDEO_ONLY; private set
    var settings:VideoSettings?=null; private set
    private var audioRoute:AudioCommunicationRoute?=null
    fun status(text:String) { message=text; observer?.invoke() }
    fun configure(captureMode:CaptureMode,video:VideoSettings?) { mode=captureMode; settings=video }
    fun connect(context:Context, invitation:PairingInvitation,reconnect:Boolean) {
        disconnect()
        val app=context.applicationContext
        lateinit var next:PairingClient
        next=PairingClient(invitation,
            onStatus={text->main.post { if(client===next)status(text) }},
            onPaired={main.post { if(client===next) {
                app.getSharedPreferences("paired-pc",Context.MODE_PRIVATE).edit().putString("peer",invitation.rememberedJson()).apply()
                authorized=true; badge="PC conectada"; status("Todo listo. Elegí qué transmitir e iniciá la sesión.")
            }}},
            onSignal={signal->main.post { if(client===next && authorized)runCatching {
                if(signal.optString("type")=="audio.effects") {
                    app.getSharedPreferences("audio-settings",Context.MODE_PRIVATE).edit()
                        .putBoolean("echoCancellation",signal.optBoolean("echoCancellation",true)).apply()
                    if(signal.has("request_id")) next.acknowledgeAudioEffects(signal,true)
                } else (streaming as? StreamingSession)?.receive(signal)
            }.onFailure {
                disconnect(); status("Se interrumpió el video. Volvé a conectar.")
                app.stopService(android.content.Intent(app,CameraStreamingService::class.java))
            }}},
            onDisconnected={main.post { if(client===next) {
                disconnect(); app.stopService(android.content.Intent(app,CameraStreamingService::class.java))
            } }},
            onUsbCodeReceived={code->main.post { if(client===next) {android.util.Log.d("StreamCamPairing","Forwarding USB challenge to Activity");usbVerificationCode=code;observer?.invoke()} }},reconnect=reconnect)
        client=next; badge="Conectando"; status("Verificando identidad de PC…"); next.connect()
    }
    fun start(context:Context) {
        check(authorized && streaming==null && !closing)
        val paired=client ?: error("PC desconectada")
        val audioSettings=AudioInputCatalog.loadSettings(context.applicationContext)
        val app=context.applicationContext
        try {
        if(mode.audio) {
            val route=AudioInputCatalog.prepareCommunicationRoute(app,audioSettings)
            if(route.requested) { audioRoute=route; appContext=app }
        }
        if(paired.usb) {
            val usb=UsbVideoSession(context.applicationContext,settings,mode.audio,
                audioSettings=audioSettings,
                sendVideo={data->try { paired.sendVideo(data) } catch(error:Exception) {main.post {disconnect(); status("USB interrumpido o saturado. Volvé a conectar.");context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}},
                sendAudio={data->try { paired.sendAudio(data) } catch(error:Exception) {main.post {stopVideo();status("USB interrumpido o saturado. Volvé a conectar.");context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}},
                status={text->main.post {if(client===paired)status(text)}},
                fatal={text->main.post {if(client===paired) {stopVideo();status(text);context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}})
            streaming=usb
            try {usb.start();starting=false;badge=when(mode){CaptureMode.VIDEO_ONLY->"Cámara activa · USB";CaptureMode.AUDIO_ONLY->"Audio activo · USB";CaptureMode.AUDIO_VIDEO->"Audio y cámara · USB"};status("Transmitiendo por cable USB ADB. Podés bloquear la pantalla.")}
            catch(error:Exception) {disconnect();throw error}
            return
        }
        val stream=StreamingSession(context.applicationContext,paired.localAddress,settings,mode.audio,audioSettings,
            send={paired.sendSignal(it)},status={text->main.post { if(client===paired)status(text) }},
            fatal={text->main.post {if(client===paired) {stopVideo();status(text);context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}})
        streaming=stream
        try { stream.start(); starting=false; badge=when(mode){CaptureMode.VIDEO_ONLY->"Cámara activa";CaptureMode.AUDIO_ONLY->"Audio activo";CaptureMode.AUDIO_VIDEO->"Audio y cámara activa"}; status("Transmitiendo. Podés bloquear la pantalla; detené desde la notificación.") }
        catch(error:Exception) { disconnect(); throw error }
        } catch(error:Exception) {
            clearAudioRoute()
            throw error
        }
    }
    fun stopVideo() {
        starting=false
        val capture=streaming;streaming=null
        if(capture!=null && authorized)runCatching {client?.sendSignal(org.json.JSONObject().put("type","media.stop"))}
        badge=if(authorized)"PC conectada" else "Sin conectar"
        if(capture==null) {
            clearAudioRoute()
            status(if(closing)"Deteniendo la cámara y liberando sus recursos…" else if(authorized)"Transmisión detenida. Podés cambiar el modo o la calidad e iniciar de nuevo." else "PC desconectada.")
            return
        }
        closing=true
        status("Deteniendo la cámara y liberando sus recursos…")
        closeCaptureAsync(capture)
    }
    fun disconnect() {
        authorized=false; starting=false; badge="Sin conectar"; usbVerificationCode=null
        val old=client; client=null
        val capture=streaming; streaming=null
        if(capture==null)clearAudioRoute()
        runCatching { old?.close() }
        if(capture!=null) {
            closing=true
            closeCaptureAsync(capture)
        }
        observer?.invoke()
    }
    private fun closeCaptureAsync(capture:AutoCloseable) {
        val generation=++closeGeneration
        val closingClient=client
        closing=true
        captureCloser.execute {
            runCatching {capture.close()}
            clearAudioRoute()
            main.post {
                if(generation!=closeGeneration)return@post
                closing=false
                if(authorized && client===closingClient)status("Transmisión detenida. Podés cambiar el modo o la calidad e iniciar de nuevo.")
                else observer?.invoke()
            }
        }
    }
    private fun clearAudioRoute() {
        val route=audioRoute ?: return
        audioRoute=null
        val context=appContext
        appContext=null
        if(context!=null)AudioInputCatalog.clearCommunicationRoute(context,route)
    }
    fun confirmUsbVerification() {
        val active=client ?: return
        if(usbVerificationCode==null || !active.usb)return
        android.util.Log.d("StreamCamPairing","Forwarding approved USB code to the pairing client")
        usbVerificationCode=null; observer?.invoke()
        runCatching { active.confirmUsbCode() }.onFailure { disconnect();status("No se pudo completar la verificación USB. Volvé a conectar desde Windows.") }
    }
    fun cancelUsbVerification() {
        val active=client ?: return
        usbVerificationCode=null; observer?.invoke()
        active.cancelUsbCode()
    }
}
