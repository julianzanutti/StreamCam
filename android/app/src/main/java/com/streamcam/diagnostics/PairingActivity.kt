package com.streamcam.diagnostics

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Bundle
import android.os.Build
import android.provider.Settings
import android.os.Handler
import android.view.View
import android.widget.Button
import android.widget.TextView
import android.widget.Spinner
import android.widget.ArrayAdapter
import android.widget.AdapterView
import com.google.android.material.checkbox.MaterialCheckBox
import androidx.appcompat.app.AppCompatActivity
import androidx.core.widget.addTextChangedListener
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import androidx.core.content.FileProvider
import com.google.zxing.integration.android.IntentIntegrator
import java.io.File

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
    private var cameraOptions:List<CameraChoice> = emptyList()
    private var profileTransport:Boolean?=null
    private var refreshVideoProfiles:(()->Unit)?=null
    private var notificationAsked=false
    private val sessionObserver:()->Unit={ render() }
    private val authorized get()=CameraSession.authorized
    private val streaming get()=CameraSession.streaming
    private lateinit var cameraChoice:Spinner
    private lateinit var qualityChoice:Spinner
    private lateinit var bitrateChoice:Spinner
    private lateinit var captureModeChoice:Spinner
    private lateinit var audioInputChoice:Spinner
    private lateinit var audioSourceChoice:Spinner
    private lateinit var echoCancellation:MaterialCheckBox
    private lateinit var noiseSuppression:MaterialCheckBox
    private lateinit var automaticGainControl:MaterialCheckBox
    private lateinit var refreshAudioInputs:Button
    private lateinit var microphoneInventory:TextView
    private var audioDeviceCallback:AudioDeviceCallback?=null
    private var usbCodeDialog:androidx.appcompat.app.AlertDialog?=null
    private var pendingUpdateInstall:File?=null
    private var usbCodeHandled=false
    private var usbPendingCode:String?=null
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
                .setCaptureActivity(QrCaptureActivity::class.java)
                .setPrompt("Apuntá al QR de StreamCam en tu PC").setBeepEnabled(false)
                .setBarcodeImageEnabled(false).setOrientationLocked(false).initiateScan()
        }
        diagnostics=findViewById(R.id.diagnostics)
        diagnostics.setOnClickListener { if(!CameraSession.active)startActivity(Intent(this,MainActivity::class.java)) }
        findViewById<Button>(R.id.check_updates).setOnClickListener { checkForAndroidUpdate(findViewById(R.id.check_updates)) }
        cameraChoice=findViewById(R.id.camera_choice)
        qualityChoice=findViewById(R.id.quality_choice)
        bitrateChoice=findViewById(R.id.bitrate_choice)
        captureModeChoice=findViewById(R.id.capture_mode)
        audioInputChoice=findViewById(R.id.audio_input_choice)
        audioSourceChoice=findViewById(R.id.audio_source_choice)
        echoCancellation=findViewById(R.id.audio_echo_cancellation)
        noiseSuppression=findViewById(R.id.audio_noise_suppression)
        automaticGainControl=findViewById(R.id.audio_auto_gain)
        refreshAudioInputs=findViewById(R.id.refresh_audio_inputs)
        microphoneInventory=findViewById(R.id.audio_microphone_inventory)
        fun options(spinner:Spinner, labels:List<String>) {
            spinner.adapter=ArrayAdapter(this,android.R.layout.simple_spinner_dropdown_item,labels)
        }
        fun selected(action:(Int)->Unit)=object:AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent:AdapterView<*>?)=Unit
            override fun onItemSelected(parent:AdapterView<*>?,view:View?,position:Int,id:Long) {action(position)}
        }
        val preferences=getSharedPreferences("video-settings",MODE_PRIVATE)
        val audioPreferences=getSharedPreferences("audio-settings",MODE_PRIVATE)
        refreshAudioInputList()
        val audioSources=AudioInputCatalog.sources()
        audioSourceChoice.adapter=ArrayAdapter(this,android.R.layout.simple_spinner_dropdown_item,audioSources.map { it.label })
        val selectedSource=audioPreferences.getInt("inputSource",android.media.MediaRecorder.AudioSource.MIC)
        audioSourceChoice.setSelection(audioSources.indexOfFirst { it.value==selectedSource }.coerceAtLeast(0))
        echoCancellation.isChecked=audioPreferences.getBoolean("echoCancellation",true)
        noiseSuppression.isChecked=audioPreferences.getBoolean("noiseSuppression",true)
        automaticGainControl.isChecked=audioPreferences.getBoolean("automaticGainControl",false)
        audioInputChoice.onItemSelectedListener=selected { position ->
            val choice=audioInputChoices.getOrNull(position)
            val key=choice?.key ?: AudioInputCatalog.AUTOMATIC
            val changed=audioPreferences.getString("inputDevice",AudioInputCatalog.AUTOMATIC)!=key
            audioPreferences.edit().putString("inputDevice",key).apply()
            if(changed && (choice?.device?.type==AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                (android.os.Build.VERSION.SDK_INT>=31 && choice?.device?.type==AudioDeviceInfo.TYPE_BLE_HEADSET))
            )
                requestBluetoothConnectPermission()
        }
        refreshAudioInputs.setOnClickListener { refreshAudioInputList() }
        audioSourceChoice.onItemSelectedListener=selected { position ->
            audioPreferences.edit().putInt("inputSource",audioSources.getOrNull(position)?.value ?: android.media.MediaRecorder.AudioSource.MIC).apply()
        }
        echoCancellation.setOnCheckedChangeListener { _, enabled -> audioPreferences.edit().putBoolean("echoCancellation",enabled).apply() }
        noiseSuppression.setOnCheckedChangeListener { _, enabled -> audioPreferences.edit().putBoolean("noiseSuppression",enabled).apply() }
        automaticGainControl.setOnCheckedChangeListener { _, enabled -> audioPreferences.edit().putBoolean("automaticGainControl",enabled).apply() }
        var desiredQuality=preferences.getInt("quality",1).coerceIn(0,2)
        captureMode=CaptureMode.fromIndex(preferences.getInt("captureMode",0))
        options(captureModeChoice,CaptureMode.labels)
        captureModeChoice.setSelection(captureMode.ordinal)
        cameraOptions=VideoSettings.cameraChoices(applicationContext)
        val savedCameraId=preferences.getString("cameraId",null)
        val previousFacing=if(preferences.getInt("camera",0)==1)"Frontal" else "Trasera"
        val initialCamera=cameraOptions.indexOfFirst { it.id==savedCameraId }.takeIf { it>=0 }
            ?: cameraOptions.indexOfFirst { it.label.startsWith(previousFacing) }.coerceAtLeast(0)
        options(cameraChoice,if(cameraOptions.isEmpty())listOf("Sin cámaras Camera2 compatibles") else cameraOptions.map {it.label})
        options(bitrateChoice,bitrates.map { "$it Mbps" })
        cameraChoice.setSelection(initialCamera)
        bitrateChoice.setSelection(preferences.getInt("bitrate",2).coerceIn(0,bitrates.lastIndex))
        fun loadProfiles() {
            profileTransport=CameraSession.client?.usb
            val cameraId=cameraOptions.getOrNull(cameraChoice.selectedItemPosition)?.id
            profileIds=cameraId?.let { VideoSettings.availableProfiles(applicationContext,it,profileTransport) } ?: emptyList()
            options(qualityChoice,if(profileIds.isEmpty())listOf("Sin formatos compatibles") else profileIds.map {VideoSettings.labels[it]})
            qualityChoice.setSelection(profileIds.indexOf(desiredQuality).coerceAtLeast(0))
        }
        refreshVideoProfiles={loadProfiles()}
        loadProfiles()
        var previousCamera=cameraChoice.selectedItemPosition
        fun remember() {
            preferences.edit().putString("cameraId",cameraOptions.getOrNull(cameraChoice.selectedItemPosition)?.id).remove("camera")
                .putInt("quality",profileIds.getOrNull(qualityChoice.selectedItemPosition) ?: desiredQuality)
                .putInt("bitrate",bitrateChoice.selectedItemPosition).apply()
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
            .setMessage("Se solicitó una conexión local usando los datos recibidos de Windows. Android no puede verificar qué aplicación inició la solicitud. ¿Querés continuar con el emparejamiento?")
            .setNegativeButton("Cancelar",null).setPositiveButton("Continuar") {_,_->connectTo(invitation,false)}.show()
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
            val audioSettings=AudioInputCatalog.loadSettings(applicationContext)
            if(captureMode.audio && Build.VERSION.SDK_INT>=31 && AudioInputCatalog.requiresBluetoothConnectPermission(audioSettings) &&
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)
                add(Manifest.permission.BLUETOOTH_CONNECT)
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
            val cameraId=cameraOptions.getOrNull(cameraChoice.selectedItemPosition)?.id
            if(cameraId==null) {showMessage("No se encontró una cámara compatible en este teléfono.");return}
            VideoSettings.selected(profile,cameraId,bitrates[bitrateChoice.selectedItemPosition]).also { video ->
                runCatching {video.requireSupported(applicationContext,CameraSession.client?.usb)}.onFailure {showMessage(it.message ?: "Formato no compatible.");return}
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
        val transport=CameraSession.client?.usb
        if(transport!=profileTransport)refreshVideoProfiles?.invoke()
        if(isDestroyed)return
        val usbCode=CameraSession.usbVerificationCode
        if(usbCode==null) {
            usbCodeDialog?.dismiss(); usbCodeDialog=null; usbCodeHandled=false
            usbPendingCode=null
        } else if(usbCodeDialog==null && !isFinishing && usbPendingCode!=usbCode) {
            usbPendingCode=usbCode
            usbCodeHandled=false
            val codeInput=android.widget.EditText(this).apply {
                hint="Código de Windows"
                contentDescription="Código de verificación USB"
                inputType=android.text.InputType.TYPE_CLASS_NUMBER
                filters=arrayOf(android.text.InputFilter.LengthFilter(6))
                setSingleLine(true)
                textSize=22f
            }
            val codeStatus=TextView(this)
            val content=android.widget.LinearLayout(this).apply {
                orientation=android.widget.LinearLayout.VERTICAL
                setPadding((24*resources.displayMetrics.density).toInt(),0,(24*resources.displayMetrics.density).toInt(),0)
                addView(TextView(this@PairingActivity).apply {
                    text="Escribí los seis dígitos que muestra StreamCam en Windows. La conexión continúa únicamente si coinciden con el desafío USB recibido por el teléfono. Tenés un minuto para compararlos."
                    textSize=18f
                })
                addView(codeInput)
                addView(codeStatus)
            }
            val dialog=MaterialAlertDialogBuilder(this).setTitle("Verificá que sea tu PC")
                .setView(content).setNegativeButton("Cancelar",null).setPositiveButton("Verificar código",null).create()
            usbCodeDialog=dialog
            dialog.setOnShowListener {
                val positive=dialog.getButton(androidx.appcompat.app.AlertDialog.BUTTON_POSITIVE)
                positive.isEnabled=false
                codeInput.addTextChangedListener { editable ->
                    val matches=UsbVerificationCode.matches(usbCode,editable?.toString().orEmpty())
                    positive.isEnabled=matches
                    codeStatus.text=if(editable?.length==6 && !matches) "No coincide. Revisá el código que muestra Windows." else ""
                }
                positive.setOnClickListener {
                    if(!UsbVerificationCode.matches(usbCode,codeInput.text?.toString().orEmpty()))return@setOnClickListener
                    android.util.Log.d("StreamCamPairing","User submitted matching USB code")
                    usbCodeHandled=true; usbCodeDialog=null; usbPendingCode=null; dialog.dismiss(); CameraSession.confirmUsbVerification()
                }
            }
            dialog.setOnCancelListener {
                if(!usbCodeHandled) { usbCodeHandled=true; usbCodeDialog=null; usbPendingCode=null; CameraSession.cancelUsbVerification() }
            }
            dialog.setOnDismissListener { if(usbCodeDialog===dialog)usbCodeDialog=null }
            dialog.show()
        }
        val active=CameraSession.active
        status.text=CameraSession.message; badge.text=CameraSession.badge
        startVideo.isEnabled=authorized && !active && (!captureMode.video || profileIds.isNotEmpty())
        stopVideo.isEnabled=active && !CameraSession.closing
        disconnectPc.isEnabled=CameraSession.client!=null || CameraSession.streaming!=null || CameraSession.starting
        scan.isEnabled=CameraSession.client==null
        saved.isEnabled=CameraSession.client==null
        cameraChoice.isEnabled=!active; qualityChoice.isEnabled=!active; bitrateChoice.isEnabled=!active
        captureModeChoice.isEnabled=!active
        audioInputChoice.isEnabled=!active && captureMode.audio
        refreshAudioInputs.isEnabled=!active && captureMode.audio
        audioSourceChoice.isEnabled=!active && captureMode.audio
        echoCancellation.isEnabled=!active && captureMode.audio
        noiseSuppression.isEnabled=!active && captureMode.audio
        automaticGainControl.isEnabled=!active && captureMode.audio
        diagnostics.isEnabled=!active
        forget.isEnabled=!active
        refreshSaved()
        updateCaptureControls()
    }

    private var audioInputChoices:List<AudioInputChoice> = emptyList()
    private fun refreshAudioInputList() {
        if(!::audioInputChoice.isInitialized)return
        val preferences=getSharedPreferences("audio-settings",MODE_PRIVATE)
        val selected=preferences.getString("inputDevice",AudioInputCatalog.AUTOMATIC)
        audioInputChoices=AudioInputCatalog.inputs(this)
        val selection=AudioInputSelectionPolicy.resolve(selected,audioInputChoices.map { it.key },AudioInputCatalog.AUTOMATIC)
        if(selection.missingSavedInput)preferences.edit().putString("inputDevice",selection.key).apply()
        microphoneInventory.text=AudioInputCatalog.microphoneSummary(this)+if(selection.missingSavedInput)
            " La entrada elegida ya no está disponible; se seleccionó Automático." else ""
        audioInputChoice.onItemSelectedListener=null
        audioInputChoice.adapter=ArrayAdapter(this,android.R.layout.simple_spinner_dropdown_item,audioInputChoices.map { it.label })
        audioInputChoice.setSelection(selection.index)
        audioInputChoice.onItemSelectedListener=object:AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent:AdapterView<*>?)=Unit
            override fun onItemSelected(parent:AdapterView<*>?,view:View?,position:Int,id:Long) {
                val choice=audioInputChoices.getOrNull(position)
                val key=choice?.key ?: AudioInputCatalog.AUTOMATIC
                val changed=preferences.getString("inputDevice",AudioInputCatalog.AUTOMATIC)!=key
                preferences.edit().putString("inputDevice",key).apply()
                if(changed && (choice?.device?.type==AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                    (android.os.Build.VERSION.SDK_INT>=31 && choice?.device?.type==AudioDeviceInfo.TYPE_BLE_HEADSET))
                )
                    requestBluetoothConnectPermission()
            }
        }
    }
    private fun requestBluetoothConnectPermission() {
        if(android.os.Build.VERSION.SDK_INT>=31 &&
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT),23)
    }
    override fun onStart() {
        super.onStart()
        CameraSession.observer=sessionObserver
        render()
        val manager=getSystemService(AudioManager::class.java)
        val callback=object:AudioDeviceCallback() {
            override fun onAudioDevicesAdded(addedDevices:Array<out AudioDeviceInfo>) { refreshAudioInputList() }
            override fun onAudioDevicesRemoved(removedDevices:Array<out AudioDeviceInfo>) { refreshAudioInputList() }
        }
        audioDeviceCallback=callback
        manager.registerAudioDeviceCallback(callback,Handler(mainLooper))
    }
    @Deprecated("Android permission callback")
    override fun onRequestPermissionsResult(requestCode:Int,permissions:Array<out String>,grantResults:IntArray) {
        super.onRequestPermissionsResult(requestCode,permissions,grantResults)
        if(requestCode==21) {
            val bluetoothIndex=permissions.indexOf(Manifest.permission.BLUETOOTH_CONNECT)
            if(bluetoothIndex>=0 && grantResults.getOrNull(bluetoothIndex)==PackageManager.PERMISSION_DENIED) {
                getSharedPreferences("audio-settings",MODE_PRIVATE).edit()
                    .putString("inputDevice",AudioInputCatalog.AUTOMATIC).apply()
                refreshAudioInputList()
            }
            val cameraReady=!captureMode.video || checkSelfPermission(Manifest.permission.CAMERA)==PackageManager.PERMISSION_GRANTED
            val audioReady=!captureMode.audio || checkSelfPermission(Manifest.permission.RECORD_AUDIO)==PackageManager.PERMISSION_GRANTED
            val bluetoothReady=!captureMode.audio ||
                !AudioInputCatalog.requiresBluetoothConnectPermission(AudioInputCatalog.loadSettings(applicationContext)) ||
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED
            if(cameraReady && audioReady && bluetoothReady)requestCapturePermissions()
            else showMessage(if(captureMode.audio && !audioReady) "Permití el micrófono para transmitir audio, o elegí otro modo." else "Permití la cámara para transmitir video, o elegí Solo audio.")
        }
        if(requestCode==22)requestCapturePermissions() // A denied notification permission does not prohibit camera FGS.
        if(requestCode==23 && grantResults.firstOrNull()!=PackageManager.PERMISSION_GRANTED) {
            getSharedPreferences("audio-settings",MODE_PRIVATE).edit()
                .putString("inputDevice",AudioInputCatalog.AUTOMATIC).apply()
            refreshAudioInputList()
            showMessage("Sin permiso para usar Bluetooth. StreamCam volvió a la selección Automática.")
        }
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
        if(requestCode==24) {
            val file=pendingUpdateInstall
            pendingUpdateInstall=null
            if(file!=null && Build.VERSION.SDK_INT>=26 && packageManager.canRequestPackageInstalls()) launchApkInstaller(file)
            else if(file!=null) showMessage("Permití instalar actualizaciones para StreamCam y volvé a tocar Verificar actualizaciones.")
            return
        }
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
        usbCodeDialog?.let { dialog ->
            usbCodeDialog=null
            usbPendingCode=null
            if(!isChangingConfigurations)CameraSession.cancelUsbVerification()
            dialog.dismiss()
        }
        audioDeviceCallback?.let { getSystemService(AudioManager::class.java).unregisterAudioDeviceCallback(it) }
        audioDeviceCallback=null
        if(CameraSession.observer===sessionObserver)CameraSession.observer=null
        if(!CameraSession.active && !isChangingConfigurations)CameraSession.disconnect()
        super.onStop()
    }
    private fun checkForAndroidUpdate(button:Button) {
        button.isEnabled=false
        button.text="Buscando…"
        AndroidUpdateClient.check(BuildConfig.VERSION_NAME) { result -> runOnUiThread {
            button.isEnabled=true
            button.text="Verificar actualizaciones"
            result.fold(
                onSuccess={release ->
                    if(release==null) showMessage("Ya tenés la versión más reciente de StreamCam para Android.")
                    else MaterialAlertDialogBuilder(this)
                        .setTitle("StreamCam ${release.tag} disponible")
                        .setMessage("Se descargará el APK desde el release oficial de GitHub y se verificará su SHA-256. Android te pedirá confirmar la instalación; StreamCam no puede instalar una actualización silenciosamente.")
                        .setNegativeButton("Ahora no",null)
                        .setPositiveButton("Descargar actualización") { _,_ -> downloadAndroidUpdate(button,release) }
                        .show()
                },
                onFailure={error -> showMessage("No se pudo verificar actualizaciones: ${error.message ?: "revisá tu conexión"}")}
            )
        } }
    }
    private fun downloadAndroidUpdate(button:Button,release:AndroidUpdateClient.Release) {
        button.isEnabled=false
        button.text="Descargando…"
        AndroidUpdateClient.download(applicationContext,release) { result -> runOnUiThread {
            button.isEnabled=true
            button.text="Verificar actualizaciones"
            result.fold(
                onSuccess={file ->
                    showMessage("APK descargado y verificado. Android va a mostrar la confirmación de instalación.")
                    requestInstallPermissionOrLaunch(file)
                },
                onFailure={error -> showMessage("No se pudo descargar o verificar el APK: ${error.message ?: "error de descarga"}")}
            )
        } }
    }
    private fun requestInstallPermissionOrLaunch(file:File) {
        if(Build.VERSION.SDK_INT>=26 && !packageManager.canRequestPackageInstalls()) {
            pendingUpdateInstall=file
            val settings=Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:$packageName"))
            startActivityForResult(settings,24)
            return
        }
        launchApkInstaller(file)
    }
    private fun launchApkInstaller(file:File) {
        val uri=FileProvider.getUriForFile(this,"$packageName.updateprovider",file)
        val install=Intent(Intent.ACTION_VIEW).setDataAndType(uri,"application/vnd.android.package-archive")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        runCatching { startActivity(install) }.onFailure {
            showMessage("Android no pudo abrir el instalador. El APK verificado sigue en el almacenamiento temporal de StreamCam.")
        }
    }
    private fun disconnect() {
        stopService(Intent(this,CameraStreamingService::class.java))
        CameraSession.disconnect(); render()
    }
}
