package com.streamcam.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AudioInputSelectionPolicyTest {
    private val keys=listOf("automatic","1|20|USB mic","1|21|USB mic")

    @Test fun exactRuntimeIdRemainsSelectedWhenOtherInputsArePresent() {
        val result=AudioInputSelectionPolicy.resolve("1|21|USB mic",keys,"automatic")
        assertEquals(2,result.index)
        assertEquals("1|21|USB mic",result.key)
        assertFalse(result.missingSavedInput)
    }

    @Test fun removedRuntimeIdFallsBackToAutomaticAndReportsIt() {
        val result=AudioInputSelectionPolicy.resolve("1|99|Old mic",keys,"automatic")
        assertEquals(0,result.index)
        assertEquals("automatic",result.key)
        assertTrue(result.missingSavedInput)
    }

    @Test fun duplicatedRuntimeKeyIsAmbiguousAndFallsBackToAutomatic() {
        val result=AudioInputSelectionPolicy.resolve("1|20|USB mic",keys+"1|20|USB mic","automatic")
        assertEquals(0,result.index)
        assertEquals("automatic",result.key)
        assertTrue(result.missingSavedInput)
    }

    @Test fun automaticAndEmptySelectionDoNotReportMissingInput() {
        assertFalse(AudioInputSelectionPolicy.resolve("automatic",keys,"automatic").missingSavedInput)
        assertFalse(AudioInputSelectionPolicy.resolve(null,keys,"automatic").missingSavedInput)
    }
}
