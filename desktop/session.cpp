// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "session.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <stdexcept>

namespace slx::sb {
using json = nlohmann::json;
namespace {
constexpr std::string_view notice = "# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX) / SLX SeaBalance";
constexpr std::string_view csv_header =
    "sequence,sim_ns,imu_ns,sensor_flags,requested_mode,ax_mps2,ay_mps2,az_mps2,gx_rad_s,gy_rad_s,gz_rad_s,"
    "speed_mps,port_feedback_rad,stbd_feedback_rad,manual_rad,roll_ref_rad,rate_ref_rad_s,port_actual_rad,"
    "stbd_actual_rad,wave_moment_nm,fin_moment_nm,wave_elevation_m,imposed_fault,limited_fins,"
    "state,cause,command_flags,effective_mode,port_command_rad,stbd_command_rad,roll_est_rad,rate_est_rad_s,"
    "rtt_ms,release_jitter_ms,deadline_missed";
constexpr std::string_view event_header = "sequence,sim_ns,origin,event,code";
constexpr std::string_view stability_columns =
    ",stability_flags,righting_moment_nm,damping_moment_nm,heel_moment_nm,roll_accel_rad_s2,"
    "roll_unwrapped_rad,gm_m,gz_m,positive_range_rad,downflood_rad,flooded_kg,reserve_j,kinetic_j,"
    "port_authority_0_1,starboard_authority_0_1";
constexpr std::string_view navigation_columns =
    ",relative_heading_rad,yaw_from_start_rad,target_speed_mps,target_heading_rad,"
    "panel_revision,panel_flags,panel_mode,panel_manual_rad,panel_speed_mps,panel_heading_rad";
constexpr std::string_view live_columns =
    ",live_fields,sea_kind,wave_height_target_m,wave_period_target_s,load_ratio,cg_rise_m,"
    "free_surface_loss_m,load_offset_m,actuator_delay_s,fault_duration_s";
constexpr std::string_view index_header =
    "# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX) / SLX SeaBalance\n"
    "SLX CSV OFFSETS 2\n";
constexpr size_t index_width = 21;
constexpr size_t page_size = 128;
constexpr size_t legacy_max_rows = 30000;

void require_header(std::istream &in, std::string_view header)
{
    std::string line;
    if (!std::getline(in, line) || line != notice ||
        !std::getline(in, line) || line != header)
        throw std::runtime_error("Incompatible SeaBalance CSV schema");
}

template <size_t N> std::array<std::string_view, N> split(std::string_view line)
{
    std::array<std::string_view, N> result;
    size_t start = 0;
    for (size_t i = 0; i < N; ++i) {
        const auto comma = line.find(',', start);
        if ((i == N - 1) != (comma == std::string_view::npos))
            throw std::runtime_error("Unexpected CSV column count");
        result[i] = line.substr(start, comma == std::string_view::npos ? comma : comma - start);
        start = comma + 1;
    }
    return result;
}

template <class T> T number(std::string_view field)
{
    T value{};
    const auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
    if (ec != std::errc{} || end != field.data() + field.size())
        throw std::runtime_error("Invalid CSV value");
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value))
            throw std::runtime_error("Non-finite CSV value");
    }
    return value;
}

bool has_imu(uint32_t flags)
{
    return flags & (SLX_SB_ACCEL_VALID | SLX_SB_GYRO_VALID | SLX_SB_ACCEL_CLIPPED | SLX_SB_GYRO_CLIPPED);
}

Row sample_row(std::string_view line, uint64_t index, unsigned version, const SLX_SB_ModelConfig &initial)
{
    auto fields = std::array<std::string_view, 70>{};
    if (version >= 4) {
        fields = split<70>(line);
    } else if (version == 3) {
        const auto old = split<60>(line);
        std::copy(old.begin(), old.end(), fields.begin());
    } else if (version == 2) {
        const auto old = split<50>(line);
        std::copy(old.begin(), old.end(), fields.begin());
    } else {
        const auto old = split<35>(line);
        std::copy(old.begin(), old.end(), fields.begin());
    }
    size_t i = 0;
    auto u64 = [&] { return number<uint64_t>(fields[i++]); };
    auto u32 = [&] { return number<uint32_t>(fields[i++]); };
    auto real = [&] { return number<double>(fields[i++]); };
    auto optional = [&] {
        const auto value = fields[i++];
        return value.empty() ? missing : number<double>(value);
    };
    Row row;
    auto &s = row.sample;
    auto &r = row.reference;
    auto &c = row.command;
    s = abi_struct<SLX_SB_Sample>();
    r = abi_struct<SLX_SB_Reference>();
    c = abi_struct<SLX_SB_Command>();
    s.sequence = r.sequence = c.sequence = u64();
    s.sim_ns = r.sim_ns = u64();
    const auto imu_ns = fields[i++];
    s.valid = u32();
    s.mode = u32();
    if (has_imu(s.valid))
        s.imu_ns = number<uint64_t>(imu_ns);
    else if (!imu_ns.empty())
        throw std::runtime_error("Inconsistent missing IMU timestamp");
    for (auto *vector : {s.accel_mps2, s.gyro_rad_s}) {
        for (int axis = 0; axis < 3; ++axis) {
            if (has_imu(s.valid))
                vector[axis] = real();
            else if (!fields[i++].empty())
                throw std::runtime_error("Inconsistent missing IMU channel");
        }
    }
    s.speed_mps = real(); s.port_feedback_rad = real(); s.starboard_feedback_rad = real();
    s.manual_rad = real(); r.roll_rad = real(); r.roll_rate_rad_s = real();
    r.port_actual_rad = real(); r.starboard_actual_rad = real(); r.wave_moment_nm = real();
    r.fin_moment_nm = real(); r.wave_elevation_m = real(); r.imposed_fault = u32(); r.limited_fins = u32();
    c.state = u32(); c.cause = u32(); c.flags = u32(); c.effective_mode = u32();
    c.port_rad = real(); c.starboard_rad = real();
    if (c.flags & SLX_SB_EST_VALID) {
        c.estimated_roll_rad = real(); c.estimated_rate_rad_s = real();
    } else if (!fields[i++].empty() || !fields[i++].empty()) {
        throw std::runtime_error("Inconsistent missing estimate");
    }
    row.rtt_ms = optional();
    row.release_jitter_ms = optional();
    const auto late = u32();
    row.late = late != 0;
    r.roll_unwrapped_rad = r.roll_rad;
    if (version >= 2) {
        r.stability_flags = u32();
        r.righting_moment_nm = real(); r.damping_moment_nm = real(); r.heel_moment_nm = real();
        r.roll_accel_rad_s2 = real(); r.roll_unwrapped_rad = real(); r.gm_m = real();
        r.gz_m = real(); r.positive_range_rad = real(); r.downflood_rad = real();
        r.flooded_kg = real(); r.reserve_j = real(); r.kinetic_j = real();
        r.port_authority = real(); r.starboard_authority = real();
        if (r.stability_flags > 63 || r.positive_range_rad < 0 || r.positive_range_rad > std::numbers::pi ||
            r.flooded_kg < 0 || r.reserve_j < 0 || r.kinetic_j < 0 ||
            r.port_authority < 0 || r.port_authority > 1 || r.starboard_authority < 0 || r.starboard_authority > 1)
            throw std::runtime_error("Invalid hydrostatic CSV fields");
    }
    if (version >= 3) {
        r.relative_heading_rad = real();
        r.yaw_from_start_rad = real();
        row.navigation = {r.relative_heading_rad, real(), real()};
        row.order = {u64(), u32(), u32(), real(), real(), real()};
        if (std::max({std::abs(r.relative_heading_rad), std::abs(r.yaw_from_start_rad),
                      std::abs(row.navigation.target_heading_rad)}) > std::numbers::pi ||
            row.navigation.target_speed_mps < 0. || row.navigation.target_speed_mps > 9. ||
            !wire::valid_order(row.order))
            throw std::runtime_error("Invalid navigation CSV fields");
    } else {
        r.relative_heading_rad = initial.relative_heading_rad;
        row.navigation = {initial.relative_heading_rad, s.speed_mps, initial.relative_heading_rad};
    }
    row.conditions = live_conditions(initial);
    row.fault_duration_s = initial.fault_duration_s;
    if (version >= 4) {
        row.live_fields = u32();
        row.conditions = {u32(), real(), real(), real(), real(), real(), real(), real()};
        row.fault_duration_s = real();
        const auto &v = row.conditions;
        if (row.live_fields > SLX_SB_UPDATE_MASK || v.sea_kind > SLX_SB_IRREGULAR ||
            v.wave_height_m < 0. || v.wave_height_m > 7. || v.wave_period_s < 2. || v.wave_period_s > 20. ||
            v.load_ratio < .65 || v.load_ratio > 1.65 || v.cg_rise_m < -.2 || v.cg_rise_m > .9 ||
            v.free_surface_loss_m < 0. || v.free_surface_loss_m > .9 || std::abs(v.load_offset_m) > .7 ||
            v.actuator_delay_s < 0. || v.actuator_delay_s > 1. || row.fault_duration_s < 0.)
            throw std::runtime_error("Invalid live settings CSV fields");
    }
    if (s.sequence != index || s.sim_ns != index * SLX_SB_STEP_NS || s.imu_ns > s.sim_ns ||
        s.mode >= modes.size() || c.effective_mode >= modes.size() || r.imposed_fault >= faults.size() ||
        c.state > SLX_SB_AWAIT_ACK || c.cause > SLX_SB_CAUSE_RESTART || r.limited_fins > 3 ||
        late > 1 || (std::isfinite(row.rtt_ms) && row.rtt_ms < 0) ||
        std::max(std::abs(r.port_actual_rad), std::abs(r.starboard_actual_rad)) >
                    SLX_SB_FIN_LIMIT_RAD + 1e-9)
        throw std::runtime_error("CSV sample sequence, timestamp or enum is out of range");
    return row;
}

template <typename Function>
void read_rows(const fs::path &path, std::string_view header, size_t limit, bool partial, Function consume)
{
    std::ifstream in(path, std::ios::binary);
    require_header(in, header);
    std::string line;
    size_t count = 0;
    while (std::getline(in, line)) {
        if (in.eof() && partial)
            break; // Ignore a truncated last record after process interruption.
        if (in.eof() || ++count > limit || line.size() > 2048)
            throw std::runtime_error("Incomplete or oversized session stream");
        consume(line);
    }
    if (in.bad())
        throw std::runtime_error("Cannot read session stream");
}
} // namespace

struct SampleSeries::Store {
    std::ifstream samples, offsets;
    std::vector<Row> legacy;
    std::array<Row, page_size> page{};
    size_t count{}, page_start{}, page_count{};
    bool indexed{};
    unsigned version{};
    SLX_SB_ModelConfig initial{};
    uint64_t bytes{}, header_end{};

    void seek(size_t i)
    {
        offsets.clear();
        offsets.seekg(static_cast<std::streamoff>(index_header.size() + i * index_width));
        std::array<char, index_width> field{};
        if (!offsets.read(field.data(), field.size()) || field.back() != '\n')
            throw std::runtime_error("Invalid CSV index entry");
        const auto offset = number<uint64_t>({field.data(), index_width - 1});
        if (offset < header_end || offset >= bytes)
            throw std::runtime_error("CSV index outside original data");
        samples.clear();
        samples.seekg(static_cast<std::streamoff>(offset));
    }

    Row next(size_t i)
    {
        std::string line;
        if (!std::getline(samples, line) || samples.eof() || line.size() > 2048)
            throw std::runtime_error("Incomplete indexed sample");
        return sample_row(line, i, version, initial);
    }
};

SampleSeries::SampleSeries(const fs::path &data, const fs::path &index, unsigned version, bool partial,
                           const SLX_SB_ModelConfig &initial)
    : store_(std::make_shared<Store>())
{
    auto &s = *store_;
    s.indexed = true;
    s.version = version;
    s.initial = initial;
    s.samples.open(data, std::ios::binary);
    std::string header_line = std::string(csv_header) + std::string(stability_columns);
    if (version >= 3)
        header_line += navigation_columns;
    if (version >= 4)
        header_line += live_columns;
    require_header(s.samples, header_line);
    s.header_end = static_cast<uint64_t>(s.samples.tellg());
    s.bytes = fs::file_size(data);
    s.offsets.open(index, std::ios::binary);
    std::string header(index_header.size(), '\0');
    if (!s.offsets.read(header.data(), static_cast<std::streamsize>(header.size())) || header != index_header)
        throw std::runtime_error("Incompatible or missing SeaBalance CSV index");
    const auto index_bytes = fs::file_size(index) - index_header.size();
    if (index_bytes % index_width != 0 && !partial)
        throw std::runtime_error("Truncated sample index");
    s.count = static_cast<size_t>(index_bytes / index_width);
    if (s.count > s.bytes / 20)
        throw std::runtime_error("Sample index count exceeds possible CSV entries");
    while (s.count) {
        try {
            s.seek(s.count - 1);
            s.next(s.count - 1);
            break;
        } catch (const std::runtime_error &) {
            if (!partial)
                throw;
            --s.count;
        }
    }
}

SampleSeries::SampleSeries(std::vector<Row> legacy) : store_(std::make_shared<Store>())
{
    store_->count = legacy.size();
    store_->legacy = std::move(legacy);
}

size_t SampleSeries::size() const { return store_ ? store_->count : 0; }

Row SampleSeries::direct(size_t i) const
{
    if (i >= size())
        throw std::out_of_range("Sample index");
    if (!store_->indexed)
        return store_->legacy[i];
    store_->seek(i);
    return store_->next(i);
}

Row SampleSeries::operator[](size_t i) const
{
    if (i >= size())
        throw std::out_of_range("Sample index");
    auto &s = *store_;
    if (!s.indexed)
        return s.legacy[i];
    if (i < s.page_start || i - s.page_start >= s.page_count) {
        s.page_start = i / page_size * page_size;
        s.page_count = 0;
        s.seek(s.page_start);
        const size_t count = std::min(page_size, s.count - s.page_start);
        for (size_t j = 0; j < count; ++j)
            s.page[j] = s.next(s.page_start + j);
        s.page_count = count;
    }
    return s.page[i - s.page_start];
}

json read_json(const fs::path &path)
{
    if (fs::file_size(path) > 1024 * 1024)
        throw std::runtime_error("Metadata file exceeds 1 MiB");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open " + path.filename().string());
    return json::parse(in);
}

void save_json(const fs::path &path, const json &j)
{
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << j.dump(2) << '\n';
    out.close();
}

Recorder::Recorder(const fs::path &path, const Scenario &scenario) : path_(path)
{
    if (path.empty() || !fs::create_directory(path))
        throw std::runtime_error("Choose a new session directory");
    save_json(path / "scenario.json", configuration(scenario));
    save_json(path / "manifest.pending.json",
              {{"copyright", copyright}, {"schema", session_schema}, {"status", "recording"},
               {"source", "synthetic"}, {"asset_version", asset_version}});
    samples_.exceptions(std::ios::badbit | std::ios::failbit);
    index_.exceptions(std::ios::badbit | std::ios::failbit);
    events_.exceptions(std::ios::badbit | std::ios::failbit);
    samples_.open(path / "samples.csv.partial", std::ios::binary);
    index_.open(path / "samples.idx.partial", std::ios::binary);
    events_.open(path / "events.csv.partial", std::ios::binary);
    samples_.imbue(std::locale::classic());
    events_.imbue(std::locale::classic());
    index_.imbue(std::locale::classic());
    index_ << index_header << std::setfill('0');
    samples_ << std::setprecision(std::numeric_limits<double>::max_digits10)
             << notice << '\n' << csv_header << stability_columns << navigation_columns << live_columns << '\n';
    events_ << notice << '\n' << event_header << '\n';
    samples_.flush();
    index_.flush();
    events_.flush();
}

void Recorder::record(const Row &row)
{
    const auto &s = row.sample;
    const auto &r = row.reference;
    const auto &c = row.command;
    auto &out = samples_;
    const auto offset = static_cast<uint64_t>(out.tellp());
    out << s.sequence << ',' << s.sim_ns << ',';
    const bool have_imu = has_imu(s.valid);
    if (have_imu)
        out << s.imu_ns;
    out << ',' << s.valid << ',' << s.mode;
    for (auto *vector : {s.accel_mps2, s.gyro_rad_s}) {
        for (int i = 0; i < 3; ++i) {
            out << ',';
            if (have_imu)
                out << vector[i];
        }
    }
    out << ',' << s.speed_mps << ',' << s.port_feedback_rad << ',' << s.starboard_feedback_rad
        << ',' << s.manual_rad << ',' << r.roll_rad << ',' << r.roll_rate_rad_s
        << ',' << r.port_actual_rad << ',' << r.starboard_actual_rad << ',' << r.wave_moment_nm
        << ',' << r.fin_moment_nm << ',' << r.wave_elevation_m << ',' << r.imposed_fault << ',' << r.limited_fins
        << ',' << c.state << ',' << c.cause << ',' << c.flags << ',' << c.effective_mode << ','
        << c.port_rad << ',' << c.starboard_rad << ',';
    if (c.flags & SLX_SB_EST_VALID)
        out << c.estimated_roll_rad << ',' << c.estimated_rate_rad_s;
    else
        out << ',';
    out << ',';
    if (std::isfinite(row.rtt_ms))
        out << row.rtt_ms;
    out << ',';
    if (std::isfinite(row.release_jitter_ms))
        out << row.release_jitter_ms;
    out << ',' << row.late << ',' << r.stability_flags << ',' << r.righting_moment_nm << ','
        << r.damping_moment_nm << ',' << r.heel_moment_nm << ',' << r.roll_accel_rad_s2 << ','
        << r.roll_unwrapped_rad << ',' << r.gm_m << ',' << r.gz_m << ',' << r.positive_range_rad << ','
        << r.downflood_rad << ',' << r.flooded_kg << ',' << r.reserve_j << ',' << r.kinetic_j << ','
        << r.port_authority << ',' << r.starboard_authority << ','
        << r.relative_heading_rad << ',' << r.yaw_from_start_rad << ','
        << row.navigation.target_speed_mps << ',' << row.navigation.target_heading_rad << ','
        << row.order.revision << ',' << row.order.flags << ',' << row.order.mode << ','
        << row.order.manual_rad << ',' << row.order.speed_mps << ',' << row.order.relative_heading_rad;
    const auto &v = row.conditions;
    out << ',' << row.live_fields << ',' << v.sea_kind << ',' << v.wave_height_m << ',' << v.wave_period_s
        << ',' << v.load_ratio << ',' << v.cg_rise_m << ',' << v.free_surface_loss_m << ',' << v.load_offset_m
        << ',' << v.actuator_delay_s << ',' << row.fault_duration_s << '\n';
    index_ << std::setw(index_width - 1) << offset << '\n';
    if (++count_ % 50 == 0) {
        out.flush();
        index_.flush();
    }
}

void Recorder::event(uint64_t sequence, std::string_view origin, std::string_view name, uint32_t code)
{
    events_ << sequence << ',' << sequence * SLX_SB_STEP_NS << ',' << origin << ',' << name
            << ',' << code << '\n';
    events_.flush();
}

void Recorder::finish(json manifest)
{
    samples_.close();
    index_.close();
    events_.close();
    fs::rename(path_ / "samples.csv.partial", path_ / "samples.csv");
    fs::rename(path_ / "samples.idx.partial", path_ / "samples.idx");
    fs::rename(path_ / "events.csv.partial", path_ / "events.csv");
    manifest["rows"] = count_;
    save_json(path_ / "manifest.json.partial", manifest);
    fs::rename(path_ / "manifest.json.partial", path_ / "manifest.json");
    std::error_code ignored;
    fs::remove(path_ / "manifest.pending.json", ignored);
}

void Recorder::record_curve(const SLX_SB_StabilityCurve &curve) const
{
    json points = json::array();
    for (uint32_t i = 0; i < curve.count; ++i)
        points.push_back({curve.points[i].heel_rad, curve.points[i].arm_m});
    save_json(path_ / "initial-gz.json", {{"copyright", copyright}, {"schema", 1}, {"points_rad_m", points}});
}

Session replay(const fs::path &path)
{
    Session session;
    session.directory = path;
    session.scenario = scenario_from(read_json(path / "scenario.json"));
    session.partial = !fs::exists(path / "manifest.json");
    session.manifest = read_json(path / (session.partial ? "manifest.pending.json" : "manifest.json"));
    const auto version = session.manifest.at("schema").get<unsigned>();
    if (version < 1 || version > session_schema || session.manifest.at("source") != "synthetic")
        throw std::runtime_error("Unsupported session format or provenance");
    auto stream_path = [&](const char *name) {
        const auto full = path / name;
        return session.partial && !fs::exists(full) ? path / (std::string(name) + ".partial") : full;
    };
    const auto &initial = session.scenario.model;
    if (version >= 2) {
        session.rows = SampleSeries(stream_path("samples.csv"), stream_path("samples.idx"), version,
                                    session.partial, initial);
        if (fs::is_regular_file(path / "initial-gz.json")) {
            const auto gz = read_json(path / "initial-gz.json");
            const auto &points = gz.at("points_rad_m");
            if (gz.at("schema") != 1 || !points.is_array() || points.size() != SLX_SB_GZ_SAMPLES)
                throw std::runtime_error("Invalid recorded GZ curve");
            session.curve = abi_struct<SLX_SB_StabilityCurve>();
            for (const auto &p : points) {
                auto &point = session.curve.points[session.curve.count++];
                point = {p.at(0).get<double>(), p.at(1).get<double>()};
                if (!std::isfinite(point.heel_rad) || !std::isfinite(point.arm_m))
                    throw std::runtime_error("Non-finite GZ curve");
            }
        }
    } else {
        std::vector<Row> old;
        read_rows(stream_path("samples.csv"), csv_header, legacy_max_rows, session.partial, [&](const auto &line) {
            old.push_back(sample_row(line, old.size(), version, initial));
        });
        session.rows = SampleSeries(std::move(old));
    }
    read_rows(stream_path("events.csv"), event_header, std::numeric_limits<size_t>::max(), session.partial, [&](const auto &line) {
        const auto v = split<5>(line);
        const auto sequence = number<uint64_t>(v[0]);
        const auto sim_ns = number<uint64_t>(v[1]);
        if (sequence > UINT64_MAX / SLX_SB_STEP_NS || sim_ns != sequence * SLX_SB_STEP_NS ||
            (!session.events.empty() && sequence < session.events.back().sequence))
            throw std::runtime_error("Invalid event time in playback");
        ++session.event_count;
        if (session.events.size() == 2048)
            session.events.pop_front();
        session.events.push_back({sequence, sim_ns, std::string(v[2]), std::string(v[3]), number<uint32_t>(v[4])});
    });
    if (!session.partial && session.manifest.at("rows") != session.rows.size())
        throw std::runtime_error("Manifest row count does not match session CSV");
    return session;
}

size_t row_at(const Session &session, double seconds)
{
    if (session.rows.empty() || seconds <= 0 || !std::isfinite(seconds))
        return 0;
    const auto index = std::min(std::floor(seconds / dt + 1e-8),
                                static_cast<double>(session.rows.size() - 1));
    return static_cast<size_t>(index);
}
} // namespace slx::sb
