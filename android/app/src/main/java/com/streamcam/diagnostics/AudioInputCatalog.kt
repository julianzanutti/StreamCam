package com.streamcam.diagnostics

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.MicrophoneInfo
import android.media.MediaRecorder
import android.os.Build
import org.webrtc.audio.JavaAudioDeviceModule

internal data class AudioInputChoice(val key:String,val label:String,val device:AudioDeviceInfo?)
internal data class AudioInputSettings(
    val source:Int,
    val device:AudioDeviceInfo?,
    val echoCancellation:Boolean,
    val noiseSuppression:Boolean,
    val automaticGainControl:Boolean
)

internal data class AudioCommunicationRoute(val requested:Boolean,val deviceId:Int?,val previousAudioMode:Int?)
internal data class CommunicationOutput(val id:Int,val address:String,val name:String)

internal object AudioCommunicationRoutePolicy {
    fun selectOutput(selectedAddress:String,selectedName:String,outputs:List<CommunicationOutput>):CommunicationOutput? {
        val address=selectedAddress.trim()
        if(address.isNotEmpty())outputs.filter { it.address.isNotBlank() && it.address.equals(address,true) }
            .singleOrNull()?.let { return it }
        val name=selectedName.trim()
        if(name.isNotEmpty())outputs.filter { it.name.trim().equals(name,true) }
            .singleOrNull()?.let { return it }
        return outputs.singleOrNull()
    }
}

internal object AudioInputCatalog {
    data class Source(val value:Int,val label:String)

    fun sources():List<Source> = buildList {
        add(Source(MediaRecorder.AudioSource.MIC,"Automático · arreglo de micrófonos"))
        add(Source(MediaRecorder.AudioSource.CAMCORDER,"Cámara · micrófono para video"))
        add(Source(MediaRecorder.AudioSource.VOICE_RECOGNITION,"Voz · reconocimiento"))
        add(Source(MediaRecorder.AudioSource.VOICE_COMMUNICATION,"Comunicación · con cancelación de eco"))
        if(Build.VERSION.SDK_INT>=24) add(Source(MediaRecorder.AudioSource.UNPROCESSED,"Sin procesar · sin efectos del sistema"))
    }

    fun inputs(context:Context):List<AudioInputChoice> {
        val manager=context.getSystemService(AudioManager::class.java)
        val discovered=manager.getDevices(AudioManager.GET_DEVICES_INPUTS).mapNotNull { device ->
            if(!device.isSource) return@mapNotNull null
            val name=device.productName?.toString()?.takeIf(String::isNotBlank) ?: deviceLabel(device.type)
            // Include the current platform ID so two same-name inputs remain selectable.
            // AudioDeviceInfo IDs are runtime-scoped; stale saved IDs fall back to automatic.
            AudioInputChoice("${device.type}|${device.id}|$name","$name · ${deviceLabel(device.type)} · ${device.id}",device)
        }
        return listOf(AudioInputChoice(AUTOMATIC,"Automático · arreglo integrado",null))+discovered
    }

    /** Inventory is informational: Android's audio policy/HAL still selects and combines the built-in array. */
    fun microphoneSummary(context:Context):String {
        return runCatching {
            val manager=context.getSystemService(AudioManager::class.java)
            val microphones=manager.microphones
            val builtIn=microphones.filter { it.type==AudioDeviceInfo.TYPE_BUILTIN_MIC }
            if(builtIn.isEmpty()) return@runCatching "Android no informa cápsulas integradas; la selección automática sigue disponible."
            val groups=builtIn.map { it.group }.filter { it!=MicrophoneInfo.GROUP_UNKNOWN }.distinct().size
            val groupText=if(groups>1) " en $groups arreglos" else ""
            "Android informa ${builtIn.size} micrófono${if(builtIn.size==1) "" else "s"} integrado${if(builtIn.size==1) "" else "s"}$groupText. En Automático, Android/HAL elige y combina las cápsulas según el teléfono."
        }.getOrElse { "Android administra el arreglo integrado automáticamente; la lista de entradas muestra las rutas seleccionables." }
    }

    fun preferredInput(context:Context,key:String):AudioDeviceInfo? {
        if(key==AUTOMATIC)return null
        return inputs(context).firstOrNull { it.key==key }?.device
    }

    fun loadSettings(context:Context):AudioInputSettings {
        val preferences=context.getSharedPreferences("audio-settings",Context.MODE_PRIVATE)
        val source=preferences.getInt("inputSource",MediaRecorder.AudioSource.MIC)
        val deviceKey=preferences.getString("inputDevice",AUTOMATIC) ?: AUTOMATIC
        return AudioInputSettings(source,preferredInput(context,deviceKey),
            preferences.getBoolean("echoCancellation",true),preferences.getBoolean("noiseSuppression",true),
            preferences.getBoolean("automaticGainControl",false))
    }

    /** Bluetooth communication routing selects an output route; Android chooses its matching mic input. */
    fun prepareCommunicationRoute(context:Context,settings:AudioInputSettings):AudioCommunicationRoute {
        val selected=settings.device ?: return AudioCommunicationRoute(false,null,null)
        if(selected.type!=AudioDeviceInfo.TYPE_BLUETOOTH_SCO &&
            !(Build.VERSION.SDK_INT>=31 && selected.type==AudioDeviceInfo.TYPE_BLE_HEADSET))
            return AudioCommunicationRoute(false,null,null)
        if(Build.VERSION.SDK_INT<31)return AudioCommunicationRoute(false,selected.id,null)
        val manager=context.getSystemService(AudioManager::class.java)
        val outputType=when(selected.type) {
            AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> AudioDeviceInfo.TYPE_BLUETOOTH_SCO
            else -> AudioDeviceInfo.TYPE_BLE_HEADSET
        }
        val previousMode=manager.mode
        manager.mode=AudioManager.MODE_IN_COMMUNICATION
        try {
            val outputs=manager.availableCommunicationDevices.filter { it.type==outputType }
                .map { CommunicationOutput(it.id,it.address,it.productName?.toString().orEmpty()) }
            val routeId=AudioCommunicationRoutePolicy.selectOutput(selected.address,selected.productName?.toString().orEmpty(),outputs)?.id
                ?: throw IllegalStateException("Android no encontró una salida Bluetooth única para ese micrófono. Desconectá los otros auriculares o elegí Automático.")
            val route=manager.availableCommunicationDevices.firstOrNull { it.id==routeId }
                ?: throw IllegalStateException("La salida Bluetooth seleccionada se desconectó. Elegí otra entrada.")
            check(manager.setCommunicationDevice(route)) { "Android no pudo activar la ruta Bluetooth. Elegí Automático o reconectá el auricular." }
            return AudioCommunicationRoute(true,selected.id,previousMode)
        } catch(error:Exception) {
            if(manager.mode==AudioManager.MODE_IN_COMMUNICATION)manager.mode=previousMode
            throw error
        }
    }

    fun requiresBluetoothConnectPermission(settings:AudioInputSettings):Boolean {
        val type=settings.device?.type ?: return false
        return type==AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
            (Build.VERSION.SDK_INT>=31 && type==AudioDeviceInfo.TYPE_BLE_HEADSET)
    }

    fun clearCommunicationRoute(context:Context,route:AudioCommunicationRoute) {
        if(!route.requested || Build.VERSION.SDK_INT<31)return
        runCatching {
            val manager=context.getSystemService(AudioManager::class.java)
            manager.clearCommunicationDevice()
            if(route.previousAudioMode!=null && manager.mode==AudioManager.MODE_IN_COMMUNICATION)
                manager.mode=route.previousAudioMode
        }
    }

    fun createWebRtcAudioModule(context:Context,settings:AudioInputSettings):JavaAudioDeviceModule =
        JavaAudioDeviceModule.builder(context).setSampleRate(48_000).setAudioSource(settings.source)
            .setUseHardwareAcousticEchoCanceler(settings.echoCancellation)
            .setUseHardwareNoiseSuppressor(settings.noiseSuppression)
            .createAudioDeviceModule().also { module->settings.device?.let(module::setPreferredInputDevice) }

    private fun deviceLabel(type:Int):String = when(type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> "micrófono integrado"
        AudioDeviceInfo.TYPE_WIRED_HEADSET -> "auricular con micrófono"
        AudioDeviceInfo.TYPE_USB_DEVICE,AudioDeviceInfo.TYPE_USB_HEADSET -> "micrófono USB"
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "micrófono Bluetooth"
        else -> if(Build.VERSION.SDK_INT>=31 && type==AudioDeviceInfo.TYPE_BLE_HEADSET) "micrófono Bluetooth LE" else "entrada de audio"
    }

    const val AUTOMATIC="automatic"
}
