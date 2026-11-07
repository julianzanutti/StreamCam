package com.streamcam.diagnostics

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat

internal data class VideoSettings(val width:Int, val height:Int, val fps:Int, val bitrate:Int, val front:Boolean) {
    fun requireSupported(context:Context) {
        require(supported(context,this)) {
            val profiles=availableProfiles(context,front)
            val sameSize=profiles.any { val option=selected(it,front,2);option.width==width && option.height==height }
            val alternatives=profiles.joinToString { labels[it] }
            if(sameSize) "El encoder no admite este bitrate para la cámara seleccionada. Elegí un bitrate menor."
            else if(alternatives.isEmpty()) "Esta cámara no anuncia un formato H.264 compatible a 30 fps. Probá la otra lente."
            else "Esta cámara no admite estos ajustes. Elegí: $alternatives."
        }
    }
    companion object {
        val labels=arrayOf("480p · 640 × 480 · 4:3 · 30 fps", "720p · 1280 × 720 · 16:9 · 30 fps", "1080p · 1920 × 1080 · 16:9 · 30 fps")
        fun selected(profile:Int,front:Boolean,bitrateMbps:Int):VideoSettings {
            val size=when(profile) {0->640 to 480;2->1920 to 1080;else->1280 to 720}
            return VideoSettings(size.first,size.second,30,bitrateMbps.coerceIn(1,20)*1_000_000,front)
        }
        fun availableProfiles(context:Context,front:Boolean):List<Int> = (0..2).filter { supported(context,selected(it,front,2)) }
        private fun supported(context:Context,video:VideoSettings):Boolean = runCatching {
            val manager=context.getSystemService(CameraManager::class.java)
            val facing=if(video.front)CameraCharacteristics.LENS_FACING_FRONT else CameraCharacteristics.LENS_FACING_BACK
            val id=manager.cameraIdList.firstOrNull {manager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING)==facing}
                ?: return@runCatching false
            val characteristics=manager.getCameraCharacteristics(id)
            val map=characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return@runCatching false
            // Both capture paths must be supported: WebRTC uses SurfaceTexture; USB uses a MediaCodec Surface.
            val sizes=map.getOutputSizes(SurfaceTexture::class.java).orEmpty()
            val encoderSizes=map.getOutputSizes(MediaCodec::class.java).orEmpty()
            if(sizes.none {it.width==video.width && it.height==video.height} ||
                encoderSizes.none {it.width==video.width && it.height==video.height})return@runCatching false
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
