// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include "runtime.hpp"
#include "wire.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <numbers>
#include <string_view>

namespace slx::sb {
constexpr double dt = SLX_SB_STEP_NS * 1e-9;
constexpr double to_degrees = 180. / std::numbers::pi;
constexpr const char *asset_version = "0.1.0";
constexpr const char *copyright = "Copyright (c) 2026 Marco Pezzullo (Silicon LogiX)";
inline constexpr std::array modes{"off", "manual", "pid", "slx"};
inline constexpr std::array faults{"none", "fin-stuck", "fin-slow", "fin-limited", "gyro-bias",
                            "gyro-clip", "imu-stale", "imu-missing", "link-loss"};
inline constexpr std::array timings{"software", "lockstep", "timed"};

struct Scenario {
    SLX_SB_ModelConfig model{};
    double duration_s{}, warmup_s{15}, manual_rad{}, ack_at_s{-1};
    uint32_t mode{SLX_SB_PID};
    wire::Timing timing{wire::Timing::software};
    bool pace_software{};
    std::string peer{"127.0.0.1"};
    uint16_t port{wire::port};
    uint32_t timeout_ms{250};
    std::string profile{"workboat"};
};

struct ScenarioPreset {
    const char *id, *title, *observation;
};
inline constexpr std::array presets{
    ScenarioPreset{"workboat", "Mare al traverso", "Osserva come il momento delle pinne modifica il rollio nello stesso mare."},
    ScenarioPreset{"parametric", "Rollio parametrico", "Segue la variazione del GM d'incontro mentre la coppia ondosa trasversale è ridotta."},
    ScenarioPreset{"free-surface", "Carico alto e superficie libera", "Confronta la curva GZ, l'assetto di equilibrio e la riserva di raddrizzamento."},
    ScenarioPreset{"downflooding", "Ingresso d'acqua asimmetrico", "Segui immersione delle aperture, acqua imbarcata e variazione della riserva."},
    ScenarioPreset{"capsize", "Sequenza critica fino al ribaltamento", "Segui gruppo d'onda, perdita di raddrizzamento e inversione della carena."},
    ScenarioPreset{"zero-speed", "Pinne a velocità nulla", "Confronta gli angoli degli attuatori con il momento disponibile a nave ferma."},
};

Scenario demo_scenario();
inline SLX_SB_LiveConditions live_conditions(const SLX_SB_ModelConfig &c)
{
    return {c.sea_kind, c.wave_height_m, c.wave_period_s, c.load_ratio, c.cg_rise_m,
            c.free_surface_loss_m, c.load_offset_m, c.actuator_delay_s};
}
Scenario scenario_preset(std::string_view id);
void import_curve(Scenario &, const fs::path &);
void validate(const Scenario &);
nlohmann::json configuration(const Scenario &);
Scenario scenario_from(const nlohmann::json &);
std::string_view mode_name(uint32_t);
std::string_view cause_name(uint32_t);
std::string_view state_name(uint32_t);
} // namespace slx::sb
