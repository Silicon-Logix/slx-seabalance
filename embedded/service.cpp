// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "panel_state.hpp"
#include "pid.hpp"

#include <charconv>
#include <cmath>
#include <csignal>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace slx::sb {
namespace {
using namespace std::chrono_literals;
using namespace wire;
constexpr uint64_t inactivity_ns = 800000000;
volatile std::sig_atomic_t exit_requested{};
void stop(int) { exit_requested = 1; }

struct Options {
    std::string address = "127.0.0.1";
    uint16_t port_number = wire::port;
    fs::path state = "board-state", runtime = "board-status";
    fs::path core = core_file(executable_directory(), false);
    bool reference_only{};
};

class Service {
public:
    explicit Service(const Options &opt)
    {
        if (!opt.reference_only && fs::exists(opt.core))
            core_.emplace(opt.core);
        socket_.bind(endpoint(opt.address, opt.port_number));
        writer_.emplace(opt.state, opt.runtime, boot_);
        state_ = writer_->initial();
        state_.has_slx = core_.has_value();
        writer_->update(state_);
        std::cout << "SLX SeaBalance UDP " << opt.address << ':' << opt.port_number
                  << " ABI " << SLX_SB_ABI_VERSION << " PID" << (core_ ? " + SLX" : "") << std::endl;
    }

    void run()
    {
        while (!exit_requested) {
            Frame frame{};
            Endpoint sender{};
            const bool received = socket_.receive(frame, sender, 25ms);
            const auto now = monotonic_ns();
            if (active_ && now - last_sample_ns_ > inactivity_ns)
                end_session(now, SLX_SB_CAUSE_LINK, "link_timeout");
            if (!received)
                continue;
            const auto p = decode({frame.bytes.data(), frame.size});
            if (p)
                process(*p, sender, now);
            else
                invalid();
        }
        if (active_)
            end_session(monotonic_ns(), SLX_SB_CAUSE_RESTART, "service_stop_during_session");
    }

private:
    std::optional<ControlCore> core_;
    std::unique_ptr<Control> active_;
    Socket socket_;
    std::optional<PanelWriter> writer_;
    const uint64_t boot_ = nonce();
    PanelState state_{};
    Endpoint owner_{};
    uint64_t next_{}, last_sample_ns_{}, endpoint_mono_{};

    void invalid()
    {
        if (state_.invalid_frames < std::numeric_limits<uint32_t>::max())
            ++state_.invalid_frames;
        writer_->update(state_);
    }

    void end_session(uint64_t now, uint32_t cause, const char *name)
    {
        state_.connected = false;
        state_.updated_ns = now;
        if (cause && !state_.command.cause) {
            state_.command.cause = cause;
            state_.last_alarm = cause;
        }
        state_.command.state = state_.command.cause ? SLX_SB_AWAIT_ACK : SLX_SB_READY;
        active_.reset();
        writer_->update(state_, name);
    }

    void welcome(const Packet &p, Endpoint sender, uint64_t now)
    {
        const auto &h = p.hello;
        const auto api = core_ ? core_->api() : reference_api();
        uint32_t status = ok;
        if (h.abi != SLX_SB_ABI_VERSION || (h.config != config_version && h.config != config_with_operator) ||
            h.calibration != SLX_SB_CALIBRATION_VERSION || h.step != SLX_SB_STEP_NS ||
            (h.timing != Timing::lockstep && h.timing != Timing::timed))
            status = incompatible;
        else if (h.command_rate < .1 || h.command_rate > 2.)
            status = bad_input;
        else if (active_ || p.session == state_.session)
            status = busy;
        else {
            const uint32_t previous = state_.command.cause;
            active_ = std::make_unique<Control>(api, previous, h.command_rate);
            owner_ = sender;
            state_.timing = h.timing;
            state_.session = p.session;
            state_.sequence = next_ = 0;
            state_.connected = true;
            state_.sampled = false;
            state_.operator_enabled = h.config == config_with_operator;
            state_.sample = {};
            state_.order = {};
            state_.navigation = {};
            state_.command = {};
            state_.command.cause = previous;
            state_.command.state = previous ? SLX_SB_AWAIT_ACK : SLX_SB_READY;
            state_.updated_ns = last_sample_ns_ = now;
            endpoint_mono_ = p.mono_ns;
            writer_->update(state_, "session_start");
        }
        Packet answer{};
        answer.kind = Kind::welcome;
        answer.session = p.session;
        answer.mono_ns = monotonic_ns();
        answer.welcome = {SLX_SB_ABI_VERSION, SLX_SB_CALIBRATION_VERSION, api.build,
                         supports_operator | (core_ ? supports_slx : 0u), platform_id(), status, boot_};
        socket_.send(answer, sender);
    }

    void process(const Packet &p, Endpoint sender, uint64_t now)
    {
        if (p.kind == Kind::hello) {
            welcome(p, sender, now);
            return;
        }
        if (!active_ || sender != owner_ || p.session != state_.session ||
            p.sequence != next_ || p.mono_ns < endpoint_mono_) {
            invalid();
            return;
        }
        Packet reply{};
        reply.session = p.session;
        reply.sequence = p.sequence;
        reply.sim_ns = p.sim_ns;
        switch (p.kind) {
        case Kind::sample: case Kind::sample_operated: {
            if (state_.operator_enabled != (p.kind == Kind::sample_operated)) {
                invalid();
                return;
            }
            auto sample = p.sample;
            auto order = state_.order;
            const auto pending = writer_->take_order(state_.session);
            if (pending && pending->revision == order.revision + 1)
                order = *pending;
            if (order.flags & operator_local) {
                sample.mode = order.mode;
                sample.manual_rad = order.manual_rad;
            }
            if (writer_->take_ack(state_.session))
                sample.valid |= SLX_SB_ACK_ALARM;
            const auto old = state_.command.cause;
            if (active_->try_step(sample, reply.command) != SLX_SB_OK) {
                invalid();
                reply.kind = Kind::diagnostic;
                reply.diagnostic = {state_.command.state, old, state_.invalid_frames, bad_input};
                break;
            }
            reply.kind = state_.operator_enabled ? Kind::command_operated : Kind::command;
            reply.order = order;
            const bool new_order = order.revision != state_.order.revision;
            last_sample_ns_ = now;
            ++next_;
            state_.sample = sample;
            state_.navigation = p.navigation;
            state_.order = order;
            state_.sampled = true;
            state_.command = reply.command;
            state_.sequence = p.sequence;
            state_.updated_ns = now;
            const char *event = nullptr;
            if (reply.command.cause && reply.command.cause != old) {
                state_.last_alarm = reply.command.cause;
                event = "alarm";
            } else if (reply.command.flags & SLX_SB_ALARM_ACKED) {
                event = "operator_ack";
            }
            if (new_order)
                writer_->update(state_, order.flags & operator_mark ? "video_mark" : "operator_order");
            writer_->update(state_, event);
            break;
        }
        case Kind::heartbeat:
            reply.kind = Kind::heartbeat;
            break;
        case Kind::diagnostic:
            reply.kind = Kind::diagnostic;
            reply.diagnostic = {state_.command.state, state_.command.cause, state_.invalid_frames, ok};
            break;
        case Kind::end:
            reply.kind = Kind::end_ack;
            end_session(now, SLX_SB_CAUSE_NONE, "session_end");
            break;
        default:
            invalid();
            return;
        }
        endpoint_mono_ = p.mono_ns;
        reply.mono_ns = monotonic_ns();
        socket_.send(reply, sender);
    }
};
} // namespace
} // namespace slx::sb

int main(int argc, char **argv)
{
    using namespace slx::sb;
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "slx-seabalance-controller [--bind IPv4] [--port 47420] "
                             "[--state DIR] [--status DIR] [--core FILE | --pid-only]\n";
                return 0;
            }
            if (arg == "--pid-only") {
                options.reference_only = true;
                continue;
            }
            if (++i == argc)
                throw std::invalid_argument("Missing option value");
            const std::string value = argv[i];
            if (arg == "--bind")
                options.address = value;
            else if (arg == "--port") {
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.port_number);
                if (error != std::errc() || end != value.data() + value.size() || !options.port_number)
                    throw std::invalid_argument("Invalid port");
            } else if (arg == "--state")
                options.state = value;
            else if (arg == "--status")
                options.runtime = value;
            else if (arg == "--core") {
                options.core = value;
                if (!fs::is_regular_file(options.core))
                    throw std::invalid_argument("Specified control core is missing");
            } else
                throw std::invalid_argument("Unknown option " + std::string(arg));
        }
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        Service service(options);
        service.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "SeaBalance controller: " << e.what() << '\n';
        return 1;
    }
}
