// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "panel_state.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace slx::sb {
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {
void write_json(const fs::path &path, const json &data)
{
    auto temp = path;
    temp += ".partial";
    std::ofstream out(temp, std::ios::trunc | std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << data.dump() << '\n';
    out.close();
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        throw std::runtime_error("Cannot replace panel status");
#else
    fs::rename(temp, path);
#endif
}

json snapshot(const PanelState &p)
{
    return {{"copyright", "Copyright (c) 2026 Marco Pezzullo (Silicon LogiX)"},
            {"version", 2}, {"connected", p.connected}, {"sampled", p.sampled},
            {"operator_enabled", p.operator_enabled}, {"has_slx", p.has_slx},
            {"source", "synthetic"}, {"timing", static_cast<uint32_t>(p.timing)},
            {"boot", p.boot}, {"session", p.session}, {"updated_ns", p.updated_ns},
            {"sequence", p.sequence}, {"invalid_frames", p.invalid_frames},
            {"last_alarm", p.last_alarm}, {"cause", p.command.cause}, {"state", p.command.state},
            {"flags", p.command.flags}, {"requested_mode", p.sample.mode},
            {"mode", p.command.effective_mode}, {"speed_mps", p.sample.speed_mps},
            {"roll_rad", p.command.estimated_roll_rad}, {"roll_rate_rad_s", p.command.estimated_rate_rad_s},
            {"port_cmd_rad", p.command.port_rad}, {"starboard_cmd_rad", p.command.starboard_rad},
            {"port_rad", p.sample.port_feedback_rad}, {"starboard_rad", p.sample.starboard_feedback_rad},
            {"sensor_valid", p.sample.valid}, {"manual_rad", p.sample.manual_rad},
            {"heading_rad", p.navigation.relative_heading_rad}, {"target_heading_rad", p.navigation.target_heading_rad},
            {"target_speed_mps", p.navigation.target_speed_mps},
            {"operator_revision", p.order.revision}, {"operator_flags", p.order.flags}};
}
} // namespace

struct PanelWriter::Impl {
    struct Event { PanelState p; const char *name{}; };
    fs::path state, runtime;
    PanelState latest;
    std::array<Event, 64> events{};
    size_t head{}, count{};
    bool stopping{};
    std::atomic<uint64_t> ack{};
    std::optional<std::pair<uint64_t, wire::OperatorOrder>> operator_request;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;

    Impl(fs::path state_dir, fs::path runtime_dir, uint64_t boot)
        : state(std::move(state_dir)), runtime(std::move(runtime_dir))
    {
        fs::create_directories(state);
        fs::create_directories(runtime);
        std::ifstream input(state / "retained.json", std::ios::binary);
        if (input) {
            const auto saved = json::parse(input, nullptr, false);
            if (saved.is_object() && saved.value("version", 0) == 1) {
                latest.command.cause = std::min(saved.value("cause", 0u), uint32_t{SLX_SB_CAUSE_RESTART});
                latest.last_alarm = std::min(saved.value("last_alarm", 0u), uint32_t{SLX_SB_CAUSE_RESTART});
                if (saved.value("active", false) && !latest.command.cause)
                    latest.command.cause = SLX_SB_CAUSE_RESTART;
            }
        }
        if (latest.command.cause) {
            latest.command.state = SLX_SB_AWAIT_ACK;
            latest.last_alarm = latest.command.cause;
        }
        latest.boot = boot;
        latest.updated_ns = wire::monotonic_ns();
        std::error_code ignored;
        fs::remove(runtime / "ack.request", ignored);
        fs::remove(runtime / "operator.request", ignored);
        fs::remove(runtime / "operator.processing", ignored);
        events[count++] = {latest, "service_start"};
    }

    void event(const Event &event)
    {
        const auto &p = event.p;
        write_json(state / "retained.json", {
            {"copyright", "Copyright (c) 2026 Marco Pezzullo (Silicon LogiX)"},
            {"version", 1}, {"active", p.connected},
            {"cause", p.command.cause}, {"last_alarm", p.last_alarm}, {"session", p.session}});
        const auto log = state / "events.log";
        std::error_code error;
        if (fs::file_size(log, error) >= 65536 && !error) {
            fs::remove(state / "events.previous.log");
            fs::rename(log, state / "events.previous.log");
        }
        std::ofstream out(log, std::ios::app | std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        if (out.tellp() == 0)
            out << "# Silicon LogiX / SLX SeaBalance. Copyright (c) 2026 Marco Pezzullo.\n";
        out << p.boot << ' ' << p.session << ' ' << p.updated_ns << ' ' << p.sequence << ' '
            << event.name << " cause=" << p.command.cause << " previous=" << p.last_alarm
            << " mode=" << p.command.effective_mode << " operator_revision=" << p.order.revision
            << " port_rad=" << p.command.port_rad << " starboard_rad=" << p.command.starboard_rad << '\n';
        out.flush();
    }

    void read_order(const PanelState &current)
    {
        const auto processing = runtime / "operator.processing";
        std::error_code error;
        fs::rename(runtime / "operator.request", processing, error);
        if (error)
            return;
        json j;
        if (fs::file_size(processing, error) <= 4096 && !error) {
            std::ifstream request(processing, std::ios::binary);
            j = json::parse(request, nullptr, false);
        }
        fs::remove(processing);
        if (!current.connected || !current.operator_enabled || !current.sampled || !j.is_object())
            return;
        try {
            if (j.at("version") != 1 || j.at("session") != current.session)
                return;
            if (!j.at("revision").is_number_unsigned() || !j.at("flags").is_number_unsigned() ||
                !j.at("mode").is_number_unsigned())
                return;
            const auto flags = j.at("flags").get<uint64_t>(), mode = j.at("mode").get<uint64_t>();
            if (flags > (wire::operator_local | wire::operator_navigation | wire::operator_mark) || mode > SLX_SB_SLX)
                return;
            const wire::OperatorOrder order{j.at("revision").get<uint64_t>(), static_cast<uint32_t>(flags),
                static_cast<uint32_t>(mode), j.at("manual_rad").get<double>(), j.at("speed_mps").get<double>(),
                j.at("heading_rad").get<double>()};
            if (!wire::valid_order(order) || order.revision <= current.order.revision ||
                ((order.flags & wire::operator_local) && order.mode == SLX_SB_SLX && !current.has_slx))
                return;
            std::scoped_lock lock(mutex);
            if (latest.connected && latest.session == current.session)
                operator_request = {current.session, order};
        } catch (const json::exception &) {
            return;
        }
    }

    void run()
    {
        std::array<Event, 64> pending{};
        bool io_error = false;
        while (true) {
            size_t ready{};
            PanelState current;
            bool done{};
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, 100ms, [this] { return count != 0 || stopping; });
                ready = count;
                for (size_t i = 0; i < ready; ++i)
                    pending[i] = events[(head + i) % events.size()];
                count = 0;
                head = 0;
                current = latest;
                done = stopping;
            }
            try {
                for (size_t i = 0; i < ready; ++i)
                    event(pending[i]);
                write_json(runtime / "status.json", snapshot(current));
                const auto marker = runtime / "ack.request";
                std::ifstream request(marker, std::ios::binary);
                if (request) {
                    uint64_t session = 0;
                    std::string notice;
                    std::getline(request, notice);
                    request >> session;
                    request.close();
                    fs::remove(marker);
                    if (current.connected && session == current.session)
                        ack.store(session);
                }
                read_order(current);
                io_error = false;
            } catch (const std::exception &e) {
                if (!io_error)
                    std::cerr << "Panel storage error: " << e.what() << '\n';
                io_error = true;
            }
            if (done)
                return;
        }
    }
};

PanelWriter::PanelWriter(fs::path state, fs::path runtime, uint64_t boot)
    : impl_(std::make_unique<Impl>(std::move(state), std::move(runtime), boot))
{
    impl_->worker = std::thread([this] { impl_->run(); });
}

PanelWriter::~PanelWriter()
{
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->wake.notify_one();
    impl_->worker.join();
}

PanelState PanelWriter::initial() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->latest;
}

void PanelWriter::update(const PanelState &state, const char *event)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->latest = state;
    if (event) {
        if (impl_->count == impl_->events.size()) {
            impl_->head = (impl_->head + 1) % impl_->events.size();
            --impl_->count;
        }
        impl_->events[(impl_->head + impl_->count++) % impl_->events.size()] = {state, event};
        impl_->wake.notify_one();
    }
    if (!state.connected) {
        impl_->ack.store(0);
        impl_->operator_request.reset();
    }
}

bool PanelWriter::take_ack(uint64_t session)
{
    return impl_->ack.exchange(0) == session;
}

std::optional<wire::OperatorOrder> PanelWriter::take_order(uint64_t session)
{
    std::scoped_lock lock(impl_->mutex);
    auto request = std::exchange(impl_->operator_request, std::nullopt);
    if (request && request->first == session)
        return request->second;
    return std::nullopt;
}
} // namespace slx::sb
