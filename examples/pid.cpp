// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "pid.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <numbers>

namespace {
constexpr double dt = SLX_SB_STEP_NS * 1e-9;
constexpr double g = 9.80665;
constexpr double limit = SLX_SB_FIN_LIMIT_RAD;
constexpr double min_speed = 1.5;
constexpr uint32_t known = SLX_SB_ACCEL_VALID | SLX_SB_GYRO_VALID | SLX_SB_FINS_VALID |
                           SLX_SB_ACCEL_CLIPPED | SLX_SB_GYRO_CLIPPED | SLX_SB_ACK_ALARM;

bool bounded(double x, double range)
{
    return std::isfinite(x) && std::abs(x) <= range;
}

slx::sb::PidGains reference_gains(double speed)
{
    const double authority = std::clamp(22.09 / (speed * speed), .34, 2.1);
    return {.72 * authority, .14 * authority, 1.15 * authority};
}
} // namespace

struct SLX_SB_Control {
    SLX_SB_ControlConfig config;
    slx::sb::GainSchedule advanced;
    uint64_t next{};
    uint32_t alarm{}, last_mode{}, tracking_flags{};
    bool initialized{};
    double angle{}, rate{}, bias{}, integral{}, port{}, starboard{};
    double disagreement_s{}, port_residual_s{}, starboard_residual_s{};

    void estimate(const SLX_SB_Sample &s, bool gyro, bool accel, uint32_t &flags) noexcept
    {
        const double magnitude = std::hypot(s.accel_mps2[0], s.accel_mps2[1], s.accel_mps2[2]);
        const double magnitude_error = std::abs(magnitude - g);
        const bool gravity_like = accel && magnitude_error < 1.2;
        if (!gyro || !initialized)
            initialized = gravity_like && gyro;
        const double measured = std::atan2(s.accel_mps2[1], s.accel_mps2[2]);
        if (gyro)
            rate = s.gyro_rad_s[0] - bias;
        if (!initialized) {
            disagreement_s = 0;
            return;
        }
        const bool prior = (flags & SLX_SB_EST_VALID) != 0;
        const double predicted = prior ? angle + dt * rate : measured;
        const double innovation = std::remainder(measured - predicted, 2 * std::numbers::pi);
        const bool accepted = gravity_like && std::abs(innovation) < .22;
        disagreement_s = gravity_like && std::abs(innovation) >= .22 ? disagreement_s + dt : 0.;
        if (accepted) {
            const double trust = std::clamp(1. - magnitude_error / 1.2, .15, 1.) /
                                 (1. + .65 * std::abs(rate));
            angle = predicted + trust * dt / (.69 + dt) * innovation;
            if (prior && magnitude_error < .45 && std::abs(rate) < .55 && std::abs(innovation) < .09)
                bias = std::clamp(bias - .11 * dt * trust * innovation, -.045, .045);
        } else {
            angle = predicted;
        }
        angle = std::remainder(angle, 2 * std::numbers::pi);
        flags |= SLX_SB_EST_VALID;
        if (!accepted)
            flags |= SLX_SB_ACCEL_REJECTED;
    }

    SLX_SB_Command step(const SLX_SB_Sample &s) noexcept
    {
        const bool present = s.valid & (SLX_SB_ACCEL_VALID | SLX_SB_GYRO_VALID |
                                        SLX_SB_ACCEL_CLIPPED | SLX_SB_GYRO_CLIPPED);
        const bool stale = present && s.sim_ns - s.imu_ns > 3 * SLX_SB_STEP_NS;
        const bool gyro = !stale && (s.valid & SLX_SB_GYRO_VALID);
        const bool accel = !stale && (s.valid & SLX_SB_ACCEL_VALID);
        const bool feedback = s.valid & SLX_SB_FINS_VALID;
        uint32_t flags = initialized ? SLX_SB_EST_VALID : 0;
        estimate(s, gyro, accel, flags);
        if (!initialized)
            flags = SLX_SB_ACCEL_REJECTED;

        const bool auto_mode = s.mode == SLX_SB_PID || s.mode == SLX_SB_SLX;
        const bool speed_limited = auto_mode && s.speed_mps < min_speed;
        const bool check_tracking = feedback && alarm == SLX_SB_CAUSE_NONE;
        port_residual_s = check_tracking && std::abs(s.port_feedback_rad - port) > .115
                              ? port_residual_s + dt : 0.;
        starboard_residual_s = check_tracking && std::abs(s.starboard_feedback_rad - starboard) > .115
                                   ? starboard_residual_s + dt : 0.;
        const bool port_mismatch = port_residual_s >= 1.5;
        const bool starboard_mismatch = starboard_residual_s >= 1.5;
        uint32_t observation = SLX_SB_CAUSE_NONE;
        if (stale)
            observation = SLX_SB_CAUSE_STALE;
        else if (!gyro || !initialized || disagreement_s >= 1.8)
            observation = SLX_SB_CAUSE_IMU;
        else if (!feedback || port_mismatch || starboard_mismatch)
            observation = SLX_SB_CAUSE_TRACKING;
        if (observation && !alarm) {
            alarm = observation;
            if (observation == SLX_SB_CAUSE_TRACKING) {
                tracking_flags = (port_mismatch ? uint32_t{SLX_SB_PORT_MISMATCH} : 0u) |
                                 (starboard_mismatch ? uint32_t{SLX_SB_STARBOARD_MISMATCH} : 0u);
            }
        }

        const double max_delta = config.command_rate_rad_s * dt;
        const bool recoverable = alarm && !observation && feedback &&
            std::max({std::abs(s.port_feedback_rad), std::abs(s.starboard_feedback_rad),
                      std::abs(port), std::abs(starboard)}) < .055;
        uint32_t state = SLX_SB_READY;
        if (alarm) {
            state = recoverable ? SLX_SB_AWAIT_ACK : SLX_SB_DEGRADED;
            if (recoverable && (s.valid & SLX_SB_ACK_ALARM)) {
                alarm = tracking_flags = 0;
                flags |= SLX_SB_ALARM_ACKED;
                state = SLX_SB_READY;
            }
        }
        flags |= tracking_flags;
        if (speed_limited)
            flags |= SLX_SB_SPEED_LIMIT;

        uint32_t mode = !alarm && !speed_limited ? s.mode : uint32_t{SLX_SB_OFF};
        if ((flags & SLX_SB_ALARM_ACKED) != 0)
            mode = SLX_SB_OFF;
        double target = 0.;
        if (mode == SLX_SB_MANUAL)
            target = s.manual_rad;
        if (mode == SLX_SB_PID || mode == SLX_SB_SLX) {
            // The integral contribution is in radians of differential command.
            const auto gains = mode == SLX_SB_SLX ? advanced(s.speed_mps, angle)
                                                  : reference_gains(s.speed_mps);
            const double pd = -gains.kp * angle - gains.kd * rate;
            if (mode != last_mode)
                integral = std::clamp((port - starboard) * .5 - pd, -.16, .16);
            const double output = pd + integral;
            const double delta_i = -gains.ki * angle * dt;
            if (std::abs(output) < limit || output * delta_i < 0)
                integral = std::clamp(integral + delta_i, -.16, .16);
            target = pd + integral;
        }

        if (std::abs(target) >= limit - 1e-6)
            flags |= SLX_SB_AT_LIMIT;
        target = std::clamp(target, -limit, limit);
        port += std::clamp(target - port, -max_delta, max_delta);
        starboard += std::clamp(-target - starboard, -max_delta, max_delta);
        last_mode = mode;
        if (mode != SLX_SB_OFF)
            state = SLX_SB_ACTIVE;
        ++next;
        return {sizeof(SLX_SB_Command), SLX_SB_ABI_VERSION, s.sequence, state, alarm, flags, mode,
                port, starboard, angle, rate};
    }
};

namespace slx::sb {
SLX_SB_Result make_controller(const SLX_SB_ControlConfig *c, SLX_SB_Control **out,
                              GainSchedule schedule) noexcept
{
    if (!out)
        return SLX_SB_E_ARGUMENT;
    *out = nullptr;
    if (!c)
        return SLX_SB_E_ARGUMENT;
    if (c->size < sizeof(*c) || c->abi != SLX_SB_ABI_VERSION)
        return SLX_SB_E_ABI;
    if (c->calibration != SLX_SB_CALIBRATION_VERSION || c->retained_cause > SLX_SB_CAUSE_RESTART ||
        !bounded(c->command_rate_rad_s, 2.) || c->command_rate_rad_s < .1)
        return SLX_SB_E_RANGE;
    *out = new (std::nothrow) SLX_SB_Control{*c, schedule, 0, c->retained_cause};
    return *out ? SLX_SB_OK : SLX_SB_E_MEMORY;
}

void destroy_controller(SLX_SB_Control *control) noexcept
{
    delete control;
}

SLX_SB_Result step_controller(SLX_SB_Control *c, const SLX_SB_Sample *s, SLX_SB_Command *out) noexcept
{
    if (!c || !s || !out)
        return SLX_SB_E_ARGUMENT;
    if (s->size < sizeof(*s) || out->size < sizeof(*out) ||
        s->abi != SLX_SB_ABI_VERSION || out->abi != SLX_SB_ABI_VERSION)
        return SLX_SB_E_ABI;
    if (s->sequence != c->next || s->sequence > UINT64_MAX / SLX_SB_STEP_NS ||
        s->sim_ns != s->sequence * SLX_SB_STEP_NS)
        return SLX_SB_E_SEQUENCE;
    if (s->mode > SLX_SB_SLX || (s->mode == SLX_SB_SLX && !c->advanced) ||
        (s->valid & ~known) || s->imu_ns > s->sim_ns ||
        !bounded(s->speed_mps, 9) || s->speed_mps < 0 ||
        !bounded(s->manual_rad, limit) || !bounded(s->port_feedback_rad, limit) ||
        !bounded(s->starboard_feedback_rad, limit))
        return SLX_SB_E_RANGE;
    for (int axis = 0; axis < 3; ++axis) {
        if (!bounded(s->accel_mps2[axis], 100) || !bounded(s->gyro_rad_s[axis], 10))
            return SLX_SB_E_RANGE;
    }
    *out = c->step(*s);
    return SLX_SB_OK;
}

SLX_SB_ControlAPI reference_api() noexcept
{
    return {sizeof(SLX_SB_ControlAPI), SLX_SB_ABI_VERSION, SLX_SB_CONTROLLER_BUILD_VERSION,
            [](const SLX_SB_ControlConfig *c, SLX_SB_Control **out) {
                return make_controller(c, out, nullptr);
            }, destroy_controller, step_controller};
}
} // namespace slx::sb
