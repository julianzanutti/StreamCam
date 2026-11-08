#include "../media/VoiceProcessor.h"
#include <cmath>
#include <cstdio>

namespace {
streamcam::AudioFrame tone(float amplitude, std::uint32_t frames = 960) {
    streamcam::AudioFrame frame;
    frame.sampleRate = 48000;
    frame.channels = 1;
    frame.frames = frames;
    frame.floatingPoint = true;
    frame.samplesFloat.assign(frames, amplitude);
    return frame;
}
bool near(float actual, float expected, float tolerance) {
    return std::abs(actual - expected) <= tolerance;
}
streamcam::AudioFrame stereo(std::uint32_t frames=480) {
    streamcam::AudioFrame frame;
    frame.sampleRate=48000;frame.channels=2;frame.frames=frames;frame.floatingPoint=true;
    frame.samplesFloat.assign(static_cast<std::size_t>(frames)*2,0.0F);
    return frame;
}
}

int main() {
    using namespace streamcam;
    VoiceProcessor processor;
    VoiceSettings settings;
    settings.inputGainDb = 6.0F;
    processor.setSettings(settings);
    auto gained = tone(0.1F, 480);
    if (!processor.process(gained) || !near(gained.samplesFloat.front(), 0.199526F, 0.001F)) {
        std::fprintf(stderr, "input gain output: %.8f\n", gained.samplesFloat.front());
        return 1;
    }

    settings = VoiceSettings{};
    settings.compressor = true;
    settings.thresholdDb = -18.0F;
    settings.ratio = 4.0F;
    processor.setSettings(settings);
    if (!processor.settings().compressor || processor.settings().ratio != 4.0F) return 4;
    auto compressed = tone(0.8F);
    for (int i = 0; i < 50; ++i) {
        compressed = tone(0.8F);
        if (!processor.process(compressed)) return 2;
    }
    if (compressed.samplesFloat.back() >= 0.4F || compressed.samplesFloat.back() < 0.15F) {
        std::fprintf(stderr, "compressor output: %.8f\n", compressed.samplesFloat.back());
        return 2;
    }

    settings = VoiceSettings{};
    settings.noiseGate = true;
    settings.gateDb = -48.0F;
    processor.setSettings(settings);
    auto gated = tone(0.0001F);
    for (int i = 0; i < 100; ++i) {
        gated = tone(0.0001F);
        if (!processor.process(gated)) return 3;
    }
    if (std::abs(gated.samplesFloat.back()) >= 1.0e-5F) {
        std::fprintf(stderr, "gate output: %.8f\n", gated.samplesFloat.back());
        return 3;
    }

    VoiceProcessor echoProcessor;
    settings = VoiceSettings{};
    settings.echoEffect = true;
    settings.echoDelayMs = 10.0F;
    settings.echoFeedback = 0.5F;
    settings.echoMix = 0.5F;
    echoProcessor.setSettings(settings);
    if (echoProcessor.settings().echoFeedback != 0.5F || echoProcessor.settings().echoMix != 0.5F) return 5;
    auto impulse = tone(0.0F, 960);
    impulse.samplesFloat[0] = 0.6F;
    if (!echoProcessor.process(impulse) || !near(impulse.samplesFloat.front(),0.3F,0.01F)) return 6;
    auto firstRepeatAtDelay = tone(0.0F, 960);
    if (!echoProcessor.process(firstRepeatAtDelay) || !near(firstRepeatAtDelay.samplesFloat[479],0.0F,0.01F) ||
        !near(firstRepeatAtDelay.samplesFloat[480],0.3F,0.01F)) {
        std::fprintf(stderr,"first echo repeat around delay: %.8f / %.8f\n",firstRepeatAtDelay.samplesFloat[479],firstRepeatAtDelay.samplesFloat[480]);
        return 7;
    }
    // Independent stereo impulses must not leak between left and right delay lines.
    VoiceProcessor stereoEcho;
    settings=VoiceSettings{};settings.echoEffect=true;settings.echoDelayMs=10.0F;
    settings.echoFeedback=0.0F;settings.echoMix=0.5F;
    stereoEcho.setSettings(settings);
    auto stereoImpulse=stereo(960);stereoImpulse.samplesFloat[0]=0.6F;stereoImpulse.samplesFloat[1]=-0.4F;
    if(!stereoEcho.process(stereoImpulse) || !near(stereoImpulse.samplesFloat[0],0.3F,0.01F) ||
       !near(stereoImpulse.samplesFloat[1],-0.2F,0.01F))return 13;
    auto stereoRepeat=stereo(960);
    if(!stereoEcho.process(stereoRepeat) || !near(stereoRepeat.samplesFloat[958],0.0F,0.01F) ||
       !near(stereoRepeat.samplesFloat[959],0.0F,0.01F) || !near(stereoRepeat.samplesFloat[960],0.3F,0.01F) ||
       !near(stereoRepeat.samplesFloat[961],-0.2F,0.01F)) {
        std::fprintf(stderr,"stereo echo around delay: %.5f / %.5f then %.5f / %.5f\n",
            stereoRepeat.samplesFloat[958],stereoRepeat.samplesFloat[959],stereoRepeat.samplesFloat[960],stereoRepeat.samplesFloat[961]);
        return 17;
    }

    settings = VoiceSettings{};
    settings.muted=true;
    settings.echoEffect=true;
    settings.echoDelayMs=30.0F;
    echoProcessor.setSettings(settings);
    auto muted=tone(0.5F);
    if(!echoProcessor.process(muted))return 9;
    for(const auto sample:muted.samplesFloat)if(sample!=0.0F)return 10;
    settings.muted=false;echoProcessor.setSettings(settings);
    auto afterMute=tone(0.0F);
    if(!echoProcessor.process(afterMute))return 11;
    for(const auto sample:afterMute.samplesFloat)if(std::abs(sample)>1.0e-6F)return 12;

    std::puts("VoiceProcessor: input gain, compressor, gate, bounded echo and mute passed");
    return 0;
}
