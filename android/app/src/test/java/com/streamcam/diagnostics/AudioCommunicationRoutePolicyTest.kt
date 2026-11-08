package com.streamcam.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class AudioCommunicationRoutePolicyTest {
    private val first=CommunicationOutput(1,"AA:00","Headset A")
    private val second=CommunicationOutput(2,"BB:00","Headset B")

    @Test fun prefersMatchingBluetoothAddressWhenSeveralHeadsetsAreConnected() {
        assertEquals(second,AudioCommunicationRoutePolicy.selectOutput("bb:00","Headset B",listOf(first,second)))
    }

    @Test fun usesUniqueProductNameWhenAddressIsUnavailable() {
        assertEquals(second,AudioCommunicationRoutePolicy.selectOutput("","Headset B",listOf(first,second)))
    }

    @Test fun refusesAmbiguousSameNameHeadsetsInsteadOfChoosingArbitrarily() {
        assertNull(AudioCommunicationRoutePolicy.selectOutput("","Headset",listOf(
            CommunicationOutput(1,"AA:00","Headset"),CommunicationOutput(2,"BB:00","Headset"))))
    }

    @Test fun permitsAnUnambiguousSingleCommunicationRoute() {
        assertEquals(first,AudioCommunicationRoutePolicy.selectOutput("","",listOf(first)))
    }
}
