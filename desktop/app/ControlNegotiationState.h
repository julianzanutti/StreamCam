#pragma once

#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace streamcam {

// Tracks both halves of the optional post-pairing control-v2 handshake.
// The Windows UI currently initiates on every Wi-Fi pairing, so a peer hello
// may arrive before or after the ready response to our own hello.
class ControlNegotiationState final {
public:
    using Capabilities = std::set<std::string>;

    void markPaired(Capabilities localCapabilities) {
        reset();
        if (localCapabilities.size() > 16) throw std::invalid_argument("too many local capabilities");
        for (const auto &capability : localCapabilities)
            if (capability.empty() || capability.size() > 64) throw std::invalid_argument("invalid local capability");
        paired_ = true;
        localCapabilities_ = std::move(localCapabilities);
    }

    void startLocalHello() {
        requirePaired();
        if (localHelloSent_) throw std::logic_error("duplicate local control hello");
        localHelloSent_ = true;
    }

    Capabilities receiveHello(Capabilities peerCapabilities) {
        requirePaired();
        if (peerHelloReceived_) throw std::logic_error("duplicate peer control hello");
        validate(peerCapabilities);
        peerHelloReceived_ = true;
        peerCapabilities_ = std::move(peerCapabilities);
        const auto negotiated = intersection();
        if (peerReadyReceived_ && peerReadyCapabilities_ != negotiated)
            throw std::logic_error("peer ready does not match capability intersection");
        return negotiated;
    }

    void markLocalReadySent(const Capabilities &capabilities) {
        requirePaired();
        if (!peerHelloReceived_) throw std::logic_error("ready sent without peer hello");
        if (localReadySent_) throw std::logic_error("duplicate local control ready");
        if (capabilities != intersection()) throw std::logic_error("local ready does not match capability intersection");
        localReadySent_ = true;
    }

    void receiveReady(Capabilities capabilities) {
        requirePaired();
        if (!localHelloSent_) throw std::logic_error("peer ready without local hello");
        if (peerReadyReceived_) throw std::logic_error("duplicate peer control ready");
        validate(capabilities);
        if (!isSubset(capabilities, localCapabilities_))
            throw std::invalid_argument("peer ready claims unsupported capabilities");
        if (peerHelloReceived_ && capabilities != intersection())
            throw std::logic_error("peer ready does not match capability intersection");
        peerReadyReceived_ = true;
        peerReadyCapabilities_ = std::move(capabilities);
    }

    bool ready() const {
        return paired_ && ((localHelloSent_ && peerReadyReceived_) || (peerHelloReceived_ && localReadySent_));
    }

    bool supports(const std::string &capability) const {
        if (!ready()) return false;
        if (peerHelloReceived_ && localReadySent_) return intersection().contains(capability);
        return peerReadyCapabilities_.contains(capability);
    }

    void reset() {
        paired_ = false;
        localHelloSent_ = false;
        peerHelloReceived_ = false;
        localReadySent_ = false;
        peerReadyReceived_ = false;
        localCapabilities_.clear();
        peerCapabilities_.clear();
        peerReadyCapabilities_.clear();
    }

private:
    static void validate(const Capabilities &capabilities) {
        if (capabilities.size() > 16) throw std::invalid_argument("too many peer capabilities");
        for (const auto &capability : capabilities)
            if (capability.empty() || capability.size() > 64) throw std::invalid_argument("invalid peer capability");
    }
    static bool isSubset(const Capabilities &subset, const Capabilities &set) {
        for (const auto &value : subset) if (!set.contains(value)) return false;
        return true;
    }
    Capabilities intersection() const {
        Capabilities result;
        for (const auto &capability : peerCapabilities_)
            if (localCapabilities_.contains(capability)) result.insert(capability);
        return result;
    }
    void requirePaired() const {
        if (!paired_) throw std::logic_error("control negotiation before pairing");
    }

    bool paired_ = false;
    bool localHelloSent_ = false;
    bool peerHelloReceived_ = false;
    bool localReadySent_ = false;
    bool peerReadyReceived_ = false;
    Capabilities localCapabilities_;
    Capabilities peerCapabilities_;
    Capabilities peerReadyCapabilities_;
};

} // namespace streamcam
