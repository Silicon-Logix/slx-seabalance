// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "session.hpp"

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace slx::sb {
namespace {
using json = nlohmann::json;
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }

void help()
{
    std::cout <<
        "SLX SeaBalance 0.3.0\n"
        "  sample-config                  Print editable JSON scenario\n"
        "  run --output DIR               Record a scenario\n"
        "  compare --output DIR [--slx]   Compare Off/PID/SLX with common waves\n"
        "  campaign --output DIR [--slx]  Sweep speed, waves, load and faults\n"
        "  replay DIR [--at SECONDS]      Inspect original session and one recorded frame\n"
        "  analyze DIR --output DIR      Reanalyze with optional --warmup N and --note TEXT\n"
        "Run options: --config FILE --mode off|manual|pid|slx --timing software|lockstep|timed\n"
        "  --preset NAME --gz FILE (CSV heel_deg,gz_m)\n"
        "  --peer IPv4 --port N --timeout-ms N --seconds N --warmup N --seed N --speed-mps N\n"
        "  --sea regular|irregular --height-m N --period-s N --heading-deg N --load N --cg-m N\n"
        "  --roll-deg N --manual-deg N --fault NAME --fault-at N --fault-duration N --ack-at N\n"
        "Faults:";
    for (const auto *fault : faults)
        std::cout << ' ' << fault;
    std::cout << "\nPresets:";
    for (const auto &preset : presets)
        std::cout << ' ' << preset.id;
    std::cout << "\nDefault run duration is continuous. Press Ctrl+C to finalize.\n";
}

template <typename T> T parse(std::string_view text)
{
    T x{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), x);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("Invalid number " + std::string(text));
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(x))
            throw std::invalid_argument("Non-finite number");
    return x;
}

template <class Array> uint32_t parse_enum(std::string_view name, const Array &values)
{
    const auto found = std::find(values.begin(), values.end(), name);
    if (found == values.end())
        throw std::invalid_argument("Unsupported value " + std::string(name));
    return static_cast<uint32_t>(found - values.begin());
}

fs::path path_from(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

struct Arguments {
    Scenario scenario = demo_scenario();
    fs::path directory;
    bool include_slx{};
};

Arguments arguments(int argc, char **argv)
{
    Arguments a;
    bool specify_warmup = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view option = argv[i];
        if (option == "--slx") {
            a.include_slx = true;
            continue;
        }
        if (i + 1 >= argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        const std::string_view value = argv[++i];
        auto &s = a.scenario;
        auto &m = s.model;
        if (option == "--output") a.directory = path_from(value);
        else if (option == "--config") {
            s = scenario_from(read_json(path_from(value)));
            specify_warmup = true;
        } else if (option == "--preset") {
            auto p = scenario_preset(value);
            s.model = p.model;
            s.profile = p.profile;
        } else if (option == "--gz") {
            import_curve(s, path_from(value));
        } else if (option == "--timing") s.timing = static_cast<wire::Timing>(parse_enum(value, timings));
        else if (option == "--mode") s.mode = parse_enum(value, modes);
        else if (option == "--sea") m.sea_kind = parse_enum(value, std::array{"regular", "irregular"});
        else if (option == "--fault") m.fault = parse_enum(value, faults);
        else if (option == "--peer") s.peer = value;
        else if (option == "--port") s.port = parse<uint16_t>(value);
        else if (option == "--timeout-ms") s.timeout_ms = parse<uint32_t>(value);
        else if (option == "--seed") m.seed = parse<uint64_t>(value);
        else {
            const auto x = parse<double>(value);
            if (option == "--seconds") s.duration_s = x;
            else if (option == "--warmup") {
                s.warmup_s = x;
                specify_warmup = true;
            } else if (option == "--speed-mps") m.speed_mps = x;
            else if (option == "--height-m") m.wave_height_m = x;
            else if (option == "--period-s") m.wave_period_s = x;
            else if (option == "--heading-deg") m.relative_heading_rad = x / to_degrees;
            else if (option == "--load") m.load_ratio = x;
            else if (option == "--cg-m") m.cg_rise_m = x;
            else if (option == "--roll-deg") m.initial_roll_rad = x / to_degrees;
            else if (option == "--manual-deg") s.manual_rad = x / to_degrees;
            else if (option == "--fault-at") m.fault_at_s = x;
            else if (option == "--fault-duration") m.fault_duration_s = x;
            else if (option == "--ack-at") s.ack_at_s = x;
            else throw std::invalid_argument("Unknown option " + std::string(option));
        }
    }
    if (a.directory.empty())
        throw std::invalid_argument("Supply --output DIR");
    if (!specify_warmup && a.scenario.duration_s > 0)
        a.scenario.warmup_s = std::min(15., a.scenario.duration_s / 8.);
    validate(a.scenario);
    return a;
}

void print(const Session &s, double at = 0.)
{
    json result = {{"manifest", s.manifest}, {"metrics", metric_json(measure(s))},
                   {"partial", s.partial}, {"events", s.event_count}};
    if (!s.rows.empty()) {
        const auto &r = s.rows[row_at(s, at)];
        result["frame"] = {{"sim_ns", r.reference.sim_ns}, {"roll_deg", r.reference.roll_rad * to_degrees},
                           {"estimator_deg", r.command.flags & SLX_SB_EST_VALID ?
                                                r.command.estimated_roll_rad * to_degrees : missing},
                           {"port_actual_deg", r.reference.port_actual_rad * to_degrees},
                           {"starboard_actual_deg", r.reference.starboard_actual_rad * to_degrees},
                           {"effective_mode", mode_name(r.command.effective_mode)},
                           {"operator_source", r.order.flags & wire::operator_local ? "panel" : "pc"},
                           {"state", state_name(r.command.state)}, {"cause", cause_name(r.command.cause)}};
        result["frame"]["navigation"] = {{"speed_mps", r.sample.speed_mps},
            {"relative_heading_deg", r.reference.relative_heading_rad * to_degrees},
            {"target_speed_mps", r.navigation.target_speed_mps},
            {"target_heading_deg", r.navigation.target_heading_rad * to_degrees},
            {"panel_revision", r.order.revision}};
        const auto &conditions = r.conditions;
        result["frame"]["conditions"] = {
            {"sea", conditions.sea_kind == SLX_SB_REGULAR ? "regular" : "irregular"},
            {"wave_height_target_m", conditions.wave_height_m}, {"wave_period_target_s", conditions.wave_period_s},
            {"load_ratio", conditions.load_ratio}, {"cg_rise_m", conditions.cg_rise_m},
            {"free_surface_loss_m", conditions.free_surface_loss_m}, {"load_offset_m", conditions.load_offset_m},
            {"actuator_delay_s", conditions.actuator_delay_s}, {"fault", faults.at(r.reference.imposed_fault)},
            {"fault_duration_s", r.fault_duration_s}, {"live_fields", r.live_fields}};
        if (r.reference.stability_flags & SLX_SB_STABILITY_AVAILABLE) {
            const auto &p = r.reference;
            result["frame"]["stability"] = {{"gm_m", p.gm_m}, {"gz_m", p.gz_m},
                {"flags", p.stability_flags}, {"righting_limit_deg", p.positive_range_rad * to_degrees},
                {"flooded_kg", p.flooded_kg}, {"reserve_j", p.reserve_j}, {"kinetic_j", p.kinetic_j}};
        }
    }
    std::cout << result.dump(2) << '\n';
}

void analyze(int argc, char **argv)
{
    if (argc < 5)
        throw std::invalid_argument("Use analyze SESSION --output DIR [--warmup N] [--note TEXT]");
    auto session = replay(path_from(argv[2]));
    fs::path target;
    std::string note;
    for (int i = 3; i < argc; ++i) {
        const std::string_view option = argv[i];
        if (++i == argc)
            throw std::invalid_argument("Missing analysis option value");
        if (option == "--output") target = path_from(argv[i]);
        else if (option == "--warmup") session.scenario.warmup_s = parse<double>(argv[i]);
        else if (option == "--note") note = argv[i];
        else throw std::invalid_argument("Unknown analysis option " + std::string(option));
    }
    if (target.empty())
        throw std::invalid_argument("Supply a new analysis directory");
    validate(session.scenario);
    write_analysis(session, target, note);
    std::cout << read_json(target / "metrics.json").dump(2) << '\n';
}

void campaign(const Arguments &a)
{
    if (a.scenario.timing != wire::Timing::software)
        throw std::invalid_argument("Campaign requires software timing");
    if (!fs::create_directory(a.directory))
        throw std::runtime_error("Choose a new campaign directory");
    struct Case {
        const char *id, *set;
        uint32_t seed_shift, sea, fault;
        double speed, height, period, heading, load, cg;
    };
    constexpr Case matrix[] = {
        {"tuning-beam", "tuning", 1, SLX_SB_REGULAR, 0, 4.7, .65, 5.1, 90, 1., 0.},
        {"tuning-oblique", "tuning", 2, SLX_SB_IRREGULAR, 0, 5.2, .7, 5.7, 65, 1.03, .04},
        {"beam-low-speed", "evaluation", 17, SLX_SB_IRREGULAR, 0, 2.2, .77, 5.5, 90, 1., .05},
        {"fast-following", "evaluation", 31, SLX_SB_IRREGULAR, 0, 7.7, 1.0, 5.8, 141, 1.1, .1},
        {"near-head", "evaluation", 45, SLX_SB_REGULAR, 0, 4.2, .8, 4.5, 18, .9, -.08},
        {"loaded-beam", "evaluation", 56, SLX_SB_IRREGULAR, 0, 5.2, 1.23, 5.5, 90, 1.25, .27},
        {"stationary", "evaluation", 68, SLX_SB_REGULAR, 0, 0, .65, 5.3, 90, 1., 0.},
        {"fin-stuck", "evaluation", 79, SLX_SB_IRREGULAR, SLX_SB_FIN_STUCK, 4.7, 1.0, 5.5, 90, 1., .02},
        {"fin-slow", "evaluation", 80, SLX_SB_IRREGULAR, SLX_SB_FIN_SLOW, 5.0, 1.2, 5.5, 80, 1.1, .1},
        {"limited-fin", "evaluation", 81, SLX_SB_REGULAR, SLX_SB_FIN_LIMITED, 4.4, .88, 5., 90, 1.1, .12},
        {"imu-bias", "evaluation", 91, SLX_SB_REGULAR, SLX_SB_GYRO_BIAS, 4.7, .85, 5.3, 90, 1.1, .04},
        {"imu-stale", "evaluation", 99, SLX_SB_IRREGULAR, SLX_SB_STALE_IMU, 4.7, .78, 5.1, 90, 1., 0.},
    };
    std::vector<Comparison> results;
    results.reserve(std::size(matrix));
    for (const auto &c : matrix) {
        auto s = a.scenario;
        if (s.duration_s == 0)
            s.duration_s = std::max(120., s.warmup_s + 30.);
        s.model.seed += c.seed_shift;
        s.model.sea_kind = c.sea;
        s.model.fault = c.fault;
        s.model.speed_mps = c.speed;
        s.model.wave_height_m = c.height;
        s.model.wave_period_s = c.period;
        s.model.relative_heading_rad = c.heading / to_degrees;
        s.model.load_ratio = c.load;
        s.model.cg_rise_m = c.cg;
        s.model.fault_at_s = std::min(s.model.fault_at_s, s.duration_s * .4);
        s.model.fault_duration_s = c.fault ? s.duration_s * .2 : 0;
        s.ack_at_s = c.fault ? s.duration_s * .86 : -1;
        auto result = compare(s, a.directory / c.id, a.include_slx);
        result.set = c.set;
        results.push_back(std::move(result));
    }
    for (const std::string_view kind : {"parametric", "downflooding", "capsize"}) {
        auto s = scenario_preset(kind);
        s.duration_s = a.scenario.duration_s == 0 ? 120 : a.scenario.duration_s;
        s.warmup_s = a.scenario.warmup_s;
        results.push_back(compare(s, a.directory / kind, a.include_slx));
    }
    comparison_report(a.directory, results);
    std::cout << read_json(a.directory / "comparison.json").dump(2) << '\n';
}
} // namespace
} // namespace slx::sb

int main(int argc, char **argv)
{
    using namespace slx::sb;
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            help();
            return 0;
        }
        const std::string_view command = argv[1];
        if (command == "sample-config") {
            std::cout << configuration(demo_scenario()).dump(2) << '\n';
        } else if (command == "replay") {
            if (argc != 3 && !(argc == 5 && std::string_view(argv[3]) == "--at"))
                throw std::invalid_argument("Use replay DIR [--at SECONDS]");
            print(replay(path_from(argv[2])), argc == 5 ? parse<double>(argv[4]) : 0.);
        } else if (command == "analyze") {
            analyze(argc, argv);
        } else if (command == "run") {
            const auto a = arguments(argc, argv);
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            const auto &config = a.scenario;
            auto s = run(config, a.directory, {[&] {
                return OperatorInput{config.mode, config.manual_rad, false, interrupted != 0};
            }, {}});
            print(s);
            const auto status = s.manifest.value("status", "");
            return status == "complete" || status == "stopped" ? 0 : 2;
        } else if (command == "compare") {
            const auto a = arguments(argc, argv);
            std::vector<Comparison> result;
            result.push_back(compare(a.scenario, a.directory, a.include_slx));
            comparison_report(a.directory, result);
            std::cout << read_json(a.directory / "comparison.json").dump(2) << '\n';
            const auto &c = result.front();
            return c.off.manifest.value("status", "") == "complete" &&
                   c.pid.manifest.value("status", "") == "complete" &&
                   (!c.slx || c.slx->manifest.value("status", "") == "complete") ? 0 : 2;
        } else if (command == "campaign") {
            campaign(arguments(argc, argv));
        } else {
            throw std::invalid_argument("Unknown command " + std::string(command));
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "SeaBalance: " << error.what() << '\n';
        return 1;
    }
}
