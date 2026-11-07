package com.streamcam.diagnostics

import android.content.Context
import android.os.Handler
import android.os.Looper

/** Application-scoped session; the foreground service owns capture, never an Activity. */
internal object CameraSession {
    private val main=Handler(Looper.getMainLooper())
    var observer:(()->Unit)?=null
    var client:PairingClient?=null; private set
    var streaming:AutoCloseable?=null; private set
    var authorized=false; private set
    var message="Abrí StreamCam en Windows y escaneá el QR para vincular tu teléfono."; private set
    var badge="Sin conectar"; private set
    var starting=false
    val active get()=streaming!=null || starting
    var mode=CaptureMode.VIDEO_ONLY; private set
    var settings:VideoSettings?=VideoSettings.selected(1,false,6); private set
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
            onSignal={signal->main.post { if(client===next && authorized)runCatching { (streaming as? StreamingSession)?.receive(signal) }.onFailure {
                disconnect(); status("Se interrumpió el video. Volvé a conectar.")
                app.stopService(android.content.Intent(app,CameraStreamingService::class.java))
            }}},
            onDisconnected={main.post { if(client===next) {
                disconnect(); app.stopService(android.content.Intent(app,CameraStreamingService::class.java))
            }}},reconnect=reconnect)
        client=next; badge="Conectando"; status("Verificando identidad de PC…"); next.connect()
    }
    fun start(context:Context) {
        check(authorized && streaming==null)
        val paired=client ?: error("PC desconectada")
        if(paired.usb) {
            val usb=UsbVideoSession(context.applicationContext,settings,mode.audio,
                sendVideo={data->try { paired.sendVideo(data) } catch(error:Exception) {main.post {disconnect(); status("USB interrumpido o saturado. Volvé a conectar.");context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}},
                sendAudio={data->try { paired.sendAudio(data) } catch(error:Exception) {main.post {stopVideo();status("USB interrumpido o saturado. Volvé a conectar.");context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}},
                status={text->main.post {if(client===paired)status(text)}},
                fatal={text->main.post {if(client===paired) {stopVideo();status(text);context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}})
            streaming=usb
            try {usb.start();starting=false;badge=when(mode){CaptureMode.VIDEO_ONLY->"Cámara activa · USB";CaptureMode.AUDIO_ONLY->"Audio activo · USB";CaptureMode.AUDIO_VIDEO->"Audio y cámara · USB"};status("Transmitiendo por cable USB ADB. Podés bloquear la pantalla.")}
            catch(error:Exception) {disconnect();throw error}
            return
        }
        val stream=StreamingSession(context.applicationContext,paired.localAddress,settings,mode.audio,
            send={paired.sendSignal(it)},status={text->main.post { if(client===paired)status(text) }},
            fatal={text->main.post {if(client===paired) {stopVideo();status(text);context.stopService(android.content.Intent(context,CameraStreamingService::class.java))}}})
        streaming=stream
        try { stream.start(); starting=false; badge=when(mode){CaptureMode.VIDEO_ONLY->"Cámara activa";CaptureMode.AUDIO_ONLY->"Audio activo";CaptureMode.AUDIO_VIDEO->"Audio y cámara activa"}; status("Transmitiendo. Podés bloquear la pantalla; detené desde la notificación.") }
        catch(error:Exception) { disconnect(); throw error }
    }
    fun stopVideo() {
        starting=false
        val capture=streaming;streaming=null
        runCatching {capture?.close()}
        if(capture!=null && authorized)runCatching {client?.sendSignal(org.json.JSONObject().put("type","media.stop"))}
        badge=if(authorized)"PC conectada" else "Sin conectar"
        status(if(authorized)"Transmisión detenida. Podés cambiar el modo o la calidad e iniciar de nuevo." else "PC desconectada.")
    }
    fun disconnect() {
        authorized=false; starting=false; badge="Sin conectar"
        val old=client; client=null
        val capture=streaming; streaming=null
        runCatching { capture?.close() }; runCatching { old?.close() }
        observer?.invoke()
    }
}
