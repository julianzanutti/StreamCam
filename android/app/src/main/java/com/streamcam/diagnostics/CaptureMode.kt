package com.streamcam.diagnostics

/** User-selected media sent to the paired PC. Microphone capture is opt-in per session. */
internal enum class CaptureMode(val label: String, val video: Boolean, val audio: Boolean) {
    VIDEO_ONLY("Solo video", true, false),
    AUDIO_ONLY("Solo audio", false, true),
    AUDIO_VIDEO("Audio + video", true, true);

    companion object {
        val labels = entries.map { it.label }
        fun fromIndex(index: Int) = entries.getOrElse(index) { VIDEO_ONLY }
    }
}
