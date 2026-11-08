package com.streamcam.diagnostics

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UsbVerificationCodeTest {
    @Test fun acceptsExactSixDigitWindowsCodeIncludingLeadingZeroes() {
        assertTrue(UsbVerificationCode.matches("004218", "004218"))
    }

    @Test fun trimsClipboardWhitespaceButStillRequiresExactDigits() {
        assertTrue(UsbVerificationCode.matches("482901", " 482901\n"))
        assertFalse(UsbVerificationCode.matches("482901", "482 901"))
    }

    @Test fun rejectsIncorrectOrIncompleteCodes() {
        assertFalse(UsbVerificationCode.matches("482901", "482902"))
        assertFalse(UsbVerificationCode.matches("482901", "48290"))
        assertFalse(UsbVerificationCode.matches("482901", "4829010"))
    }

    @Test fun rejectsMalformedExpectedCodes() {
        assertFalse(UsbVerificationCode.matches(null, "482901"))
        assertFalse(UsbVerificationCode.matches("48290x", "48290x"))
        assertFalse(UsbVerificationCode.matches("48290", "48290"))
    }
}
