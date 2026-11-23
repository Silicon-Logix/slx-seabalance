// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "scenario.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace slx::sb {
using json = nlohmann::json;
namespace {
struct Parameter {
    const char *name;
    double SLX_SB_ModelConfig::*member;
};
constexpr Parameter parameters[] = {
    {"speed_mps", &SLX_SB_ModelConfig::speed_mps},
    {"relative_heading_rad", &SLX_SB_ModelConfig::relative_heading_rad},
    {"wave_height_m", &SLX_SB_ModelConfig::wave_height_m},
    {"wave_period_s", &SLX_SB_ModelConfig::wave_period_s},
    {"load_ratio", &SLX_SB_ModelConfig::load_ratio},
    {"cg_rise_m", &SLX_SB_ModelConfig::cg_rise_m},
    {"initial_roll_rad", &SLX_SB_ModelConfig::initial_roll_rad},
    {"actuator_rate_rad_s", &SLX_SB_ModelConfig::actuator_rate_rad_s},
    {"actuator_delay_s", &SLX_SB_ModelConfig::actuator_delay_s},
    {"fault_at_s", &SLX_SB_ModelConfig::fault_at_s},
    {"fault_duration_s", &SLX_SB_ModelConfig::fault_duration_s},
    {"imu_port_m", &SLX_SB_ModelConfig::imu_port_m},
    {"imu_up_m", &SLX_SB_ModelConfig::imu_up_m},
    {"accel_noise_mps2", &SLX_SB_ModelConfig::accel_noise_mps2},
    {"gyro_noise_rad_s", &SLX_SB_ModelConfig::gyro_noise_rad_s},
    {"gyro_bias_rad_s", &SLX_SB_ModelConfig::gyro_bias_rad_s},
    {"gyro_drift_rad_s_sqrt_s", &SLX_SB_ModelConfig::gyro_drift_rad_s_sqrt_s},
    {"accel_quant_mps2", &SLX_SB_ModelConfig::accel_quant_mps2},
    {"gyro_quant_rad_s", &SLX_SB_ModelConfig::gyro_quant_rad_s},
    {"accel_clip_mps2", &SLX_SB_ModelConfig::accel_clip_mps2},
    {"gyro_clip_rad_s", &SLX_SB_ModelConfig::gyro_clip_rad_s},
};
constexpr Parameter stability_parameters[] = {
    {"positive_range_rad", &SLX_SB_ModelConfig::positive_range_rad},
    {"downflood_rad", &SLX_SB_ModelConfig::downflood_rad},
    {"free_surface_loss_m", &SLX_SB_ModelConfig::free_surface_loss_m},
    {"load_offset_m", &SLX_SB_ModelConfig::load_offset_m},
    {"gm_modulation", &SLX_SB_ModelConfig::gm_modulation},
    {"flood_area_m2", &SLX_SB_ModelConfig::flood_area_m2},
    {"flood_capacity_kg", &SLX_SB_ModelConfig::flood_capacity_kg},
    {"flood_lever_m", &SLX_SB_ModelConfig::flood_lever_m},
    {"packet_height_m", &SLX_SB_ModelConfig::packet_height_m},
    {"packet_center_s", &SLX_SB_ModelConfig::packet_center_s},
    {"packet_width_s", &SLX_SB_ModelConfig::packet_width_s},
    {"jonswap_gamma", &SLX_SB_ModelConfig::jonswap_gamma},
};

template <typename T> T unsigned_field(const json &object, const char *name)
{
    const auto &value = object.at(name);
    if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<int64_t>() < 0))
        throw std::runtime_error(std::string("Expected an unsigned integer for ") + name);
    const auto n = value.get<uint64_t>();
    if (n > std::numeric_limits<T>::max())
        throw std::runtime_error(std::string("Integer out of range for ") + name);
    return static_cast<T>(n);
}
} // namespace

Scenario demo_scenario()
{
    Scenario s;
    auto &m = s.model;
    m = abi_struct<SLX_SB_ModelConfig>();
    m.seed = 61731;
    m.sea_kind = SLX_SB_IRREGULAR;
    m.speed_mps = 4.7;
    m.relative_heading_rad = 90 / to_degrees;
    m.wave_height_m = .78;
    m.wave_period_s = 5.1;
    m.load_ratio = 1;
    m.initial_roll_rad = .035;
    m.actuator_rate_rad_s = .95;
    m.actuator_delay_s = .06;
    m.fault_at_s = 25;
    m.fault_duration_s = 0;
    m.imu_port_m = .15;
    m.imu_up_m = 1.2;
    m.accel_noise_mps2 = .04;
    m.gyro_noise_rad_s = .002;
    m.gyro_bias_rad_s = .002;
    m.gyro_drift_rad_s_sqrt_s = .00012;
    m.accel_quant_mps2 = .002;
    m.gyro_quant_rad_s = .0002;
    m.accel_clip_mps2 = 39.2;
    m.gyro_clip_rad_s = 4.;
    m.calibration = SLX_SB_CALIBRATION_VERSION;
    m.positive_range_rad = 116 / to_degrees;
    m.downflood_rad = 50 / to_degrees;
    m.flood_capacity_kg = 5800;
    m.flood_lever_m = 1.05;
    m.packet_center_s = 24;
    m.packet_width_s = 3.2;
    m.jonswap_gamma = 3.3;
    return s;
}

Scenario scenario_preset(std::string_view id)
{
    auto s = demo_scenario();
    auto &m = s.model;
    s.profile = id;
    if (id == "parametric") {
        m.sea_kind = SLX_SB_REGULAR;
        m.speed_mps = 4.4;
        m.relative_heading_rad = 0;
        m.wave_height_m = 1.4;
        m.wave_period_s = 4.16;
        m.gm_modulation = .78;
        m.initial_roll_rad = .13;
    } else if (id == "free-surface") {
        m.wave_height_m = 1.12;
        m.load_ratio = 1.2;
        m.cg_rise_m = .23;
        m.free_surface_loss_m = .18;
        m.load_offset_m = -.045;
        m.positive_range_rad = 102 / to_degrees;
    } else if (id == "downflooding" || id == "capsize") {
        const bool severe = id == "capsize";
        m.speed_mps = 3.8;
        m.wave_height_m = severe ? .80 : 1.38;
        m.wave_period_s = severe ? 6.6 : 5.8;
        m.load_ratio = severe ? 1.27 : 1.14;
        m.cg_rise_m = severe ? .28 : .10;
        m.free_surface_loss_m = severe ? .11 : .05;
        m.load_offset_m = severe ? -.045 : -.04;
        m.initial_roll_rad = severe ? .12 : .04;
        m.positive_range_rad = (severe ? 92 : 115) / to_degrees;
        m.downflood_rad = (severe ? 34 : 41) / to_degrees;
        m.flood_area_m2 = severe ? .09 : .038;
        m.packet_height_m = severe ? 4.5 : 2.15;
        m.packet_center_s = 18.;
    } else if (id == "zero-speed") {
        m.speed_mps = 0;
    } else if (id != "workboat") {
        throw std::invalid_argument("Unknown scenario preset");
    }
    return s;
}

void import_curve(Scenario &scenario, const fs::path &path)
{
    if (fs::file_size(path) > 8192)
        throw std::invalid_argument("GZ curve exceeds the supported point table");
    std::ifstream file(path);
    std::string line;
    auto strip_cr = [](std::string &s) { if (!s.empty() && s.back() == '\r') s.pop_back(); };
    if (std::getline(file, line)) strip_cr(line);
    if (line.starts_with("#") && std::getline(file, line))
        strip_cr(line);
    if (line != "heel_deg,gz_m")
        throw std::invalid_argument("GZ CSV header must be heel_deg,gz_m");
    auto config = scenario.model;
    config.gz_point_count = 0;
    auto number = [](std::string_view text) {
        double x{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), x);
        if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(x))
            throw std::invalid_argument("Invalid GZ CSV field");
        return x;
    };
    while (std::getline(file, line)) {
        strip_cr(line);
        const auto comma = line.find(',');
        if (comma == std::string::npos || config.gz_point_count == SLX_SB_MAX_GZ_POINTS)
            throw std::invalid_argument("GZ CSV requires two values per row and at most 32 points");
        const std::string_view row(line);
        config.gz_curve[config.gz_point_count++] = {number(row.substr(0, comma)) / to_degrees,
                                                    number(row.substr(comma + 1))};
    }
    Model checked(core_file(executable_directory(), true), config);
    scenario.model = config;
    scenario.profile = "custom-gz";
}

void validate(const Scenario &s)
{
    constexpr double last_timestamp_s = (std::numeric_limits<int64_t>::max() / 2) * 1e-9;
    const bool indefinite = s.duration_s == 0;
    if (!(indefinite || (s.duration_s >= dt && s.duration_s < last_timestamp_s)) ||
        !(s.warmup_s >= 0 && s.warmup_s < (indefinite ? last_timestamp_s : s.duration_s)) ||
        !(std::abs(s.manual_rad) <= SLX_SB_FIN_LIMIT_RAD) ||
        !(s.ack_at_s == -1 || (s.ack_at_s >= 0 && s.ack_at_s < (indefinite ? last_timestamp_s : s.duration_s))) ||
        s.mode >= modes.size() || static_cast<uint32_t>(s.timing) >= timings.size() ||
        s.timeout_ms < 20 || s.timeout_ms > 1000 || s.port == 0)
        throw std::invalid_argument("Use 0 for a continuous run or at least one 20 ms sample; verify the analysis window, ACK time and timeout");
    if (s.timing != wire::Timing::software)
        wire::endpoint(s.peer, s.port);
}

json configuration(const Scenario &s)
{
    const auto &m = s.model;
    json model = {{"seed", m.seed}, {"sea_kind", m.sea_kind}, {"fault", m.fault},
                  {"imu_drop_every", m.imu_drop_every}, {"calibration", m.calibration}};
    for (const auto &param : parameters)
        model[param.name] = m.*param.member;
    for (const auto &param : stability_parameters)
        model[param.name] = m.*param.member;
    model["gz_curve"] = json::array();
    for (uint32_t i = 0; i < m.gz_point_count; ++i)
        model["gz_curve"].push_back({m.gz_curve[i].heel_rad, m.gz_curve[i].arm_m});
    return {{"copyright", copyright}, {"schema", 2}, {"step_ns", SLX_SB_STEP_NS}, {"profile", s.profile},
            {"duration_s", s.duration_s}, {"warmup_s", s.warmup_s},
            {"ack_at_s", s.ack_at_s}, {"manual_rad", s.manual_rad}, {"mode", s.mode},
            {"timing", static_cast<uint32_t>(s.timing)}, {"pace_software", s.pace_software},
            {"peer", s.peer}, {"port", s.port}, {"timeout_ms", s.timeout_ms}, {"model", model}};
}

Scenario scenario_from(const json &data)
{
    const auto version = unsigned_field<uint32_t>(data, "schema");
    if ((version != 1 && version != 2) || data.at("step_ns") != SLX_SB_STEP_NS)
        throw std::runtime_error("Incompatible scenario schema or sensor step");
    auto s = demo_scenario();
    const auto &m = data.at("model");
    s.model.seed = unsigned_field<uint64_t>(m, "seed");
    s.model.sea_kind = unsigned_field<uint32_t>(m, "sea_kind");
    s.model.fault = unsigned_field<uint32_t>(m, "fault");
    s.model.imu_drop_every = unsigned_field<uint32_t>(m, "imu_drop_every");
    s.model.calibration = unsigned_field<uint32_t>(m, "calibration");
    for (const auto &p : parameters)
        s.model.*p.member = m.at(p.name).get<double>();
    if (version == 2) {
        s.profile = data.at("profile").get<std::string>();
        for (const auto &p : stability_parameters)
            s.model.*p.member = m.at(p.name).get<double>();
        const auto &curve = m.at("gz_curve");
        if (!curve.is_array() || curve.size() > SLX_SB_MAX_GZ_POINTS)
            throw std::invalid_argument("Invalid GZ point table");
        for (const auto &point : curve) {
            if (!point.is_array() || point.size() != 2)
                throw std::invalid_argument("Each GZ point needs radians and metres");
            s.model.gz_curve[s.model.gz_point_count++] = {point.at(0).get<double>(), point.at(1).get<double>()};
        }
    } else {
        s.profile = "legacy-v1";
    }
    data.at("duration_s").get_to(s.duration_s);
    data.at("warmup_s").get_to(s.warmup_s);
    s.mode = unsigned_field<uint32_t>(data, "mode");
    data.at("manual_rad").get_to(s.manual_rad);
    data.at("ack_at_s").get_to(s.ack_at_s);
    s.timing = static_cast<wire::Timing>(unsigned_field<uint32_t>(data, "timing"));
    data.at("pace_software").get_to(s.pace_software);
    data.at("peer").get_to(s.peer);
    s.port = unsigned_field<uint16_t>(data, "port");
    s.timeout_ms = unsigned_field<uint32_t>(data, "timeout_ms");
    validate(s);
    return s;
}

std::string_view mode_name(uint32_t mode) { return mode < modes.size() ? modes[mode] : "unknown"; }
std::string_view cause_name(uint32_t cause)
{
    constexpr std::array values{"none", "imu_quality", "imu_stale", "fin_tracking", "link", "service_restart"};
    return cause < values.size() ? values[cause] : "unknown";
}
std::string_view state_name(uint32_t state)
{
    constexpr std::array values{"ready", "active", "degraded", "await_ack"};
    return state < values.size() ? values[state] : "unknown";
}
} // namespace slx::sb
