#include "AudioEffectsAckTracker.h"

#include <iostream>

int main() {
    using Tracker = streamcam::AudioEffectsAckTracker;
    Tracker tracker;
    if (!tracker.begin("request-1") || tracker.begin("request-2")) return 1;
    if (tracker.timeout("unknown") != Tracker::TimeoutAction::Stale) return 2;
    if (tracker.timeout("request-1") != Tracker::TimeoutAction::Retry || !tracker.hasPending()) return 3;
    if (tracker.acknowledge("request-1") != Tracker::AckAction::Accepted || tracker.hasPending()) return 4;
    if (tracker.acknowledge("request-1") != Tracker::AckAction::Duplicate) return 5;
    if (tracker.acknowledge("foreign") != Tracker::AckAction::Unexpected) return 6;

    if (!tracker.begin("request-2")) return 7;
    if (tracker.timeout("request-2") != Tracker::TimeoutAction::Retry) return 8;
    if (tracker.timeout("request-2") != Tracker::TimeoutAction::Expired || tracker.hasPending()) return 9;
    if (tracker.acknowledge("request-2") != Tracker::AckAction::Duplicate) return 10;

    if (!tracker.begin("request-3")) return 11;
    if (!tracker.abandon("request-3") || tracker.hasPending() ||
        tracker.acknowledge("request-3") != Tracker::AckAction::Duplicate) return 12;
    if (!tracker.begin("request-4")) return 13;
    tracker.reset();
    if (tracker.hasPending() || tracker.acknowledge("request-4") != Tracker::AckAction::Unexpected) return 14;
    std::cout << "AudioEffectsAckTracker: one retry, duplicate/stale/foreign ACK and reset passed\n";
    return 0;
}
