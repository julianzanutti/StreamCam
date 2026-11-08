#include "AvTimestampMapper.h"
#include "AvSessionClock.h"

#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    using Mapper = streamcam::AvTimestampMapper;
    Mapper mapper;
    std::uint64_t audio0 = 0, video0 = 0, audio1 = 0, video1 = 0;

    // Audio arrives first. A delayed video frame captured 300 ms earlier must
    // retain that offset instead of being rebased to its later arrival time.
    if (!mapper.map(Mapper::Stream::Audio, 10'000'000, 5'000'000'000ULL, audio0)) return 1;
    if (!mapper.map(Mapper::Stream::Video, 9'700'000, 5'450'000'000ULL, video0)) return 2;
    if (audio0 - video0 != 300'000'000ULL) return 3;

    if (mapper.map(Mapper::Stream::Audio, 10'000'000, 5'500'000'000ULL, audio1)) return 4;
    if (mapper.map(Mapper::Stream::Video, 9'700'000, 5'500'000'000ULL, video1)) return 5;
    if (!mapper.map(Mapper::Stream::Audio, 10'020'000, 5'520'000'000ULL, audio1)) return 6;
    if (!mapper.map(Mapper::Stream::Video, 9'733'333, 5'553'333'000ULL, video1)) return 7;
    if (audio1 - audio0 != 20'000'000ULL || video1 - video0 != 33'333'000ULL) return 8;

    // One stream's out-of-order packet is rejected without shifting the other.
    if (mapper.map(Mapper::Stream::Audio, 10'010'000, 5'560'000'000ULL, audio1)) return 9;
    std::uint64_t afterLateAudio = 0;
    if (!mapper.map(Mapper::Stream::Video, 9'766'666, 5'586'666'000ULL, afterLateAudio)) return 10;
    if (afterLateAudio - video1 != 33'333'000ULL) return 11;

    // Long sessions retain a shared clock without per-track re-anchoring.
    std::uint64_t longAudio = 0, longVideo = 0;
    if (!mapper.map(Mapper::Stream::Audio, 10'000'000 + 8ULL * 60 * 60 * 1'000'000,
                    34'200'000'000'000ULL, longAudio)) return 12;
    if (!mapper.map(Mapper::Stream::Video, 9'700'000 + 8ULL * 60 * 60 * 1'000'000,
                    34'200'033'333'000ULL, longVideo)) return 13;
    if (longAudio - longVideo != 300'000'000ULL) return 14;

    std::uint64_t overflow = 0;
    Mapper edge;
    if (!edge.map(Mapper::Stream::Audio, 1, std::numeric_limits<std::uint64_t>::max() - 1000, overflow)) return 15;
    std::uint64_t ignored = 0;
    if (edge.map(Mapper::Stream::Video, 3, std::numeric_limits<std::uint64_t>::max() - 1000, ignored)) return 16;

    mapper.reset();
    if (!mapper.map(Mapper::Stream::Video, 1, 1000, ignored)) return 17;

    streamcam::AvSessionClock sessions;
    using Stream = streamcam::AvSessionClock::Stream;
    std::uint64_t oldAudio = 0, oldVideo = 0, newAudio = 0, newVideo = 0;
    if (sessions.map(Stream::Audio, 50'000'000, 60'000'000'000ULL, ignored)) return 18;
    sessions.setConnected(Stream::Audio, true);
    if (sessions.map(Stream::Audio, 50'000'000, 60'000'000'000ULL, ignored)) return 19;
    sessions.setConnected(Stream::Video, true);
    if (!sessions.map(Stream::Audio, 50'000'000, 60'000'000'000ULL, oldAudio)) return 20;
    if (!sessions.map(Stream::Video, 49'700'000, 60'300'000'000ULL, oldVideo)) return 21;
    if (oldAudio - oldVideo != 300'000'000ULL) return 22;
    sessions.setConnected(Stream::Audio, false);
    if (sessions.map(Stream::Video, 49'733'333, 60'333'333'000ULL, ignored)) return 23;
    sessions.setConnected(Stream::Audio, true);
    // A new Android device/boot can restart its monotonic timestamps at a
    // smaller value. The synchronized reconnect must accept both tracks and
    // retain their captured 300 ms offset on the new, monotonic OBS epoch.
    if (!sessions.map(Stream::Audio, 8'000'000, 60'500'000'000ULL, newAudio)) return 24;
    if (!sessions.map(Stream::Video, 7'700'000, 60'800'000'000ULL, newVideo)) return 25;
    if (newAudio <= oldAudio || newVideo <= oldVideo) return 26;
    if (newAudio - newVideo != 300'000'000ULL) {
        std::cerr << "Reconnect A/V offset changed to " << (newAudio-newVideo) << " ns.\n";
        return 27;
    }

    std::cout << "AvTimestampMapper: shared A/V offset, long session, overflow and reconnect epoch passed\n";
    return 0;
}
