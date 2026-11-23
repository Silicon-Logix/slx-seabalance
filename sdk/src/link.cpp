// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "link.hpp"

#include <stdexcept>

namespace slx::sb {
using namespace std::chrono;

RemoteControl::RemoteControl(const std::string &ip, uint16_t port, wire::Timing timing,
                             double command_rate, milliseconds timeout)
    : remote_(wire::endpoint(ip, port)), timeout_(timeout)
{
    wire::Packet hello{};
    hello.kind = wire::Kind::hello;
    hello.hello.config = wire::config_with_operator;
    hello.hello.timing = timing;
    hello.hello.command_rate = command_rate;
    peer_ = exchange(hello, wire::Kind::welcome).welcome;
    if (peer_.status == wire::incompatible && peer_.abi == SLX_SB_ABI_VERSION &&
        peer_.calibration == SLX_SB_CALIBRATION_VERSION) {
        hello.hello.config = wire::config_version;
        peer_ = exchange(hello, wire::Kind::welcome).welcome;
    }
    if (peer_.status == wire::busy)
        throw std::runtime_error("Controller already has an active session");
    if (peer_.status != wire::ok || peer_.abi != SLX_SB_ABI_VERSION ||
        peer_.calibration != SLX_SB_CALIBRATION_VERSION || peer_.build == 0)
        throw std::runtime_error("Incompatible controller ABI, calibration or timing");
    operator_enabled_ = hello.hello.config == wire::config_with_operator &&
                        (peer_.capabilities & wire::supports_operator) != 0;
}

wire::Packet RemoteControl::exchange(wire::Packet message, wire::Kind expected, bool drop)
{
    message.session = session_;
    message.sim_ns = message.sequence * SLX_SB_STEP_NS;
    message.mono_ns = wire::monotonic_ns();
    const auto sent = steady_clock::now();
    if (!drop)
        socket_.send(message, remote_);
    const auto deadline = sent + timeout_;
    while (steady_clock::now() < deadline) {
        const auto left = ceil<milliseconds>(deadline - steady_clock::now());
        if (left <= milliseconds::zero())
            break;
        wire::Frame frame{};
        wire::Endpoint from{};
        if (!socket_.receive(frame, from, left) || from != remote_)
            continue;
        if (steady_clock::now() > deadline)
            break;
        const auto reply = wire::decode({frame.bytes.data(), frame.size});
        if (!reply || reply->session != session_ || reply->sequence != message.sequence)
            continue;
        if ((expected == wire::Kind::command || expected == wire::Kind::command_operated) &&
            reply->kind == wire::Kind::diagnostic &&
            reply->diagnostic.status == wire::bad_input)
            throw std::runtime_error("Controller rejected sample " + std::to_string(message.sequence));
        if (reply->kind == expected) {
            rtt_ms_ = duration<double, std::milli>(steady_clock::now() - sent).count();
            return *reply;
        }
    }
    throw std::runtime_error("Controller timeout at sequence " + std::to_string(message.sequence));
}

SLX_SB_Command RemoteControl::step(const SLX_SB_Sample &sample, const wire::Navigation &navigation, bool drop_packet)
{
    if (!drop_packet && next_ % 250 == 0) {
        wire::Packet monitor{};
        monitor.session = session_;
        monitor.sequence = next_;
        monitor.sim_ns = next_ * SLX_SB_STEP_NS;
        monitor.mono_ns = wire::monotonic_ns();
        monitor.kind = wire::Kind::heartbeat;
        socket_.send(monitor, remote_);
        monitor.kind = wire::Kind::diagnostic;
        socket_.send(monitor, remote_);
    }
    wire::Packet packet{};
    packet.kind = operator_enabled_ ? wire::Kind::sample_operated : wire::Kind::sample;
    packet.sequence = sample.sequence;
    packet.sample = sample;
    packet.navigation = navigation;
    const auto kind = operator_enabled_ ? wire::Kind::command_operated : wire::Kind::command;
    const auto result = exchange(packet, kind, drop_packet);
    if (result.order.revision < order_.revision)
        throw std::runtime_error("Controller returned a stale operator revision");
    order_ = result.order;
    next_ = sample.sequence + 1;
    return result.command;
}

void RemoteControl::finish()
{
    wire::Packet packet{};
    packet.kind = wire::Kind::end;
    packet.sequence = next_;
    exchange(packet, wire::Kind::end_ack);
}
} // namespace slx::sb
