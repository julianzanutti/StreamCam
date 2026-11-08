package com.streamcam.diagnostics

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import org.webrtc.Camera2Enumerator

internal data class CameraChoice(val id:String,val label:String)

internal data class VideoSettings(val width:Int, val height:Int, val fps:Int, val bitrate:Int, val cameraId:String) {
    fun requireSupported(context:Context, usb:Boolean?=null) {
        require(supported(context,this,usb)) {
            val profiles=availableProfiles(context,cameraId,usb)
            val sameSize=profiles.any { val option=selected(it,cameraId,2);option.width==width && option.height==height }
            val alternatives=profiles.joinToString { labels[it] }
            if(sameSize) "El encoder no admite este bitrate para la cámara seleccionada. Elegí un bitrate menor."
            else if(alternatives.isEmpty()) "Esta cámara no anuncia un formato H.264 compatible a 30 fps. Probá la otra lente."
            else "Esta cámara no admite estos ajustes. Elegí: $alternatives."
        }
    }
    companion object {
        val labels=arrayOf("480p · 640 × 480 · 4:3 · 30 fps", "720p · 1280 × 720 · 16:9 · 30 fps", "1080p · 1920 × 1080 · 16:9 · 30 fps")
        fun cameraChoices(context:Context):List<CameraChoice> = runCatching {
            val manager=context.getSystemService(CameraManager::class.java)
            val supported=Camera2Enumerator(context).deviceNames.toSet()
            manager.cameraIdList.filter { it in supported }.mapNotNull { id -> runCatching {
                val characteristics=manager.getCameraCharacteristics(id)
                val facing=when(characteristics.get(CameraCharacteristics.LENS_FACING)) {
                    CameraCharacteristics.LENS_FACING_FRONT->"Frontal"
                    CameraCharacteristics.LENS_FACING_BACK->"Trasera"
                    CameraCharacteristics.LENS_FACING_EXTERNAL->"Externa"
                    else->"Cámara"
                }
                val physicalCount=characteristics.physicalCameraIds.size
                val array=if(physicalCount>1)" · arreglo de $physicalCount sensores" else ""
                CameraChoice(id,"$facing · cámara $id$array")
            }.getOrNull()
            }
        }.getOrDefault(emptyList())

        fun selected(profile:Int,cameraId:String,bitrateMbps:Int):VideoSettings {
            val size=when(profile) {0->640 to 480;2->1920 to 1080;else->1280 to 720}
            return VideoSettings(size.first,size.second,30,bitrateMbps.coerceIn(1,20)*1_000_000,cameraId)
        }
        fun availableProfiles(context:Context,cameraId:String,usb:Boolean?=null):List<Int> =
            (0..2).filter { supported(context,selected(it,cameraId,2),usb) }
        private fun supported(context:Context,video:VideoSettings,usb:Boolean?):Boolean = runCatching {
            val manager=context.getSystemService(CameraManager::class.java)
            if(video.cameraId !in manager.cameraIdList || video.cameraId !in Camera2Enumerator(context).deviceNames)return@runCatching false
            val characteristics=manager.getCameraCharacteristics(video.cameraId)
            val map=characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return@runCatching false
            val sizes=map.getOutputSizes(SurfaceTexture::class.java).orEmpty()
            val encoderSizes=map.getOutputSizes(MediaCodec::class.java).orEmpty()
            val textureSize=sizes.any {it.width==video.width && it.height==video.height}
            val codecSurfaceSize=encoderSizes.any {it.width==video.width && it.height==video.height}
            val cameraSupportsRoute=when(usb) {
                true->codecSurfaceSize
                false->textureSize
                null->textureSize || codecSurfaceSize
            }
            if(!cameraSupportsRoute)return@runCatching false
            val ranges=characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES).orEmpty()
            if(ranges.none {it.upper>=video.fps})return@runCatching false
            MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.any { codec ->
                codec.isEncoder && codec.isHardwareAccelerated && codec.supportedTypes.any {it.equals(MediaFormat.MIMETYPE_VIDEO_AVC,true)} &&
                runCatching {
                    val caps=codec.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC)
                    val capabilities=caps.videoCapabilities
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface in caps.colorFormats &&
                        capabilities.areSizeAndRateSupported(video.width,video.height,video.fps.toDouble()) &&
                        capabilities.bitrateRange.contains(video.bitrate)
                }.getOrDefault(false)
            }
        }.getOrDefault(false)
    }
}
