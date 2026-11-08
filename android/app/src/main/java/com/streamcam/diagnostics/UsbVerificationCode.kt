package com.streamcam.diagnostics

/** Requires the user to transcribe the code shown by the PC into Android. */
internal object UsbVerificationCode {
    private val format = Regex("[0-9]{6}")

    fun matches(expected: String?, entered: String): Boolean {
        if (expected == null || !format.matches(expected)) return false
        val candidate = entered.trim()
        return format.matches(candidate) && candidate == expected
    }
}
