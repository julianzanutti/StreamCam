#pragma once
#include "AudioFrame.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace streamcam {
struct VoiceSettings final {
    float inputGainDb = 0.0F;
    float outputGainDb = 0.0F;
    bool muted = false;
    bool compressor = false;
    float thresholdDb = -18.0F;
    float ratio = 3.0F;
    bool noiseGate = false;
    float gateDb = -48.0F;
    bool echoEffect = false;
    float echoDelayMs = 180.0F;
    float echoFeedback = 0.30F;
    float echoMix = 0.25F;
};

// Lightweight receive-side dynamics stage shared by Wi-Fi and USB before OBS IPC.
// Echo cancellation remains at capture time because it needs the far-end playback reference.
class VoiceProcessor final {
public:
    void setSettings(const VoiceSettings &settings) {
        if(!settings_.muted && settings.muted) reset();
        const bool delayChanged = settings_.echoDelayMs != std::clamp(settings.echoDelayMs,30.0F,500.0F);
        settings_.muted=settings.muted;
        settings_.inputGainDb=std::clamp(settings.inputGainDb,-18.0F,18.0F);
        settings_.outputGainDb=std::clamp(settings.outputGainDb,-18.0F,12.0F);
        settings_.thresholdDb=std::clamp(settings.thresholdDb,-36.0F,-6.0F);
        settings_.ratio=std::clamp(settings.ratio,1.0F,10.0F);
        settings_.gateDb=std::clamp(settings.gateDb,-72.0F,-24.0F);
        settings_.echoDelayMs=std::clamp(settings.echoDelayMs,30.0F,500.0F);
        settings_.echoFeedback=std::clamp(settings.echoFeedback,0.0F,0.70F);
        settings_.echoMix=std::clamp(settings.echoMix,0.0F,0.75F);
        settings_.compressor=settings.compressor;
        settings_.noiseGate=settings.noiseGate;
        if(settings_.echoEffect!=settings.echoEffect || delayChanged) {
            std::fill(delayBuffer_.begin(),delayBuffer_.end(),std::array<float,2>{});
            delayWriteIndex_=0;
        }
        settings_.echoEffect=settings.echoEffect;
        inputGain_=std::pow(10.0F,settings_.inputGainDb/20.0F);
        outputGain_=std::pow(10.0F,settings_.outputGainDb/20.0F);
        attack_=std::exp(-1.0F/(48000.0F*0.010F));
        release_=std::exp(-1.0F/(48000.0F*0.120F));
        gateAttack_=std::exp(-1.0F/(48000.0F*0.005F));
        gateRelease_=std::exp(-1.0F/(48000.0F*0.060F));
    }
    const VoiceSettings &settings() const { return settings_; }

    void reset() {
        envelope_=0.0F;compressionGainDb_=0.0F;gateGain_=1.0F;delayWriteIndex_=0;
        std::fill(delayBuffer_.begin(),delayBuffer_.end(),std::array<float,2>{});
    }

    bool process(AudioFrame &frame) {
        if (!frame.valid()) return false;
        if (settings_.muted) {
            std::fill(frame.pcm.begin(),frame.pcm.end(),0);
            std::fill(frame.samplesFloat.begin(),frame.samplesFloat.end(),0.0F);
            return true;
        }
        const auto thresholdDb=settings_.thresholdDb;
        const auto gateDb=settings_.gateDb;
        const auto inverseRatio=1.0F/settings_.ratio;
        for(std::size_t frameIndex=0;frameIndex<frame.frames;++frameIndex) {
            float peak=0.0F;
            for(std::size_t channel=0;channel<frame.channels;++channel) {
                const auto i=frameIndex*frame.channels+channel;
                const auto input=frame.floatingPoint ? frame.samplesFloat[i] : static_cast<float>(frame.pcm[i])/32768.0F;
                if(std::isfinite(input))peak=std::max(peak,std::abs(input*inputGain_));
            }
            const auto envelopeCoefficient=peak>envelope_ ? attack_ : release_;
            envelope_=envelopeCoefficient*envelope_+(1.0F-envelopeCoefficient)*peak;
            const auto levelDb=envelope_>1.0e-6F ? 20.0F*std::log10(envelope_) : -120.0F;
            auto targetCompressionDb=0.0F;
            if(settings_.compressor && levelDb>thresholdDb)
                targetCompressionDb=(thresholdDb+(levelDb-thresholdDb)*inverseRatio)-levelDb;
            const auto compressionCoefficient=targetCompressionDb<compressionGainDb_ ? attack_ : release_;
            compressionGainDb_=compressionCoefficient*compressionGainDb_+(1.0F-compressionCoefficient)*targetCompressionDb;
            const auto targetGateGain=settings_.noiseGate && levelDb<gateDb ? 0.0F : 1.0F;
            const auto gateCoefficient=targetGateGain<gateGain_ ? gateRelease_ : gateAttack_;
            gateGain_=gateCoefficient*gateGain_+(1.0F-gateCoefficient)*targetGateGain;
            const auto dynamicsGain=inputGain_*std::pow(10.0F,compressionGainDb_/20.0F)*gateGain_;
            const auto delayFrames=static_cast<std::size_t>(std::clamp(
                std::lround(settings_.echoDelayMs*48000.0F/1000.0F),1L,24000L));
            const auto delayedIndex=(delayWriteIndex_+DelayBufferFrames-delayFrames)%DelayBufferFrames;
            std::array<float,2> dry{};
            for(std::size_t channel=0;channel<frame.channels;++channel) {
                const auto i=frameIndex*frame.channels+channel;
                const auto input=frame.floatingPoint ? frame.samplesFloat[i] : static_cast<float>(frame.pcm[i])/32768.0F;
                dry[channel]=std::clamp((std::isfinite(input)?input:0.0F)*dynamicsGain,-1.0F,1.0F);
            }
            std::array<float,2> wet{};
            if(settings_.echoEffect) {
                for(std::size_t channel=0;channel<frame.channels;++channel) {
                    wet[channel]=delayBuffer_[delayedIndex][channel];
                    delayBuffer_[delayWriteIndex_][channel]=std::clamp(
                        dry[channel]+wet[channel]*settings_.echoFeedback,-1.0F,1.0F);
                }
                // Advance once per audio frame so both stereo channels retain separate delay lines.
                delayWriteIndex_=(delayWriteIndex_+1)%DelayBufferFrames;
            }
            for(std::size_t channel=0;channel<frame.channels;++channel) {
                const auto i=frameIndex*frame.channels+channel;
                const auto effected=settings_.echoEffect
                    ? dry[channel]*(1.0F-settings_.echoMix)+wet[channel]*settings_.echoMix
                    : dry[channel];
                const auto sample=std::clamp(effected*outputGain_,-1.0F,1.0F);
                if(frame.floatingPoint) frame.samplesFloat[i]=sample;
                else frame.pcm[i]=static_cast<std::int16_t>(std::lround(sample*32767.0F));
            }
        }
        return true;
    }
private:
    static constexpr std::size_t DelayBufferFrames=24001;
    VoiceSettings settings_{};
    float inputGain_=1.0F;
    float outputGain_=1.0F;
    float envelope_=0.0F;
    float compressionGainDb_=0.0F;
    float gateGain_=1.0F;
    float attack_=0.0F,release_=0.0F,gateAttack_=0.0F,gateRelease_=0.0F;
    std::array<std::array<float,2>,DelayBufferFrames> delayBuffer_{};
    std::size_t delayWriteIndex_=0;
};
}
