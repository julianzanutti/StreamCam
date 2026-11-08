package com.streamcam.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test

class ControlNegotiationStateTest {
    private val ack = ControlNegotiationState.AUDIO_EFFECTS_ACK

    @Test fun peerInitiatedHandshakeUsesTheCapabilityIntersection() {
        val state = ControlNegotiationState()
        state.markPaired()

        assertEquals(setOf(ack), state.receiveHello(setOf(ack)))
        assertFalse(state.ready)
        state.markLocalReadySent(setOf(ack))
        assertTrue(state.ready)
        assertTrue(state.audioEffectsAckNegotiated)
    }

    @Test fun localInitiatedHandshakeCanNegotiateAnEmptyIntersection() {
        val state = ControlNegotiationState()
        state.markPaired()
        state.startLocalHello(setOf(ack))

        assertEquals(emptySet<String>(), state.receiveReady(emptySet()))
        assertTrue(state.ready)
        assertFalse(state.audioEffectsAckNegotiated)
    }

    @Test fun simultaneousInitiationAcceptsBothMatchingResponses() {
        val state = ControlNegotiationState()
        state.markPaired()
        state.startLocalHello(setOf(ack))

        assertEquals(setOf(ack), state.receiveHello(setOf(ack)))
        assertFalse(state.ready)
        state.markLocalReadySent(setOf(ack))
        assertTrue(state.ready)
        assertEquals(setOf(ack), state.receiveReady(setOf(ack)))
        assertTrue(state.audioEffectsAckNegotiated)
    }

    @Test fun simultaneousInitiationRejectsConflictingIntersections() {
        val state = ControlNegotiationState()
        state.markPaired()
        state.startLocalHello(setOf(ack))
        state.receiveHello(setOf(ack))

        assertThrows(IllegalStateException::class.java) { state.receiveReady(emptySet()) }
    }

    @Test fun readyCannotClaimUnsupportedCapabilities() {
        val state = ControlNegotiationState()
        state.markPaired()
        state.startLocalHello(emptySet())

        assertThrows(IllegalArgumentException::class.java) { state.receiveReady(setOf(ack)) }
    }

    @Test fun malformedCapabilitySetsAreRejectedBeforeNegotiation() {
        val state = ControlNegotiationState()
        state.markPaired()
        assertThrows(IllegalArgumentException::class.java) { state.receiveHello(setOf("")) }
        assertThrows(IllegalArgumentException::class.java) { state.receiveHello(setOf("x".repeat(65))) }
        assertThrows(IllegalArgumentException::class.java) {
            state.receiveHello((0..16).map { "capability.$it" }.toSet())
        }
        assertFalse(state.ready)
        assertEquals(setOf(ack), state.receiveHello(setOf(ack)))
    }

    @Test fun malformedLocalOffersAreRejected() {
        val state = ControlNegotiationState()
        state.markPaired()
        assertThrows(IllegalArgumentException::class.java) { state.startLocalHello(setOf("")) }
        assertThrows(IllegalArgumentException::class.java) { state.startLocalHello(setOf("x".repeat(65))) }
        assertThrows(IllegalArgumentException::class.java) {
            state.startLocalHello((0..16).map { "capability.$it" }.toSet())
        }
        state.startLocalHello(setOf(ack))
    }

    @Test fun duplicateMessagesAndUnpairedHelloAreRejected() {
        val state = ControlNegotiationState()
        assertThrows(IllegalStateException::class.java) { state.receiveHello(emptySet()) }
        state.markPaired()
        state.receiveHello(emptySet())
        assertThrows(IllegalStateException::class.java) { state.receiveHello(emptySet()) }
    }

    @Test fun resetDropsNegotiatedStateBeforeTheNextPairing() {
        val state = ControlNegotiationState()
        state.markPaired()
        assertEquals(setOf(ack), state.receiveHello(setOf(ack)))
        assertFalse(state.ready)
        state.reset()

        assertFalse(state.ready)
        assertFalse(state.audioEffectsAckNegotiated)
        assertThrows(IllegalStateException::class.java) { state.receiveReady(emptySet()) }
    }
}
