// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include "wire.hpp"

namespace slx::sb {
class RemoteControl {
public:
    RemoteControl(const std::string &ip, uint16_t port, wire::Timing timing, double command_rate,
                  std::chrono::milliseconds timeout);
    SLX_SB_Command step(const SLX_SB_Sample &sample, const wire::Navigation &, bool drop_packet);
    void finish();
    const wire::Welcome &peer() const { return peer_; }
    const wire::OperatorOrder &order() const { return order_; }
    bool supports_operator() const { return operator_enabled_; }
    uint64_t session() const { return session_; }
    double rtt_ms() const { return rtt_ms_; }

private:
    wire::Packet exchange(wire::Packet, wire::Kind expected, bool drop = false);
    wire::Socket socket_;
    wire::Endpoint remote_;
    std::chrono::milliseconds timeout_;
    uint64_t session_{wire::nonce()}, next_{};
    wire::Welcome peer_;
    wire::OperatorOrder order_;
    bool operator_enabled_{};
    double rtt_ms_{};
};
} // namespace slx::sb
