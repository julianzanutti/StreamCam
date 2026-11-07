package com.streamcam.diagnostics

import android.app.*
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder

/** Explicit, user-started camera foreground service; never restarted after process death. */
class CameraStreamingService:Service() {
    private var ownedCapture:AutoCloseable?=null
    private var wakeLock:android.os.PowerManager.WakeLock?=null
    override fun onBind(intent:Intent?):IBinder?=null
    override fun onStartCommand(intent:Intent?,flags:Int,startId:Int):Int {
        if(intent?.action=="stop" || !CameraSession.authorized) { stopSelf(); return START_NOT_STICKY }
        if(CameraSession.streaming!=null)return START_NOT_STICKY
        try {
            val manager=getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(NotificationChannel("camera","Cámara en uso",NotificationManager.IMPORTANCE_LOW))
            val open=PendingIntent.getActivity(this,0,Intent(this,PairingActivity::class.java),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            val stop=PendingIntent.getService(this,1,Intent(this,CameraStreamingService::class.java).setAction("stop"),PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            val mode=CameraSession.mode
            val mediaLabel=when(mode) { CaptureMode.VIDEO_ONLY->"cámara"; CaptureMode.AUDIO_ONLY->"micrófono"; CaptureMode.AUDIO_VIDEO->"cámara y micrófono" }
            val notification=Notification.Builder(this,"camera").setSmallIcon(R.drawable.ic_camera)
                .setContentTitle("StreamCam · $mediaLabel activa").setContentText("Transmitiendo a tu PC. Tocá Detener para finalizar.")
                .setContentIntent(open).setOngoing(true).addAction(Notification.Action.Builder(null,"Detener",stop).build()).build()
            val foregroundTypes=(if(mode.video)ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA else 0) or
                (if(mode.audio)ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE else 0)
            startForeground(11,notification,foregroundTypes)
            wakeLock=getSystemService(android.os.PowerManager::class.java)
                .newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK,"StreamCam:Camera").apply {acquire(12*60*60*1000L)}
            CameraSession.start(applicationContext)
            ownedCapture=CameraSession.streaming
        } catch(error:Exception) {
            CameraSession.stopVideo()
            CameraSession.status(error.message ?: "No se pudo iniciar la transmisión. Revisá los permisos y los dispositivos ocupados.")
            stopSelf()
        }
        return START_NOT_STICKY
    }
    override fun onDestroy() {
        val lock=wakeLock;wakeLock=null
        if(lock?.isHeld==true)lock.release()
        val owned=ownedCapture;ownedCapture=null
        if(owned!=null && CameraSession.streaming===owned)CameraSession.stopVideo()
        stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy()
    }
}
