// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include "scenario.hpp"

#include <fstream>
#include <functional>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace slx::sb {
inline constexpr double missing = std::numeric_limits<double>::quiet_NaN();
inline constexpr unsigned session_schema = 4;
struct Row {
    SLX_SB_Sample sample{};
    SLX_SB_Reference reference{};
    SLX_SB_Command command{};
    wire::OperatorOrder order;
    wire::Navigation navigation;
    SLX_SB_LiveConditions conditions{};
    uint32_t live_fields{};
    double fault_duration_s{};
    double rtt_ms{missing}, release_jitter_ms{missing};
    bool late{};
};
struct Event {
    uint64_t sequence{}, sim_ns{};
    std::string origin, name;
    uint32_t code{};
};

class SampleSeries {
public:
    SampleSeries() = default;
    SampleSeries(const fs::path &samples, const fs::path &index, unsigned schema, bool partial,
                 const SLX_SB_ModelConfig &initial);
    explicit SampleSeries(std::vector<Row> legacy);
    size_t size() const;
    bool empty() const { return size() == 0; }
    Row operator[](size_t i) const;
    Row direct(size_t i) const;
    Row back() const { return (*this)[size() - 1]; }

private:
    struct Store;
    std::shared_ptr<Store> store_;
};

struct Session {
    fs::path directory;
    Scenario scenario;
    nlohmann::json manifest;
    SampleSeries rows;
    std::deque<Event> events;
    size_t event_count{};
    SLX_SB_StabilityCurve curve{};
    bool partial{};
};

nlohmann::json read_json(const fs::path &);
void save_json(const fs::path &, const nlohmann::json &);
Session replay(const fs::path &);
size_t row_at(const Session &, double seconds);

class Recorder {
public:
    Recorder(const fs::path &, const Scenario &);
    void record(const Row &);
    void event(uint64_t sequence, std::string_view origin, std::string_view name, uint32_t code);
    void record_curve(const SLX_SB_StabilityCurve &) const;
    void finish(nlohmann::json manifest);

private:
    fs::path path_;
    std::ofstream samples_, index_, events_;
    size_t count_{};
};

struct OperatorInput {
    uint32_t mode{};
    double manual_rad{};
    bool acknowledge{}, stop{};
};
struct UpdateReceipt {
    uint64_t sequence{};
    SLX_SB_Result result{};
    bool panel_has_authority{};
};
struct RunHooks {
    std::function<OperatorInput()> controls{};
    std::function<void(const Row &)> progress{};
    std::function<std::optional<SLX_SB_ModelUpdate>()> update{};
    std::function<void(UpdateReceipt)> update_result{};
};
Session run(const Scenario &, const fs::path &directory, const RunHooks & = {});

struct Metrics {
    size_t count{}, estimate_count{}, invalid_imu{}, accel_rejected{}, deadline_missed{};
    double rms_deg{missing}, peak_deg{missing}, estimate_rms_deg{missing};
    double estimate_lag_s{missing}, activity{missing}, saturation_s{missing}, settling_s{missing};
    double rtt_p95_ms{missing}, rtt_max_ms{missing}, release_p95_ms{missing};
    double first_downflood_s{missing}, first_righting_loss_s{missing}, first_capsize_s{missing};
    double min_gm_m{missing}, peak_flooded_kg{missing}, remaining_reserve_min_kj{missing};
};
Metrics measure(const Session &);
nlohmann::json metric_json(const Metrics &);
double improvement(double baseline, double controlled);
void write_analysis(const Session &, const fs::path &output = {}, std::string_view note = {});
struct Comparison {
    std::string label, set;
    Session off, pid;
    std::optional<Session> slx;
};
Comparison compare(Scenario, const fs::path &, bool with_slx);
void comparison_report(const fs::path &, const std::vector<Comparison> &);
} // namespace slx::sb
