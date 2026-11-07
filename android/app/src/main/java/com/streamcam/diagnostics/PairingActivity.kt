package com.streamcam.diagnostics

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.TextView
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.widget.AdapterView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.zxing.integration.android.IntentIntegrator

/** Visible pairing, consent and camera controls. Secrets never enter UI diagnostics. */
class PairingActivity : AppCompatActivity() {
    private lateinit var status: TextView
    private lateinit var badge: TextView
    private lateinit var startVideo: Button
    private lateinit var stopVideo: Button
    private lateinit var scan: Button
    private lateinit var saved: Button
    private lateinit var forget: Button
    private lateinit var diagnostics: Button
    private lateinit var disconnectPc: Button
    private var profileIds=listOf(0,1,2)
    private var notificationAsked=false
    private val sessionObserver:()->Unit={ render() }
    private val authorized get()=CameraSession.authorized
    private val streaming get()=CameraSession.streaming
    private lateinit var cameraChoice:Spinner
    private lateinit var qualityChoice:Spinner
    private lateinit var bitrateChoice:Spinner
    private lateinit var captureModeChoice:Spinner
    private var captureMode=CaptureMode.VIDEO_ONLY
    private val bitrates=listOf(2,4,6,8,10,12,16,20)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_pairing)
        status=findViewById(R.id.pairing_status)
        badge=findViewById(R.id.connection_badge)
        startVideo=findViewById(R.id.start_video)
        stopVideo=findViewById(R.id.stop_video)
        scan=findViewById(R.id.scan_qr)
        saved=findViewById(R.id.connect_saved)
        forget=findViewById(R.id.forget_saved)
        saved.setOnClickListener {
            val text=getSharedPreferences("paired-pc",MODE_PRIVATE).getString("peer",null)
            val invitation=runCatching { PairingInvitation.remembered(text ?: error("No saved PC")) }.getOrNull()
            if(invitation==null) showMessage("Conectá por USB desde Windows o escaneá un nuevo QR de Wi-Fi.")
            else connectTo(invitation,true)
        }
        forget.setOnClickListener {
            disconnect()
            getSharedPreferences("paired-pc",MODE_PRIVATE).edit().clear().apply()
            refreshSaved()
            showMessage("PC olvidada. Podés vincularla de nuevo por USB o Wi-Fi.")
        }
        notificationAsked=savedInstanceState?.getBoolean("notificationAsked") ?: false
        startVideo.setOnClickListener { requestCapturePermissions() }
        stopVideo.setOnClickListener {
            stopService(Intent(this,CameraStreamingService::class.java))
            CameraSession.stopVideo()
            showMessage("Cámara detenida. Podés cambiar los ajustes y volver a iniciarla.")
        }
        disconnectPc=findViewById(R.id.disconnect_pc)
        disconnectPc.setOnClickListener { disconnect(); showMessage("PC desconectada.") }
        scan.setOnClickListener {
            IntentIntegrator(this).setDesiredBarcodeFormats(IntentIntegrator.QR_CODE)
                .setPrompt("Apuntá al QR de StreamCam en tu PC").setBeepEnabled(false)
                .setBarcodeImageEnabled(false).setOrientationLocked(false).initiateScan()
        }
        diagnostics=findViewById(R.id.diagnostics)
        diagnostics.setOnClickListener { if(!CameraSession.active)startActivity(Intent(this,MainActivity::class.java)) }
        cameraChoice=findViewById(R.id.camera_choice)
        qualityChoice=findViewById(R.id.quality_choice)
        bitrateChoice=findViewById(R.id.bitrate_choice)
        captureModeChoice=findViewById(R.id.capture_mode)
        fun options(spinner:Spinner, labels:List<String>) {
            spinner.adapter=ArrayAdapter(this,android.R.layout.simple_spinner_dropdown_item,labels)
        }
        val preferences=getSharedPreferences("video-settings",MODE_PRIVATE)
        var desiredQuality=preferences.getInt("quality",1).coerceIn(0,2)
        captureMode=CaptureMode.fromIndex(preferences.getInt("captureMode",0))
        options(captureModeChoice,CaptureMode.labels)
        captureModeChoice.setSelection(captureMode.ordinal)
        options(cameraChoice,listOf("Trasera","Frontal"))
        options(bitrateChoice,bitrates.map { "$it Mbps" })
        cameraChoice.setSelection(preferences.getInt("camera",0).coerceIn(0,1))
        bitrateChoice.setSelection(preferences.getInt("bitrate",2).coerceIn(0,bitrates.lastIndex))
        fun loadProfiles() {
            profileIds=VideoSettings.availableProfiles(applicationContext,cameraChoice.selectedItemPosition==1)
            options(qualityChoice,if(profileIds.isEmpty())listOf("Sin formatos compatibles") else profileIds.map {VideoSettings.labels[it]})
            qualityChoice.setSelection(profileIds.indexOf(desiredQuality).coerceAtLeast(0))
        }
        loadProfiles()
        var previousCamera=cameraChoice.selectedItemPosition
        fun remember() {
            preferences.edit().putInt("camera",cameraChoice.selectedItemPosition)
                .putInt("quality",profileIds.getOrNull(qualityChoice.selectedItemPosition) ?: desiredQuality)
                .putInt("bitrate",bitrateChoice.selectedItemPosition).apply()
        }
        fun selected(action:(Int)->Unit)=object:AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent:AdapterView<*>?)=Unit
            override fun onItemSelected(parent:AdapterView<*>?,view:View?,position:Int,id:Long) {action(position)}
        }
        captureModeChoice.onItemSelectedListener=selected { position ->
            captureMode=CaptureMode.fromIndex(position)
            preferences.edit().putInt("captureMode",captureMode.ordinal).apply()
            updateCaptureControls()
        }
        qualityChoice.onItemSelectedListener=selected { position ->
            val profile=profileIds.getOrNull(position)
            if(profile!=null && profile!=desiredQuality) {
                desiredQuality=profile
                bitrateChoice.setSelection(when(profile){0->0;2->4;else->2})
            }
            remember(); render()
        }
        cameraChoice.onItemSelectedListener=selected { position ->
            if(position!=previousCamera) { previousCamera=position; loadProfiles() }
            remember(); render()
        }
        bitrateChoice.onItemSelectedListener=selected { remember() }
        refreshSaved()
        updateCaptureControls()
        handleUsbIntent(intent)
    }

    override fun onNewIntent(intent:Intent) {
        super.onNewIntent(intent);setIntent(intent);handleUsbIntent(intent)
    }
    private fun handleUsbIntent(intent:Intent?) {
        if(intent?.action!="com.streamcam.USB_CONNECT" || CameraSession.active)return
        intent.action=null // A rotation must not replay the bootstrap or reopen consent.
        val id=intent.getStringExtra("pc_id") ?: return
        val pin=intent.getStringExtra("pc_pin") ?: return
        if(!id.matches(Regex("[A-Za-z0-9_-]{16,64}")) || !pin.matches(Regex("[0-9a-f]{64}")))return
        val peer=org.json.JSONObject().put("endpoint","wss://127.0.0.1:47831/pair").put("pc_id",id).put("pc_key_sha256",pin)
        val invitation=runCatching {PairingInvitation.remembered(peer.toString())}.getOrNull() ?: return
        val saved=getSharedPreferences("paired-pc",MODE_PRIVATE).getString("peer",null)
        if(saved==peer.toString()) {connectTo(invitation,false);return}
        MaterialAlertDialogBuilder(this).setTitle("Conectar por USB")
            .setMessage("StreamCam en tu PC quiere vincularse por el cable USB. No necesitás escanear un QR.")
            .setNegativeButton("Cancelar",null).setPositiveButton("Conectar") {_,_->connectTo(invitation,false)}.show()
    }
    private fun refreshSaved() {
        val visibility=if(getSharedPreferences("paired-pc",MODE_PRIVATE).contains("peer"))View.VISIBLE else View.GONE
        saved.visibility=visibility; forget.visibility=visibility
    }
    private fun showMessage(message:String) {
        CameraSession.status(message)
    }
    private fun requestCapturePermissions() {
        if(!authorized || CameraSession.active)return
        val required=buildList {
            if(captureMode.video && checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED)
                add(Manifest.permission.CAMERA)
            if(captureMode.audio && checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED)
                add(Manifest.permission.RECORD_AUDIO)
        }
        if(required.isNotEmpty()) { requestPermissions(required.toTypedArray(),21); return }
        if(android.os.Build.VERSION.SDK_INT>=33 && !notificationAsked &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED) {
            notificationAsked=true
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS),22); return
        }
        startStream()
    }
    override fun onSaveInstanceState(outState:Bundle) {
        outState.putBoolean("notificationAsked",notificationAsked)
        super.onSaveInstanceState(outState)
    }
    private fun startStream() {
        if(!authorized || CameraSession.active)return
        val selected=if(captureMode.video) {
            val profile=profileIds.getOrNull(qualityChoice.selectedItemPosition)
            if(profile==null) {showMessage("Esta lente no tiene formatos compatibles. Probá la otra cámara.");return}
            VideoSettings.selected(profile,cameraChoice.selectedItemPosition==1,bitrates[bitrateChoice.selectedItemPosition]).also { video ->
                runCatching {video.requireSupported(applicationContext)}.onFailure {showMessage(it.message ?: "Formato no compatible.");return}
            }
        } else null
        CameraSession.configure(captureMode,selected)
        CameraSession.starting=true
        runCatching { startForegroundService(Intent(this,CameraStreamingService::class.java)) }.onFailure {
            CameraSession.starting=false; showMessage("No se pudo iniciar el servicio de cámara.")
        }
        render()
    }
    private fun render() {
        if(isDestroyed)return
        val active=CameraSession.active
        status.text=CameraSession.message; badge.text=CameraSession.badge
        startVideo.isEnabled=authorized && !active && (!captureMode.video || profileIds.isNotEmpty())
        stopVideo.isEnabled=active
        disconnectPc.isEnabled=CameraSession.client!=null || active
        scan.isEnabled=CameraSession.client==null
        saved.isEnabled=CameraSession.client==null
        cameraChoice.isEnabled=!active; qualityChoice.isEnabled=!active; bitrateChoice.isEnabled=!active
        captureModeChoice.isEnabled=!active
        diagnostics.isEnabled=!active
        forget.isEnabled=!active
        refreshSaved()
        updateCaptureControls()
    }
    override fun onStart() { super.onStart(); CameraSession.observer=sessionObserver; render() }
    @Deprecated("Android permission callback")
    override fun onRequestPermissionsResult(requestCode:Int,permissions:Array<out String>,grantResults:IntArray) {
        super.onRequestPermissionsResult(requestCode,permissions,grantResults)
        if(requestCode==21) {
            val cameraReady=!captureMode.video || checkSelfPermission(Manifest.permission.CAMERA)==PackageManager.PERMISSION_GRANTED
            val audioReady=!captureMode.audio || checkSelfPermission(Manifest.permission.RECORD_AUDIO)==PackageManager.PERMISSION_GRANTED
            if(cameraReady && audioReady)requestCapturePermissions()
            else showMessage(if(captureMode.audio && !audioReady) "Permití el micrófono para transmitir audio, o elegí otro modo." else "Permití la cámara para transmitir video, o elegí Solo audio.")
        }
        if(requestCode==22)requestCapturePermissions() // A denied notification permission does not prohibit camera FGS.
    }
    private fun updateCaptureControls() {
        if(!::captureModeChoice.isInitialized)return
        val active=CameraSession.active
        cameraChoice.isEnabled=!active && captureMode.video
        qualityChoice.isEnabled=!active && captureMode.video
        bitrateChoice.isEnabled=!active && captureMode.video
        val start=findViewById<Button>(R.id.start_video)
        start.text=when(captureMode) {
            CaptureMode.VIDEO_ONLY -> "Iniciar cámara"
            CaptureMode.AUDIO_ONLY -> "Iniciar solo audio"
            CaptureMode.AUDIO_VIDEO -> "Iniciar audio y cámara"
        }
        if(!active)start.isEnabled=authorized && (!captureMode.video || profileIds.isNotEmpty())
    }
    @Deprecated("ZXing compatibility callback")
    override fun onActivityResult(requestCode:Int,resultCode:Int,data:Intent?) {
        val result=IntentIntegrator.parseActivityResult(requestCode,resultCode,data)
        if(result==null) { super.onActivityResult(requestCode,resultCode,data); return }
        val contents=result.contents ?: return
        val invitation=runCatching { PairingInvitation.parse(contents) }.getOrElse {
            showMessage("Ese QR no es válido o ya venció. Generá uno nuevo en Windows."); return
        }
        MaterialAlertDialogBuilder(this).setTitle("¿Vincular con esta PC?")
            .setMessage("PC en ${invitation.endpoint.host}\nDespués vas a confirmar la conexión en Windows.")
            .setNegativeButton("Cancelar",null)
            .setPositiveButton("Vincular") { _,_ -> connectTo(invitation,false) }.show()
    }
    private fun connectTo(invitation:PairingInvitation,reconnect:Boolean) {
        runCatching { CameraSession.connect(applicationContext,invitation,reconnect) }.onFailure {
            CameraSession.disconnect(); CameraSession.status("No encontramos una ruta local a tu PC. Revisá la conexión.")
        }
        render()
    }
    override fun onStop() {
        if(CameraSession.observer===sessionObserver)CameraSession.observer=null
        if(!CameraSession.active && !isChangingConfigurations)CameraSession.disconnect()
        super.onStop()
    }
    private fun disconnect() {
        stopService(Intent(this,CameraStreamingService::class.java))
        CameraSession.disconnect(); render()
    }
}
