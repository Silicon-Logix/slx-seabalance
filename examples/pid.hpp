// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include <slx/seabalance.h>

namespace slx::sb {
struct PidGains {
    double kp, ki, kd;
};
using GainSchedule = PidGains (*)(double speed_mps, double roll_rad) noexcept;

// Use a null schedule for the editable reference PID only.
SLX_SB_Result make_controller(const SLX_SB_ControlConfig *, SLX_SB_Control **, GainSchedule) noexcept;
void destroy_controller(SLX_SB_Control *) noexcept;
SLX_SB_Result step_controller(SLX_SB_Control *, const SLX_SB_Sample *, SLX_SB_Command *) noexcept;
SLX_SB_ControlAPI reference_api() noexcept;
} // namespace slx::sb
