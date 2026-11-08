package com.streamcam.diagnostics

/** Tracks each direction of the optional post-pairing control handshake independently. */
internal class ControlNegotiationState {
    companion object {
        const val AUDIO_EFFECTS_ACK = "audio.effects.ack"
    }

    private var paired = false
    private var localHelloSent = false
    private var localOffer = emptySet<String>()
    private var peerHelloReceived = false
    private var peerReadyReceived = false
    private var localReadySent = false
    private var negotiatedAck: Boolean? = null

    val ready: Boolean get() = synchronized(this) {
        paired && negotiatedAck != null &&
            ((localHelloSent && peerReadyReceived) || (peerHelloReceived && localReadySent))
    }
    val audioEffectsAckNegotiated: Boolean get() = synchronized(this) { paired && negotiatedAck == true }

    @Synchronized
    fun reset() {
        paired = false
        localHelloSent = false
        localOffer = emptySet()
        peerHelloReceived = false
        peerReadyReceived = false
        localReadySent = false
        negotiatedAck = null
    }

    @Synchronized
    fun markPaired() {
        reset()
        paired = true
    }

    @Synchronized
    fun startLocalHello(capabilities: Set<String>) {
        check(paired && !localHelloSent) { "Unexpected local control hello." }
        validate(capabilities)
        require(capabilities.all { it == AUDIO_EFFECTS_ACK }) { "Unsupported local control capability." }
        localOffer = capabilities.toSet()
        localHelloSent = true
    }

    /** Records a peer hello and returns the intersection to send in control.ready. */
    @Synchronized
    fun receiveHello(capabilities: Set<String>): Set<String> {
        check(paired && !peerHelloReceived) { "Unexpected or duplicate control hello." }
        validate(capabilities)
        peerHelloReceived = true
        return commitIntersection(if (AUDIO_EFFECTS_ACK in capabilities) setOf(AUDIO_EFFECTS_ACK) else emptySet())
    }

    /** Records the response to our hello. It may arrive after we already answered a peer hello. */
    @Synchronized
    fun receiveReady(capabilities: Set<String>): Set<String> {
        check(paired && localHelloSent && !peerReadyReceived) { "Unexpected or duplicate control ready." }
        validate(capabilities)
        require(capabilities.all { it in localOffer }) { "Peer returned a capability we did not offer." }
        peerReadyReceived = true
        return commitIntersection(capabilities)
    }

    /** Marks our reply only after the WebSocket accepted it for sending. */
    @Synchronized
    fun markLocalReadySent(capabilities: Set<String>) {
        check(paired && peerHelloReceived && !localReadySent) { "Unexpected or duplicate local control ready." }
        validate(capabilities)
        val expected = if (negotiatedAck == true) setOf(AUDIO_EFFECTS_ACK) else emptySet()
        check(capabilities == expected) { "Local ready does not match the negotiated capability intersection." }
        localReadySent = true
    }

    @Synchronized
    private fun commitIntersection(capabilities: Set<String>): Set<String> {
        val ack = AUDIO_EFFECTS_ACK in capabilities
        val previous = negotiatedAck
        check(previous == null || previous == ack) { "Control peers disagreed on the capability intersection." }
        negotiatedAck = ack
        return if (ack) setOf(AUDIO_EFFECTS_ACK) else emptySet()
    }

    private fun validate(capabilities: Set<String>) {
        require(capabilities.size <= 16) { "Too many control capabilities." }
        require(capabilities.all { it.isNotEmpty() && it.length <= 64 }) { "Invalid control capability." }
    }
}
