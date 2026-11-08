#include "ControlNegotiationState.h"

#include <iostream>

int main() {
    using State = streamcam::ControlNegotiationState;
    const State::Capabilities ack{"audio.effects.ack"};
    const State::Capabilities empty;

    // One-sided initiation remains valid for clients that only answer hello.
    State localInitiated;
    localInitiated.markPaired(ack);
    localInitiated.startLocalHello();
    localInitiated.receiveReady(ack);
    if (!localInitiated.ready() || !localInitiated.supports("audio.effects.ack")) return 1;

    State peerInitiated;
    peerInitiated.markPaired(ack);
    if (peerInitiated.receiveHello(ack) != ack) return 2;
    peerInitiated.markLocalReadySent(ack);
    if (!peerInitiated.ready() || !peerInitiated.supports("audio.effects.ack")) return 3;

    // Both peers may hello at once; ready can arrive before or after the peer hello.
    State simultaneous;
    simultaneous.markPaired(ack);
    simultaneous.startLocalHello();
    simultaneous.receiveReady(ack);
    if (!simultaneous.ready()) return 4;
    if (simultaneous.receiveHello(ack) != ack || !simultaneous.ready()) return 5;
    simultaneous.markLocalReadySent(ack);
    if (!simultaneous.ready() || !simultaneous.supports("audio.effects.ack")) return 6;

    State reverseOrder;
    reverseOrder.markPaired(ack);
    reverseOrder.startLocalHello();
    if (reverseOrder.receiveHello(ack) != ack || reverseOrder.ready()) return 7;
    reverseOrder.markLocalReadySent(ack);
    if (!reverseOrder.ready()) return 8;
    reverseOrder.receiveReady(ack);
    if (!reverseOrder.ready()) return 9;

    State emptyIntersection;
    emptyIntersection.markPaired(ack);
    emptyIntersection.startLocalHello();
    if (emptyIntersection.receiveHello(empty) != empty) return 10;
    emptyIntersection.markLocalReadySent(empty);
    emptyIntersection.receiveReady(empty);
    if (!emptyIntersection.ready() || emptyIntersection.supports("audio.effects.ack")) return 11;

    State conflict;
    conflict.markPaired(ack);
    conflict.startLocalHello();
    conflict.receiveReady(empty);
    try { conflict.receiveHello(ack); return 12; }
    catch (const std::logic_error &) {}

    State invalid;
    try { invalid.receiveHello(ack); return 13; }
    catch (const std::logic_error &) {}
    invalid.markPaired(ack);
    invalid.startLocalHello();
    try { invalid.receiveReady(State::Capabilities{"not-supported"}); return 14; }
    catch (const std::invalid_argument &) {}

    State malformed;
    malformed.markPaired(ack);
    try { malformed.receiveHello(State::Capabilities{""}); return 15; }
    catch (const std::invalid_argument &) {}
    try { malformed.receiveHello(State::Capabilities{std::string(65,'x')}); return 16; }
    catch (const std::invalid_argument &) {}
    State tooMany;
    tooMany.markPaired(ack);
    State::Capabilities oversized;
    for (int index=0; index<17; ++index) oversized.insert("capability."+std::to_string(index));
    try { tooMany.receiveHello(oversized); return 17; }
    catch (const std::invalid_argument &) {}
    if (tooMany.receiveHello(ack)!=ack) return 18;

    State duplicate;
    duplicate.markPaired(ack);
    duplicate.startLocalHello();
    duplicate.receiveHello(ack);
    try { duplicate.receiveHello(ack); return 15; }
    catch (const std::logic_error &) {}

    std::cout << "ControlNegotiationState: handshakes, intersections, duplicates and capability bounds passed\n";
    return 0;
}
