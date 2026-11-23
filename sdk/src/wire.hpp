// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include <slx/seabalance.h>

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <string>

namespace slx::sb::wire {
constexpr uint16_t port = 47420;
constexpr uint32_t config_version = 1;
constexpr uint32_t config_with_operator = 2;
constexpr uint32_t supports_slx = 1;
constexpr uint32_t supports_operator = 2;
constexpr size_t max_frame = 168;
enum class Kind : uint16_t {
    hello = 1, welcome, sample, command, heartbeat, diagnostic, end, end_ack, command_operated, sample_operated
};
enum Status : uint32_t { ok, incompatible, busy, bad_input };
enum class Timing : uint32_t { software, lockstep, timed };

struct Hello {
    uint32_t abi{SLX_SB_ABI_VERSION}, config{config_version};
    uint32_t calibration{SLX_SB_CALIBRATION_VERSION};
    Timing timing{Timing::lockstep};
    uint64_t step{SLX_SB_STEP_NS};
    double command_rate{.95};
};
struct Welcome {
    uint32_t abi{SLX_SB_ABI_VERSION}, calibration{SLX_SB_CALIBRATION_VERSION};
    uint32_t build{}, capabilities{}, platform{}, status{};
    uint64_t boot_id{};
};
struct Diagnostic {
    uint32_t state{}, cause{}, invalid_frames{}, status{};
};
enum OperatorFlags : uint32_t { operator_local = 1, operator_navigation = 2, operator_mark = 4 };
struct OperatorOrder {
    uint64_t revision{};
    uint32_t flags{}, mode{};
    double manual_rad{}, speed_mps{}, relative_heading_rad{};
};
struct Navigation {
    double relative_heading_rad{}, target_speed_mps{}, target_heading_rad{};
};
bool valid_order(const OperatorOrder &);
struct Packet {
    Kind kind{};
    uint64_t session{}, sequence{}, sim_ns{}, mono_ns{};
    Hello hello;
    Welcome welcome;
    SLX_SB_Sample sample{};
    SLX_SB_Command command{};
    Diagnostic diagnostic;
    OperatorOrder order;
    Navigation navigation;
};
struct Frame {
    std::array<uint8_t, max_frame + 1> bytes{};
    size_t size{};
};

Frame encode(const Packet &);
std::optional<Packet> decode(std::span<const uint8_t>);
uint64_t monotonic_ns();
uint64_t nonce();

struct Endpoint {
    uint32_t ip{};
    uint16_t port{};
    bool operator==(const Endpoint &) const = default;
};
Endpoint endpoint(const std::string &ipv4, uint16_t port);

class Socket {
public:
    Socket();
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    void bind(Endpoint);
    void send(const Packet &, Endpoint) const;
    bool receive(Frame &, Endpoint &, std::chrono::milliseconds wait) const;

private:
    uintptr_t handle_{};
};
} // namespace slx::sb::wire
