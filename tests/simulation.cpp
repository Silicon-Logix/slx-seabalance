// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "session.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace slx::sb;

void check(bool pass, const char *message)
{
    if (!pass)
        throw std::runtime_error(message);
}

int main()
{
    const auto root = fs::temp_directory_path() / ("slx-sb-test-" + std::to_string(wire::nonce()));
    struct Cleanup {
        const fs::path &folder;
        bool owned{};
        ~Cleanup()
        {
            std::error_code ignored;
            if (owned)
                fs::remove_all(folder, ignored);
        }
    } cleanup{root};
    try {
        cleanup.owned = fs::create_directory(root);
        check(cleanup.owned, "Could not create the test session directory");
        auto c = demo_scenario();
        const auto file = core_file(executable_directory(), true);
        using namespace wire;
        {
            Library lib(file);
            const auto get = reinterpret_cast<SLX_SB_GetModelAPIFn>(lib.symbol("SLX_SB_GetModelAPI"));
            SLX_SB_ModelAPI api{};
            check(get(1, sizeof(api), &api) == SLX_SB_E_ABI &&
                  get(SLX_SB_MODEL_ABI_VERSION, sizeof(api) - 1, &api) == SLX_SB_E_ABI,
                  "Model accepted incompatible ABI");
        }
        for (const double sign : {-1., 1.}) {
            Model model(file, c.model);
            SLX_SB_Sample s{};
            SLX_SB_Reference r{};
            double previous = 0;
            for (uint64_t k = 0; k < 150; ++k) {
                model.observe(s, r);
                check(std::abs(r.port_actual_rad - previous) <= c.model.actuator_rate_rad_s * dt + 1e-9,
                      "Actuator slew exceeded");
                if (k > 35)
                    check(r.fin_moment_nm * sign > 0 && r.port_actual_rad * sign > 0 &&
                          r.starboard_actual_rad * sign < 0, "Fin sign allocation failed");
                previous = r.port_actual_rad;
                auto cmd = abi_struct<SLX_SB_Command>();
                cmd.sequence = k;
                cmd.port_rad = sign * SLX_SB_FIN_LIMIT_RAD;
                cmd.starboard_rad = -cmd.port_rad;
                model.advance(cmd);
            }
            check(std::abs(previous) <= SLX_SB_FIN_LIMIT_RAD, "Fin travel exceeded");
        }
        c.duration_s = 32;
        c.warmup_s = 5;
        const auto pair = compare(c, root / "ab", false);
        check(pair.off.rows.size() == 1600 && pair.pid.rows.size() == pair.off.rows.size(), "Lost run samples");
        for (size_t i = 0; i < pair.off.rows.size(); ++i)
            check(pair.off.rows[i].reference.wave_elevation_m == pair.pid.rows[i].reference.wave_elevation_m,
                  "A/B changed the wave input");
        const auto baseline = measure(pair.off), active = measure(pair.pid);
        check(active.rms_deg < baseline.rms_deg && active.estimate_rms_deg < 3,
              "Default profile PID or estimator regression");
        const auto again = replay(root / "ab/pid");
        const auto i = row_at(again, 10.);
        check(i == 500 && again.rows[i].reference.roll_rad == pair.pid.rows[i].reference.roll_rad,
              "Replay does not preserve original pose");
        const auto incomplete = root / "incomplete";
        fs::create_directory(incomplete);
        fs::copy_file(again.directory / "scenario.json", incomplete / "scenario.json");
        fs::copy_file(again.directory / "samples.csv", incomplete / "samples.csv.partial");
        fs::copy_file(again.directory / "samples.idx", incomplete / "samples.idx.partial");
        fs::copy_file(again.directory / "events.csv", incomplete / "events.csv.partial");
        save_json(incomplete / "manifest.pending.json",
                  {{"copyright", copyright}, {"schema", session_schema}, {"source", "synthetic"}, {"status", "recording"}});
        const auto partial = incomplete / "samples.csv.partial";
        fs::resize_file(partial, fs::file_size(partial) - 4);
        const auto recovered = replay(incomplete);
        check(recovered.partial && recovered.rows.size() == again.rows.size() - 1,
              "Partial replay did not retain the complete prefix");

        c.model.wave_height_m = 0;
        c.model.initial_roll_rad = .19;
        c.mode = SLX_SB_OFF;
        const auto free = run(c, root / "free");
        check(std::abs(free.rows.back().reference.roll_rad) < .01 &&
              std::isfinite(measure(free).settling_s), "Free roll did not decay");

        c.model.fault = SLX_SB_MISSING_IMU;
        c.model.fault_at_s = 4;
        c.model.fault_duration_s = 2;
        c.ack_at_s = 8;
        c.mode = SLX_SB_PID;
        const auto fault = run(c, root / "fault");
        check(fault.rows[200].command.cause == SLX_SB_CAUSE_IMU &&
              !(fault.rows[200].command.flags & SLX_SB_EST_VALID), "Missing data fabricated an estimate");
        check(fault.rows[350].command.state == SLX_SB_AWAIT_ACK &&
              (fault.rows[400].command.flags & SLX_SB_ALARM_ACKED), "Fault recovery lost context");
        c.model.fault = SLX_SB_LINK_LOSS;
        c.ack_at_s = -1;
        const auto cut = run(c, root / "link-cut");
        check(cut.rows.size() == 200 && cut.manifest.at("status") == "aborted",
              "Lockstep simulated past missing command");
        auto critical = scenario_preset("capsize");
        critical.duration_s = 65;
        critical.warmup_s = 0;
        critical.mode = SLX_SB_OFF;
        const auto storm = run(critical, root / "critical");
        const auto risk = measure(storm);
        check(storm.rows.size() == 3250 && std::isfinite(risk.first_righting_loss_s) &&
                  std::isfinite(risk.first_capsize_s) && risk.peak_flooded_kg > 0 &&
                  (storm.rows.back().reference.stability_flags & SLX_SB_CAPSIZE_RECORDED),
              "Critical scenario did not record sustained inversion and continue");
        auto continuous = demo_scenario();
        size_t frames = 0;
        const auto stop = run(continuous, root / "continuous", {
            [&] { return OperatorInput{SLX_SB_PID, 0, false, frames >= 35}; },
            [&](const Row &) { ++frames; }
        });
        check(stop.rows.size() == 35 && stop.manifest.at("status") == "stopped" &&
                  stop.manifest.at("planned_steps").is_null(), "The continuous session did not stop on request");
        auto edited = demo_scenario();
        edited.duration_s = 8.;
        edited.warmup_s = 0.;
        size_t live_tick = 0;
        std::vector<UpdateReceipt> receipts;
        RunHooks edits;
        edits.progress = [&](const Row &) { ++live_tick; };
        edits.update = [&]() -> std::optional<SLX_SB_ModelUpdate> {
            auto update = abi_struct<SLX_SB_ModelUpdate>();
            switch (live_tick) {
            case 20:
                update.fields = SLX_SB_UPDATE_FAULT;
                update.fault = SLX_SB_MISSING_IMU;
                update.fault_duration_s = .4;
                break;
            case 80:
                update.fields = SLX_SB_UPDATE_FAULT;
                update.fault = SLX_SB_FIN_STUCK;
                break;
            case 110:
                update.fields = SLX_SB_UPDATE_SEA | SLX_SB_UPDATE_LOAD |
                                SLX_SB_UPDATE_DELAY | SLX_SB_UPDATE_NAVIGATION;
                update.conditions = {SLX_SB_REGULAR, 1.4, 7., 1.3, .25, .1, .1, .25};
                update.speed_mps = 3.3;
                update.relative_heading_rad = 1.1;
                break;
            case 140:
                update.fields = SLX_SB_UPDATE_SEA | SLX_SB_UPDATE_LOAD;
                update.conditions = {SLX_SB_REGULAR, 9., 7., 1.5, .25, .1, .1, .25};
                break;
            case 180:
                update.fields = SLX_SB_UPDATE_FAULT;
                break;
            default:
                return std::nullopt;
            }
            return update;
        };
        edits.update_result = [&](UpdateReceipt r) { receipts.push_back(r); };
        const auto live = run(edited, root / "live", edits);
        check(live.manifest.at("status") == "complete" && live.rows.size() == 400 &&
              live.manifest.at("live_updates") == 4 && receipts.size() == 5 &&
              receipts[2].sequence == 111 && receipts[2].result == SLX_SB_OK &&
              receipts[3].result == SLX_SB_E_RANGE, "Live request interrupted or failed to acknowledge a session");
        check(live.rows[20].reference.imposed_fault == SLX_SB_NO_FAULT &&
              live.rows[21].reference.imposed_fault == SLX_SB_MISSING_IMU &&
              live.rows[21].command.cause == SLX_SB_CAUSE_IMU &&
              live.rows[40].reference.imposed_fault == SLX_SB_MISSING_IMU &&
              live.rows[41].reference.imposed_fault == SLX_SB_NO_FAULT, "Live fault did not last the requested steps");
        check(live.rows[81].reference.imposed_fault == SLX_SB_FIN_STUCK &&
              live.rows[180].reference.imposed_fault == SLX_SB_FIN_STUCK &&
              live.rows[181].reference.imposed_fault == SLX_SB_NO_FAULT, "Held fault was not cancelled");
        const auto before = live.rows[110], after = live.rows[111], tail = live.rows.back();
        check(before.conditions.wave_height_m == edited.model.wave_height_m &&
              after.conditions.wave_height_m == 1.4 && after.conditions.load_ratio == 1.3 &&
              after.conditions.actuator_delay_s == .25 && tail.conditions.load_ratio == 1.3 &&
              after.navigation.target_speed_mps == 3.3 && std::abs(tail.sample.speed_mps - 3.3) < .001 &&
              std::abs(tail.reference.relative_heading_rad - 1.1) < .001 &&
              std::abs(after.reference.gm_m - before.reference.gm_m) > .1 &&
              std::abs(after.reference.roll_rad - before.reference.roll_rad) < .045 &&
              std::abs(after.reference.wave_elevation_m - before.reference.wave_elevation_m) < .07,
              "Live conditions did not change physics continuously or did not survive playback");
        const auto model_path = core_file(executable_directory(), true);
        Library model_library(model_path);
        const auto api = reinterpret_cast<SLX_SB_GetModelAPIFn>(model_library.symbol("SLX_SB_GetModelAPI"));
        constexpr auto v2_size = offsetof(SLX_SB_ModelAPI, queue_update);
        SLX_SB_ModelAPI table{};
        check(api(SLX_SB_MODEL_ABI_VERSION, v2_size, &table) == SLX_SB_OK && table.size == v2_size &&
              table.set_navigation && !table.queue_update, "ABI 2 model table prefix no longer works");
        std::cout << "Fin travel, stability, capsize, continuous mode, recovery and indexed replay passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
