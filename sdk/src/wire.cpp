// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "wire.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace slx::sb::wire {
namespace {
constexpr uint32_t magic = 0x42584c53; // SLXB
constexpr uint16_t version = 1;
constexpr uint32_t header_size = 48;
constexpr uint32_t command_flags = SLX_SB_EST_VALID | SLX_SB_ACCEL_REJECTED | SLX_SB_AT_LIMIT |
    SLX_SB_SPEED_LIMIT | SLX_SB_ALARM_ACKED | SLX_SB_PORT_MISMATCH | SLX_SB_STARBOARD_MISMATCH;

std::optional<uint32_t> payload(Kind kind)
{
    switch (kind) {
    case Kind::hello: case Kind::welcome: return 32;
    case Kind::sample: return 96;
    case Kind::sample_operated: return 120;
    case Kind::command: return 48;
    case Kind::command_operated: return 88;
    case Kind::diagnostic: return 16;
    case Kind::heartbeat: case Kind::end: case Kind::end_ack: return 0;
    }
    return std::nullopt;
}

struct Writer {
    Frame frame;
    template <class T> void put(T value)
    {
        static_assert(std::is_unsigned_v<T>);
        for (size_t i = 0; i < sizeof(T); ++i)
            frame.bytes[frame.size++] = static_cast<uint8_t>(value >> (8 * i));
    }
    void real(double value) { put(std::bit_cast<uint64_t>(value)); }
};
struct Reader {
    std::span<const uint8_t> bytes;
    size_t cursor{};
    template <class T> T get()
    {
        static_assert(std::is_unsigned_v<T>);
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<uint64_t>(bytes[cursor++]) << (8 * i);
        return static_cast<T>(value);
    }
    double real() { return std::bit_cast<double>(get<uint64_t>()); }
};

bool finite_sample(const SLX_SB_Sample &s)
{
    return std::all_of(s.accel_mps2, s.accel_mps2 + 3, [](double n) { return std::isfinite(n); }) &&
           std::all_of(s.gyro_rad_s, s.gyro_rad_s + 3, [](double n) { return std::isfinite(n); }) &&
           std::isfinite(s.speed_mps) && std::isfinite(s.manual_rad) &&
           std::isfinite(s.port_feedback_rad) && std::isfinite(s.starboard_feedback_rad);
}

bool valid_command(const SLX_SB_Command &c)
{
    return c.state <= SLX_SB_AWAIT_ACK && c.cause <= SLX_SB_CAUSE_RESTART &&
           c.effective_mode <= SLX_SB_SLX && (c.flags & ~command_flags) == 0 &&
           std::abs(c.port_rad) <= SLX_SB_FIN_LIMIT_RAD &&
           std::abs(c.starboard_rad) <= SLX_SB_FIN_LIMIT_RAD &&
           std::abs(c.estimated_roll_rad) <= 3.141592653589794 &&
           std::abs(c.estimated_rate_rad_s) <= 10.;
}
} // namespace

bool valid_order(const OperatorOrder &p)
{
    constexpr uint32_t valid_flags = operator_local | operator_navigation | operator_mark;
    return (p.flags & ~valid_flags) == 0 &&
           (!(p.flags & operator_navigation) || (p.flags & operator_local)) &&
           (p.revision != 0 || p.flags == 0) && p.mode <= SLX_SB_SLX &&
           std::abs(p.manual_rad) <= SLX_SB_FIN_LIMIT_RAD &&
           p.speed_mps >= 0. && p.speed_mps <= 9. &&
           std::abs(p.relative_heading_rad) <= 3.141592653589794;
}

Frame encode(const Packet &p)
{
    const auto length = payload(p.kind);
    if (!length)
        throw std::invalid_argument("Unsupported SLXB frame kind");
    Writer w;
    w.put(magic);
    w.put(version);
    w.put(static_cast<uint16_t>(p.kind));
    w.put(*length);
    w.put(uint32_t{0});
    w.put(p.session);
    w.put(p.sequence);
    w.put(p.sim_ns);
    w.put(p.mono_ns);
    switch (p.kind) {
    case Kind::hello:
        w.put(p.hello.abi); w.put(p.hello.config); w.put(p.hello.calibration);
        w.put(static_cast<uint32_t>(p.hello.timing)); w.put(p.hello.step);
        w.real(p.hello.command_rate);
        break;
    case Kind::welcome:
        w.put(p.welcome.abi); w.put(p.welcome.calibration); w.put(p.welcome.build);
        w.put(p.welcome.capabilities); w.put(p.welcome.platform); w.put(p.welcome.status);
        w.put(p.welcome.boot_id);
        break;
    case Kind::sample: case Kind::sample_operated:
        w.put(p.sample.imu_ns); w.put(p.sample.valid); w.put(p.sample.mode);
        for (double value : p.sample.accel_mps2)
            w.real(value);
        for (double value : p.sample.gyro_rad_s)
            w.real(value);
        w.real(p.sample.speed_mps); w.real(p.sample.port_feedback_rad);
        w.real(p.sample.starboard_feedback_rad); w.real(p.sample.manual_rad);
        if (p.kind == Kind::sample_operated) {
            w.real(p.navigation.relative_heading_rad); w.real(p.navigation.target_speed_mps);
            w.real(p.navigation.target_heading_rad);
        }
        break;
    case Kind::command: case Kind::command_operated:
        w.put(p.command.state); w.put(p.command.cause); w.put(p.command.flags);
        w.put(p.command.effective_mode);
        w.real(p.command.port_rad); w.real(p.command.starboard_rad);
        w.real(p.command.estimated_roll_rad); w.real(p.command.estimated_rate_rad_s);
        if (p.kind == Kind::command_operated) {
            w.put(p.order.revision); w.put(p.order.flags); w.put(p.order.mode);
            w.real(p.order.manual_rad); w.real(p.order.speed_mps); w.real(p.order.relative_heading_rad);
        }
        break;
    case Kind::diagnostic:
        w.put(p.diagnostic.state); w.put(p.diagnostic.cause); w.put(p.diagnostic.invalid_frames);
        w.put(p.diagnostic.status);
        break;
    case Kind::heartbeat: case Kind::end: case Kind::end_ack: break;
    }
    return w.frame;
}

std::optional<Packet> decode(std::span<const uint8_t> bytes)
{
    if (bytes.size() < header_size || bytes.size() > max_frame)
        return std::nullopt;
    Reader r{bytes};
    if (r.get<uint32_t>() != magic || r.get<uint16_t>() != version)
        return std::nullopt;
    Packet p;
    p.kind = static_cast<Kind>(r.get<uint16_t>());
    const auto length = payload(p.kind);
    if (!length || r.get<uint32_t>() != *length || r.get<uint32_t>() != 0 ||
        bytes.size() != header_size + *length)
        return std::nullopt;
    p.session = r.get<uint64_t>();
    p.sequence = r.get<uint64_t>();
    p.sim_ns = r.get<uint64_t>();
    p.mono_ns = r.get<uint64_t>();
    if (!p.session || p.sequence > UINT64_MAX / SLX_SB_STEP_NS ||
        p.sim_ns != p.sequence * SLX_SB_STEP_NS)
        return std::nullopt;
    switch (p.kind) {
    case Kind::hello:
        p.hello = {r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>(),
                   static_cast<Timing>(r.get<uint32_t>()), r.get<uint64_t>(), r.real()};
        if (p.sequence != 0 || !std::isfinite(p.hello.command_rate))
            return std::nullopt;
        break;
    case Kind::welcome:
        p.welcome = {r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>(),
                     r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint64_t>()};
        if (p.sequence != 0 || p.welcome.status > bad_input || p.welcome.boot_id == 0)
            return std::nullopt;
        break;
    case Kind::sample: case Kind::sample_operated:
        p.sample = abi_struct<SLX_SB_Sample>();
        p.sample.sequence = p.sequence;
        p.sample.sim_ns = p.sim_ns;
        p.sample.imu_ns = r.get<uint64_t>();
        p.sample.valid = r.get<uint32_t>();
        p.sample.mode = r.get<uint32_t>();
        for (double &value : p.sample.accel_mps2)
            value = r.real();
        for (double &value : p.sample.gyro_rad_s)
            value = r.real();
        p.sample.speed_mps = r.real();
        p.sample.port_feedback_rad = r.real();
        p.sample.starboard_feedback_rad = r.real();
        p.sample.manual_rad = r.real();
        if (!finite_sample(p.sample))
            return std::nullopt;
        if (p.kind == Kind::sample_operated) {
            p.navigation = {r.real(), r.real(), r.real()};
            if (!(std::abs(p.navigation.relative_heading_rad) <= 3.141592653589794 &&
                  p.navigation.target_speed_mps >= 0. && p.navigation.target_speed_mps <= 9. &&
                  std::abs(p.navigation.target_heading_rad) <= 3.141592653589794))
                return std::nullopt;
        }
        break;
    case Kind::command: case Kind::command_operated:
        p.command = {sizeof(SLX_SB_Command), SLX_SB_ABI_VERSION, p.sequence,
                     r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>(),
                     r.real(), r.real(), r.real(), r.real()};
        if (!valid_command(p.command))
            return std::nullopt;
        if (p.kind == Kind::command_operated) {
            p.order = {r.get<uint64_t>(), r.get<uint32_t>(), r.get<uint32_t>(), r.real(), r.real(), r.real()};
            if (!valid_order(p.order))
                return std::nullopt;
        }
        break;
    case Kind::diagnostic:
        p.diagnostic = {r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>(), r.get<uint32_t>()};
        if (p.diagnostic.state > SLX_SB_AWAIT_ACK || p.diagnostic.cause > SLX_SB_CAUSE_RESTART ||
            p.diagnostic.status > bad_input)
            return std::nullopt;
        break;
    case Kind::heartbeat: case Kind::end: case Kind::end_ack: break;
    }
    return p;
}

uint64_t monotonic_ns()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t nonce()
{
    std::random_device random;
    const uint64_t id = (uint64_t{random()} << 32) ^ random() ^ monotonic_ns();
    return id == 0 ? 1 : id;
}
} // namespace slx::sb::wire
