// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "session.hpp"

#include "link.hpp"
#include "pid.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace slx::sb {
using namespace std::chrono;
using json = nlohmann::json;

namespace {
void accept_conditions(SLX_SB_LiveConditions &current, const SLX_SB_ModelUpdate &update)
{
    const auto &v = update.conditions;
    if (update.fields & SLX_SB_UPDATE_SEA) {
        current.sea_kind = v.sea_kind;
        current.wave_height_m = v.wave_height_m;
        current.wave_period_s = v.wave_period_s;
    }
    if (update.fields & SLX_SB_UPDATE_LOAD) {
        current.load_ratio = v.load_ratio;
        current.cg_rise_m = v.cg_rise_m;
        current.free_surface_loss_m = v.free_surface_loss_m;
        current.load_offset_m = v.load_offset_m;
    }
    if (update.fields & SLX_SB_UPDATE_DELAY)
        current.actuator_delay_s = v.actuator_delay_s;
}
} // namespace

Session run(const Scenario &s, const fs::path &path, const RunHooks &hooks)
{
    validate(s);
    Recorder record(path, s);
    const auto begin = steady_clock::now();
    const auto steps = static_cast<uint64_t>(std::ceil(s.duration_s / dt - 1e-8));
    const bool network = s.timing != wire::Timing::software;
    const bool paced = !steps || s.timing == wire::Timing::timed || s.pace_software;
    auto manifest = json{{"copyright", copyright}, {"schema", session_schema}, {"source", "synthetic"},
                         {"asset_version", asset_version}, {"software_build", SLX_SB_BUILD_VERSION},
                         {"abi", SLX_SB_ABI_VERSION}, {"model_abi", SLX_SB_MODEL_ABI_VERSION},
                         {"status", "complete"}, {"planned_steps", steps ? json(steps) : json(nullptr)},
                         {"timing", timings[static_cast<size_t>(s.timing)]},
                         {"controller_source", network ? "ethernet" : "local"},
                         {"model_build", nullptr}, {"control_build", nullptr}, {"peer_platform", nullptr},
                         {"panel_operator", false}, {"live_updates", 0}};
    uint64_t n = 0;
    uint64_t live_updates = 0;
    bool link_ok = false;
    std::optional<RemoteControl> remote;
    try {
        const auto dir = executable_directory();
        Model model(core_file(dir, true), s.model);
        manifest["model_build"] = model.build();
        record.record_curve(model.stability_curve());
        std::optional<ControlCore> advanced;
        std::optional<Control> local;
        bool has_advanced{};
        if (network) {
            remote.emplace(s.peer, s.port, s.timing, s.model.actuator_rate_rad_s, milliseconds(s.timeout_ms));
            link_ok = true;
            const auto &peer = remote->peer();
            manifest["control_build"] = peer.build;
            manifest["peer_platform"] = peer.platform;
            manifest["service_boot_id"] = peer.boot_id;
            manifest["session"] = remote->session();
            manifest["panel_operator"] = remote->supports_operator();
            has_advanced = (peer.capabilities & wire::supports_slx) != 0;
        } else {
            if (s.mode == SLX_SB_SLX) {
                const auto core = core_file(dir, false);
                if (!fs::is_regular_file(core))
                    throw std::runtime_error("The SLX strategy requires the corresponding control core");
                advanced.emplace(core);
            }
            has_advanced = advanced.has_value();
            const auto api = advanced ? advanced->api() : reference_api();
            manifest["control_build"] = api.build;
            manifest["peer_platform"] = platform_id();
            manifest["session"] = wire::nonce();
            local.emplace(api, SLX_SB_CAUSE_NONE, s.model.actuator_rate_rad_s);
        }
        if (s.mode == SLX_SB_SLX && !has_advanced)
            throw std::runtime_error("The SLX strategy requires the corresponding control core");
        record.event(0, "host", "session_start", static_cast<uint32_t>(s.timing));
        const auto epoch = steady_clock::now();
        uint32_t previous_fault = SLX_SB_NO_FAULT, previous_cause = SLX_SB_CAUSE_NONE;
        uint32_t previous_requested = s.mode;
        uint32_t previous_effective = SLX_SB_OFF;
        uint32_t previous_state = SLX_SB_READY;
        uint32_t previous_stability = SLX_SB_STABILITY_AVAILABLE;
        bool requested_ack = false;
        wire::OperatorOrder last_order;
        double target_speed = s.model.speed_mps, target_heading = s.model.relative_heading_rad;
        auto conditions = live_conditions(s.model);
        double fault_duration = s.model.fault_duration_s;
        uint32_t applied_fields = 0;
        for (; steps == 0 || n < steps; ++n) {
            if (n >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / (2 * SLX_SB_STEP_NS))
                throw std::runtime_error("Simulated timestamp exceeds the clock representation");
            auto input = hooks.controls ? hooks.controls() : OperatorInput{s.mode, s.manual_rad};
            if (input.stop) {
                manifest["status"] = "stopped";
                record.event(n, "operator", "stop", 0);
                break;
            }
            if (input.mode == SLX_SB_SLX && !has_advanced)
                throw std::runtime_error("The SLX strategy is unavailable on the selected controller");
            if (!network && has_advanced && input.mode == SLX_SB_PID)
                throw std::runtime_error("Choose PID when starting a new software session");
            if (input.mode != previous_requested) {
                record.event(n, "operator", "requested_mode", input.mode);
                previous_requested = input.mode;
            }
            Row row;
            model.observe(row.sample, row.reference);
            row.navigation = {row.reference.relative_heading_rad, target_speed, target_heading};
            row.conditions = conditions;
            row.live_fields = applied_fields;
            row.fault_duration_s = fault_duration;
            const auto changed = previous_stability ^ row.reference.stability_flags;
            if (changed) {
                record.event(n, "stability", "conditions", row.reference.stability_flags);
                if (changed & row.reference.stability_flags & SLX_SB_CAPSIZE_RECORDED)
                    record.event(n, "stability", "capsize", SLX_SB_CAPSIZE_RECORDED);
                previous_stability = row.reference.stability_flags;
            }
            row.sample.mode = input.mode;
            row.sample.manual_rad = input.manual_rad;
            if (row.reference.imposed_fault != previous_fault) {
                record.event(n, "model", row.reference.imposed_fault ? "fault_start" : "fault_end",
                             row.reference.imposed_fault ? row.reference.imposed_fault : previous_fault);
                previous_fault = row.reference.imposed_fault;
            }
            const bool scheduled_ack = !requested_ack && s.ack_at_s >= 0 && n * dt + 1e-9 >= s.ack_at_s;
            if (input.acknowledge || scheduled_ack) {
                row.sample.valid |= SLX_SB_ACK_ALARM;
                requested_ack = requested_ack || scheduled_ack;
                record.event(n, "operator", "ack_request", 0);
            }
            const auto release = epoch + nanoseconds(n * SLX_SB_STEP_NS);
            if (paced) {
                std::this_thread::sleep_until(release);
                row.release_jitter_ms = duration<double, std::milli>(steady_clock::now() - release).count();
            }
            const bool drop = row.reference.imposed_fault == SLX_SB_LINK_LOSS;
            if (remote) {
                link_ok = false;
                row.command = remote->step(row.sample, row.navigation, drop);
                link_ok = true;
                row.rtt_ms = remote->rtt_ms();
                row.order = remote->order();
                if (row.order.revision != last_order.revision) {
                    const auto order = row.order;
                    if ((order.flags ^ last_order.flags) & wire::operator_local)
                        record.event(n, "panel", order.flags & wire::operator_local ? "takes_control" : "returns_control", 0);
                    if ((order.flags & wire::operator_local) && (order.mode != last_order.mode ||
                        !(last_order.flags & wire::operator_local)))
                        record.event(n, "panel", "requested_mode", order.mode);
                    if ((order.flags & wire::operator_navigation) &&
                        (order.speed_mps != target_speed || order.relative_heading_rad != target_heading)) {
                        target_speed = row.navigation.target_speed_mps = order.speed_mps;
                        target_heading = row.navigation.target_heading_rad = order.relative_heading_rad;
                        model.set_navigation(target_speed, target_heading);
                        record.event(n, "panel", "navigation_target", 0);
                    }
                    if (order.flags & wire::operator_mark)
                        record.event(n, "panel", "video_mark", row.command.effective_mode);
                    last_order = order;
                }
            } else {
                if (drop)
                    throw std::runtime_error("Scenario link interruption at sequence " + std::to_string(n));
                row.command = local->step(row.sample);
            }
            row.late = paced && steady_clock::now() > release + nanoseconds(SLX_SB_STEP_NS);
            if (row.command.cause != previous_cause) {
                record.event(n, "controller", row.command.cause ? "alarm" : "alarm_clear",
                             row.command.cause ? row.command.cause : previous_cause);
                previous_cause = row.command.cause;
            }
            if (row.command.state != previous_state) {
                record.event(n, "controller", "state", row.command.state);
                previous_state = row.command.state;
            }
            if (n == 0 || row.command.effective_mode != previous_effective) {
                record.event(n, "controller", "effective_mode", row.command.effective_mode);
                previous_effective = row.command.effective_mode;
            }
            if (row.command.flags & SLX_SB_ALARM_ACKED)
                record.event(n, "controller", "alarm_acknowledged", 0);
            const auto update = hooks.update ? hooks.update() : std::nullopt;
            UpdateReceipt receipt{n + 1, SLX_SB_OK};
            if (update) {
                receipt.panel_has_authority = (row.order.flags & wire::operator_local) &&
                    (update->fields & SLX_SB_UPDATE_NAVIGATION);
                receipt.result = receipt.panel_has_authority ? SLX_SB_E_ARGUMENT :
                    steps && n + 1 >= steps ? SLX_SB_E_SEQUENCE : model.queue_update(*update);
                if (receipt.result != SLX_SB_OK)
                    record.event(n, "operator", "live_update_rejected", static_cast<uint32_t>(-receipt.result));
            }
            model.advance(row.command);
            record.record(row);
            applied_fields = 0;
            if (update && receipt.result == SLX_SB_OK) {
                applied_fields = update->fields;
                accept_conditions(conditions, *update);
                if (update->fields & SLX_SB_UPDATE_NAVIGATION) {
                    target_speed = update->speed_mps;
                    target_heading = update->relative_heading_rad;
                }
                record.event(n + 1, "operator", "live_update", applied_fields);
                if (update->fields & SLX_SB_UPDATE_FAULT) {
                    fault_duration = update->fault ? update->fault_duration_s : 0.;
                    record.event(n + 1, "operator", update->fault ? "fault_injected" : "fault_cancelled", update->fault);
                }
                ++live_updates;
            }
            if (update && hooks.update_result)
                hooks.update_result(receipt);
            if (hooks.progress)
                hooks.progress(row);
        }
        record.event(n, "host", "session_end", 0);
    } catch (const std::exception &error) {
        manifest["status"] = "aborted";
        manifest["reason"] = error.what();
        record.event(n, "host", "aborted", remote && !link_ok ? SLX_SB_CAUSE_LINK : SLX_SB_CAUSE_NONE);
    }
    if (remote && link_ok) {
        try {
            remote->finish();
        } catch (const std::exception &error) {
            manifest["end_ack_error"] = error.what();
            record.event(n, "host", "end_unconfirmed", SLX_SB_CAUSE_LINK);
        }
    }
    manifest["wall_elapsed_s"] = duration<double>(steady_clock::now() - begin).count();
    manifest["live_updates"] = live_updates;
    record.finish(manifest);
    auto session = replay(path);
    write_analysis(session);
    return session;
}

Comparison compare(Scenario scenario, const fs::path &path, bool with_slx)
{
    if (scenario.timing != wire::Timing::software)
        throw std::invalid_argument("Automated A/B uses a fresh local controller for each arm");
    if (!fs::create_directory(path))
        throw std::runtime_error("Choose a new comparison directory");
    if (scenario.duration_s == 0)
        scenario.duration_s = std::max(120., scenario.warmup_s + 30.);
    scenario.pace_software = false;
    Comparison result{};
    result.label = path.filename().string();
    result.set = "evaluation";
    scenario.mode = SLX_SB_OFF;
    result.off = run(scenario, path / "off");
    scenario.mode = SLX_SB_PID;
    result.pid = run(scenario, path / "pid");
    if (with_slx) {
        scenario.mode = SLX_SB_SLX;
        result.slx.emplace(run(scenario, path / "slx"));
    }
    return result;
}
} // namespace slx::sb
