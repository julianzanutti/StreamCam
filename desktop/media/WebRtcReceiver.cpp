#include "WebRtcReceiver.h"
#include "MfH264Decoder.h"
#include "UsbAudioPacket.h"
#include "UsbH264DecodeQueue.h"
#include "AudioSignalMonitor.h"
#include <api/create_peerconnection_factory.h>
#include <api/make_ref_counted.h>
#include <api/set_local_description_observer_interface.h>
#include <api/set_remote_description_observer_interface.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <api/audio_codecs/audio_encoder_factory_template.h>
#include <api/audio_codecs/audio_decoder_factory_template.h>
#include <api/audio_codecs/opus/audio_encoder_opus.h>
#include <api/audio_codecs/opus/audio_decoder_opus.h>
#include <api/video/video_sink_interface.h>
#include <api/video/video_frame.h>
#include <api/media_stream_interface.h>
#include <chrono>
#include <rtc_base/ssl_adapter.h>
#include <rtc_base/thread.h>
#include <rtc_base/win32_socket_init.h>
#include <rtc_base/network.h>
#include <p2p/base/basic_packet_socket_factory.h>
#include <api/environment/environment_factory.h>
#include <p2p/client/basic_port_allocator.h>
#include <libyuv.h>
#include <QMetaObject>
#include <QtEndian>
#include <functional>
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <cstdio>
#include <limits>
#include <thread>

namespace streamcam {
namespace {
class H264EncoderFactory final : public webrtc::VideoEncoderFactory {
public:
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        return webrtc::SupportedH264Codecs();
    }
    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment &environment, const webrtc::SdpVideoFormat &format) override {
        return webrtc::CreateH264Encoder(environment, webrtc::H264EncoderSettings::Parse(format));
    }
};
class SelectedNetworkManager final : public webrtc::BasicNetworkManager {
    std::string address_;
public:
    SelectedNetworkManager(const webrtc::Environment &env, webrtc::SocketFactory *sockets, std::string address)
        : BasicNetworkManager(env, sockets), address_(std::move(address)) {}
    std::vector<const webrtc::Network *> GetNetworks() const override {
        auto networks = BasicNetworkManager::GetNetworks();
        const auto before=networks.size();
        std::erase_if(networks, [this](const webrtc::Network *network) { return network->GetBestIP().ToString() != address_; });
        std::fprintf(stderr,"Selected network count: enumerated=%zu retained=%zu\n",before,networks.size()); std::fflush(stderr);
        return networks;
    }
    bool GetDefaultLocalAddress(int, webrtc::IPAddress *) const override { return false; }
};
class H264SoftwareFactory final : public webrtc::VideoDecoderFactory {
public:
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        return {
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "42e01f"}, {"packetization-mode", "1"}, {"level-asymmetry-allowed", "1"}}),
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "42001f"}, {"packetization-mode", "1"}, {"level-asymmetry-allowed", "1"}})
        };
    }
    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment &, const webrtc::SdpVideoFormat &format) override {
        if (format.name != "H264") return nullptr;
        return std::make_unique<MfH264Decoder>();
    }
};
struct RemoteObserver : webrtc::SetRemoteDescriptionObserverInterface {
    std::function<void(webrtc::RTCError)> done;
    explicit RemoteObserver(decltype(done) callback) : done(std::move(callback)) {}
    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override { done(std::move(error)); }
};
struct LocalObserver : webrtc::SetLocalDescriptionObserverInterface {
    std::function<void(webrtc::RTCError)> done;
    explicit LocalObserver(decltype(done) callback) : done(std::move(callback)) {}
    void OnSetLocalDescriptionComplete(webrtc::RTCError error) override { done(std::move(error)); }
};
struct AnswerObserver : webrtc::CreateSessionDescriptionObserver {
    std::function<void(webrtc::SessionDescriptionInterface*)> done;
    std::function<void()> failed;
    AnswerObserver(decltype(done) callback, decltype(failed) failure) : done(std::move(callback)), failed(std::move(failure)) {}
    void OnSuccess(webrtc::SessionDescriptionInterface *description) override { done(description); }
    void OnFailure(webrtc::RTCError) override { failed(); }
};
}

class WebRtcReceiver::Impl : public webrtc::PeerConnectionObserver, public webrtc::VideoSinkInterface<webrtc::VideoFrame>, public webrtc::AudioTrackSinkInterface, public webrtc::DecodedImageCallback {
public:
    WebRtcReceiver *owner;
    std::shared_ptr<std::atomic_bool> alive=std::make_shared<std::atomic_bool>(true);
    webrtc::WinsockInitializer winsock;
    std::shared_ptr<LatestFrame> frames=std::make_shared<LatestFrame>();
    std::shared_ptr<LatestAudio> audioFrames=std::make_shared<LatestAudio>();
    AudioSignalMonitor audioSignal;
    std::atomic<std::uint64_t> receivedCount{0};
    std::unique_ptr<webrtc::Thread> network;
    std::unique_ptr<webrtc::Thread> signaling;
    std::unique_ptr<webrtc::Thread> worker;
    webrtc::Environment environment = webrtc::CreateEnvironment();
    std::unique_ptr<SelectedNetworkManager> networkManager;
    std::unique_ptr<webrtc::BasicPacketSocketFactory> socketFactory;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> audioTrack;
    UsbH264DecodeQueue usbDecodeQueue;
    std::thread usbDecodeThread;
    bool remoteReady = false;
    std::vector<std::unique_ptr<webrtc::IceCandidate>> pendingCandidates;
    explicit Impl(WebRtcReceiver *parent, const QString &localAddress) : owner(parent) {
        std::puts("receiver: ssl"); std::fflush(stdout);
        webrtc::InitializeSSL();
        std::puts("receiver: threads"); std::fflush(stdout);
        network = webrtc::Thread::CreateWithSocketServer();
        signaling = webrtc::Thread::Create();
        worker = webrtc::Thread::Create();
        network->Start(); signaling->Start(); worker->Start();
        std::puts("receiver: factory"); std::fflush(stdout);
        std::fprintf(stderr, "H264 formats: send=%zu receive=%zu supported=%d\n", webrtc::SupportedH264Codecs().size(),
            webrtc::SupportedH264DecoderCodecs().size(), webrtc::H264Decoder::IsSupported()); std::fflush(stderr);
        factory = webrtc::CreatePeerConnectionFactory(network.get(), worker.get(), signaling.get(), nullptr,
            webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
            webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
            std::make_unique<H264EncoderFactory>(), std::make_unique<H264SoftwareFactory>(), nullptr, nullptr);
        if (!factory) throw std::runtime_error("Cannot initialize native WebRTC");
        std::puts("receiver: peer configuration"); std::fflush(stdout);
        webrtc::PeerConnectionInterface::RTCConfiguration config;
        config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        config.type = webrtc::PeerConnectionInterface::kAll;
        config.tcp_candidate_policy = webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled;
        webrtc::PeerConnectionDependencies dependencies(this);
        if (!localAddress.isEmpty()) {
            network->BlockingCall([&] {
                networkManager = std::make_unique<SelectedNetworkManager>(environment, network->socketserver(), localAddress.toStdString());
                socketFactory = std::make_unique<webrtc::BasicPacketSocketFactory>(network->socketserver());
                dependencies.allocator = std::make_unique<webrtc::BasicPortAllocator>(environment, networkManager.get(), socketFactory.get());
            });
        }
        auto result = factory->CreatePeerConnectionOrError(config, std::move(dependencies));
        std::puts("receiver: peer created"); std::fflush(stdout);
        if (!result.ok()) throw std::runtime_error("Cannot create receiver peer");
        peer = result.MoveValue();
        usbDecodeThread=std::thread([this] { decodeUsbLoop(); });
    }
    ~Impl() {
        alive->store(false);
        usbDecodeQueue.close();
        if(usbDecodeThread.joinable()) usbDecodeThread.join();
        if (track) track->RemoveSink(this);
        if (audioTrack) audioTrack->RemoveSink(this);
        if (peer) peer->Close();
        track = nullptr; audioTrack = nullptr; peer = nullptr; factory = nullptr;
        network->BlockingCall([&] { networkManager.reset(); socketFactory.reset(); });
        signaling->Stop(); worker->Stop(); network->Stop(); frames->clear(); audioFrames->clear();
    }
    void notify(const QString &text) {
        QMetaObject::invokeMethod(owner, [parent = owner, text, valid=alive] { if(valid->load())emit parent->statusChanged(text); }, Qt::QueuedConnection);
    }
    void send(QJsonObject message) {
        QMetaObject::invokeMethod(owner, [parent = owner, message, valid=alive] { if(valid->load())emit parent->signalReady(message); }, Qt::QueuedConnection);
    }
    void receive(const QJsonObject &message) {
        const auto type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("rtc.offer")) {
            auto description = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer,
                message.value(QStringLiteral("sdp")).toString().toStdString());
            if (!description) throw std::runtime_error("Invalid remote SDP");
            peer->SetRemoteDescription(std::move(description), webrtc::make_ref_counted<RemoteObserver>([this](webrtc::RTCError error) {
                notify(QStringLiteral("Oferta remota aplicada: %1").arg(error.ok()));
                if (!error.ok()) { notify(QStringLiteral("Oferta de video rechazada")); return; }
                remoteReady = true;
                for (auto &candidate : pendingCandidates) peer->AddIceCandidate(std::move(candidate), [](webrtc::RTCError) {});
                pendingCandidates.clear();
                peer->CreateAnswer(webrtc::make_ref_counted<AnswerObserver>([this](webrtc::SessionDescriptionInterface *raw) {
                    std::unique_ptr<webrtc::SessionDescriptionInterface> answer(raw);
                    const auto sdp = QString::fromStdString(answer->ToString());
                    peer->SetLocalDescription(std::move(answer), webrtc::make_ref_counted<LocalObserver>([this, sdp](webrtc::RTCError result) {
                        if (result.ok()) send({{QStringLiteral("type"), QStringLiteral("rtc.answer")}, {QStringLiteral("sdp"), sdp}});
                        else notify(QStringLiteral("No se pudo negociar video"));
                    }));
                }, [this] { notify(QStringLiteral("No se pudo crear respuesta de video")); }).get(), {});
            }));
        } else if (type == QStringLiteral("rtc.ice")) {
            auto candidate = webrtc::IceCandidate::Create(message.value(QStringLiteral("mid")).toString().toStdString(),
                message.value(QStringLiteral("mline")).toInt(), message.value(QStringLiteral("candidate")).toString().toStdString());
            if (!candidate) throw std::runtime_error("Invalid ICE candidate");
            if (remoteReady) peer->AddIceCandidate(std::move(candidate), [](webrtc::RTCError) {});
            else {
                if (pendingCandidates.size() >= 64) throw std::runtime_error("Too many pending candidates");
                pendingCandidates.push_back(std::move(candidate));
            }
        }
    }
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override {
        if(state==webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected ||
            state==webrtc::PeerConnectionInterface::PeerConnectionState::kFailed ||
            state==webrtc::PeerConnectionInterface::PeerConnectionState::kClosed) { frames->clear(); audioFrames->clear(); }
        switch(state) {
        case webrtc::PeerConnectionInterface::PeerConnectionState::kNew: notify(QStringLiteral("Preparando video")); break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting: notify(QStringLiteral("Conectando video…")); break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected: notify(QStringLiteral("Teléfono conectado")); break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected: notify(QStringLiteral("Conexión interrumpida")); break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed: notify(QStringLiteral("No se pudo conectar el video. Volvé a vincular el teléfono.")); break;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed: notify(QStringLiteral("Sesión cerrada")); break;
        }
    }
    void OnIceCandidate(const webrtc::IceCandidate *candidate) override {
        send({{QStringLiteral("type"), QStringLiteral("rtc.ice")},
            {QStringLiteral("mid"), QString::fromStdString(candidate->sdp_mid())},
            {QStringLiteral("mline"), candidate->sdp_mline_index()},
            {QStringLiteral("candidate"), QString::fromStdString(candidate->ToString())}});
    }
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {
        auto remote = transceiver->receiver()->track();
        if (remote && remote->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
            track = static_cast<webrtc::VideoTrackInterface *>(remote.get());
            track->AddOrUpdateSink(this, webrtc::VideoSinkWants());
        } else if(remote && remote->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
            audioTrack=static_cast<webrtc::AudioTrackInterface *>(remote.get());
            audioSignal.reset();
            audioTrack->AddSink(this);
        }
    }
    void OnData(const void *data,int bits,int rate,size_t channels,size_t frames,std::optional<int64_t> timestampMs) override {
        if(!data || bits!=16 || rate!=48000 || (channels!=1 && channels!=2) || frames==0 || frames>960)return;
        audioSignal.observe(steadyNowNs());
        auto frame=std::make_shared<AudioFrame>(); frame->sampleRate=rate; frame->channels=static_cast<uint32_t>(channels);
        frame->frames=static_cast<uint32_t>(frames);
        if(timestampMs && *timestampMs>=0 && *timestampMs<=std::numeric_limits<std::int64_t>::max()/1000)
            frame->timestampUs=static_cast<std::uint64_t>(*timestampMs)*1000ULL;
        else frame->timestampUs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        const auto *samples=static_cast<const int16_t *>(data);
        frame->pcm.assign(samples,samples+frames*channels);
        audioFrames->publish(std::move(frame));
    }
    int32_t Decoded(webrtc::VideoFrame &frame) override { OnFrame(frame); return 0; }
    void decodeUsbLoop() {
        std::unique_ptr<MfH264Decoder> decoder;
        int decoderWidth=0, decoderHeight=0;
        while(auto packet=usbDecodeQueue.waitPop()) {
            if(!decoder || decoderWidth!=packet->width || decoderHeight!=packet->height) {
                decoder.reset();
                auto next=std::make_unique<MfH264Decoder>();
                webrtc::VideoDecoder::Settings settings;
                settings.set_max_render_resolution({packet->width,packet->height});
                if(!next->Configure(settings)) {
                    notify(QStringLiteral("No se pudo iniciar el decoder H.264 USB para %1 × %2").arg(packet->width).arg(packet->height));
                    usbDecodeQueue.requireKeyframe();
                    continue;
                }
                next->RegisterDecodeCompleteCallback(this);
                decoderWidth=packet->width; decoderHeight=packet->height; decoder=std::move(next);
            }
            webrtc::EncodedImage image;
            image.SetEncodedData(webrtc::EncodedImageBuffer::Create(packet->bytes.data(),packet->bytes.size()));
            image.SetRtpTimestamp(packet->rtpTimestamp);
            image.capture_time_ms_ = static_cast<std::int64_t>(std::min<std::uint64_t>(
                packet->timestampUs / 1000, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
            if (packet->timestampUs <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                image.SetPresentationTimestamp(webrtc::Timestamp::Micros(static_cast<std::int64_t>(packet->timestampUs)));
            if(decoder->Decode(image,false,0)<0) {
                decoder.reset(); decoderWidth=0; decoderHeight=0;
                usbDecodeQueue.requireKeyframe();
                notify(QStringLiteral("Se resincroniza el decoder H.264 USB en el próximo keyframe"));
            }
        }
        decoder.reset();
    }
    void receiveUsb(const QByteArray &packet) {
        const auto *raw = packet.constData();
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(raw);
        if (packet.size() >= 4 && qFromLittleEndian<quint32>(bytes) == 0x31414353) {
            const auto decoded = parseUsbAudioPacket(raw, static_cast<std::size_t>(packet.size()));
            if (!decoded) throw std::runtime_error("Invalid USB audio packet");
            // Keep the same signal watchdog for USB PCM and WebRTC audio packets.
            audioSignal.observe(steadyNowNs());
            auto audio=std::make_shared<AudioFrame>(); audio->sampleRate=decoded->sampleRate; audio->channels=decoded->channels;
            audio->frames=decoded->frames; audio->timestampUs=decoded->timestampUs;
            audio->pcm.resize(static_cast<size_t>(decoded->frames)*decoded->channels);
            memcpy(audio->pcm.data(), decoded->pcm, decoded->pcmBytes); audioFrames->publish(std::move(audio)); return;
        }
        if(packet.size()<21 || packet.size()>2*1024*1024+20)throw std::runtime_error("Invalid USB frame");
        const auto *data=reinterpret_cast<const uchar *>(packet.constData());
        const auto width=qFromLittleEndian<quint32>(data+4), height=qFromLittleEndian<quint32>(data+8);
        if(qFromLittleEndian<quint32>(data)!=0x31484353 || width<16 || width>1920 || height<16 || height>1080)
            throw std::runtime_error("Invalid USB format");
        UsbEncodedVideo frame;
        frame.width=static_cast<int>(width); frame.height=static_cast<int>(height);
        const auto timestamp=qFromLittleEndian<quint64>(data+12);
        frame.timestampUs=timestamp;
        frame.rtpTimestamp=static_cast<std::uint32_t>((timestamp/1000)*90);
        frame.bytes.assign(data+20,data+packet.size());
        usbDecodeQueue.push(std::move(frame));
    }
    static std::uint64_t steadyNowNs() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void OnFrame(const webrtc::VideoFrame &incoming) override {
        const auto buffer = incoming.video_frame_buffer()->ToI420();
        if (!buffer || buffer->width() > 1920 || buffer->height() > 1080) return;
        auto frame = std::make_shared<VideoFrame>();
        frame->width = buffer->width(); frame->height = buffer->height(); frame->stride = frame->width * 4;
        const auto captureTimestampUs=incoming.timestamp_us();
        if(captureTimestampUs<0)return;
        frame->timestampUs=static_cast<std::uint64_t>(captureTimestampUs);
        frame->bgra.resize(frame->stride * frame->height);
        libyuv::I420ToARGB(buffer->DataY(), buffer->StrideY(), buffer->DataU(), buffer->StrideU(),
            buffer->DataV(), buffer->StrideV(), frame->bgra.data(), frame->stride, frame->width, frame->height);
        frames->publish(std::move(frame));
        ++receivedCount;
    }
};
WebRtcReceiver::WebRtcReceiver(QObject *parent, const QString &localAddress) : QObject(parent), impl_(std::make_unique<Impl>(this, localAddress)) {}
WebRtcReceiver::~WebRtcReceiver() = default;
void WebRtcReceiver::receive(const QJsonObject &message) {
    if (impl_) impl_->signaling->BlockingCall([&] { impl_->receive(message); });
}
void WebRtcReceiver::receiveUsb(const QByteArray &packet) { if(impl_)impl_->receiveUsb(packet); }
void WebRtcReceiver::reset(const QString &address) { impl_.reset(); impl_=std::make_unique<Impl>(this,address); }
void WebRtcReceiver::stop() { impl_.reset(); }
std::shared_ptr<const VideoFrame> WebRtcReceiver::latestFrame() const { return impl_ ? impl_->frames->read() : nullptr; }
std::shared_ptr<LatestFrame> WebRtcReceiver::frameSource() const { return impl_ ? impl_->frames : nullptr; }
std::shared_ptr<LatestAudio> WebRtcReceiver::audioSource() const { return impl_ ? impl_->audioFrames : nullptr; }
std::vector<std::shared_ptr<const AudioFrame>> WebRtcReceiver::drainAudio(std::size_t maximum) { return impl_ ? impl_->audioFrames->drain(maximum) : std::vector<std::shared_ptr<const AudioFrame>>{}; }
std::uint64_t WebRtcReceiver::droppedAudioFrames() const { return impl_ ? impl_->audioFrames->dropped() : 0; }
std::size_t WebRtcReceiver::queuedAudioFrames() const { return impl_ ? impl_->audioFrames->queuedFrames() : 0; }
bool WebRtcReceiver::audioUnderflow() const {
    if (!impl_) return false;
    const auto nowNs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    return impl_->audioSignal.underflow(nowNs);
}
std::uint64_t WebRtcReceiver::receivedFrameCount() const { return impl_ ? impl_->receivedCount.load() : 0; }
}
