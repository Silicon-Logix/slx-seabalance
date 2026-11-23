// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "gl_api.hpp"
#include "renderer.hpp"
#include "session.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace slx::sb {
namespace {
using namespace ImGui;
using json = nlohmann::json;
constexpr unsigned navy = 0x111c31, accent = 0x5354e8, ink = 0x192541, muted = 0x56647a;
constexpr unsigned line = 0xe3e8f0, paper = 0xffffff, canvas = 0xf3f5f9, teal = 0x087762;
constexpr unsigned amber = 0x9b691b, red = 0xb4484e;
constexpr std::array mode_labels{"Spento", "Manuale", "PID", "SLX"};
constexpr std::array timing_labels{"Software", "Lockstep", "Temporizzata"};
constexpr std::array execution_labels{"Software locale", "Centralina Ethernet · Lockstep", "Centralina Ethernet · 50 Hz"};
constexpr std::array fault_labels{"Nessuno", "Pinna BB bloccata", "Pinna BB lenta", "Corsa BB limitata",
    "Bias giroscopio", "Gyro saturo", "IMU obsoleta", "IMU assente", "Interruzione link"};
constexpr std::array cause_labels{"Nessun allarme", "Qualità IMU", "IMU obsoleta", "Posizione pinna",
    "Perdita collegamento", "Riavvio controllore"};
constexpr std::array clips{"SLX_Demo_Roll_Uncontrolled", "SLX_Demo_Roll_Controlled", "SLX_Demo_Fin_Sweep",
    "SLX_Demo_Heave_Pitch", "SLX_Demo_Actuator_Fault"};
constexpr std::array clip_labels{"Rollio libero", "Rollio illustrativo ridotto", "Corsa pinne",
    "Sussulto e beccheggio", "Arresto pinna di babordo"};
constexpr ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoSavedSettings;

std::pair<const char *, unsigned> stability_state(uint32_t flags)
{
    if (!(flags & SLX_SB_STABILITY_AVAILABLE)) return {"Stabilità N/D", muted};
    if (flags & SLX_SB_CAPSIZE_RECORDED) return {"Ribaltamento registrato", red};
    if (flags & SLX_SB_INVERTED) return {"Carena in inversione", red};
    if (flags & SLX_SB_RIGHTING_LOST) return {"Braccio di raddrizzamento perso", red};
    if (flags & SLX_SB_RESERVE_EXCEEDED) return {"Energia di rollio oltre la riserva", amber};
    if (flags & SLX_SB_OPENING_IMMERSED) return {"Apertura immersa", amber};
    return {"Raddrizzamento disponibile", teal};
}

ImVec4 color(unsigned hex)
{
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, 1};
}
ImU32 rgba(unsigned hex) { return GetColorU32(color(hex)); }
ImTextureID tex(unsigned id) { return static_cast<ImTextureID>(id); }

void style()
{
    StyleColorsLight();
    auto &s = GetStyle();
    s.FontSizeBase = 16;
    s.WindowPadding = {19, 16};
    s.FramePadding = {10, 6};
    s.ItemSpacing = {9, 8};
    s.ChildRounding = 11;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.WindowRounding = 0;
    s.FrameBorderSize = 1;
    s.ChildBorderSize = 1;
    s.WindowBorderSize = 0;
    auto *c = s.Colors;
    c[ImGuiCol_Text] = color(ink);
    c[ImGuiCol_TextDisabled] = color(muted);
    c[ImGuiCol_WindowBg] = color(canvas);
    c[ImGuiCol_ChildBg] = color(paper);
    c[ImGuiCol_PopupBg] = color(paper);
    c[ImGuiCol_Border] = color(line);
    c[ImGuiCol_FrameBg] = color(0xf6f8fc);
    c[ImGuiCol_FrameBgHovered] = color(0xebeffa);
    c[ImGuiCol_FrameBgActive] = color(0xe7e9fc);
    c[ImGuiCol_Button] = color(paper);
    c[ImGuiCol_ButtonHovered] = color(0xeff0fc);
    c[ImGuiCol_ButtonActive] = color(0xdfe1ff);
    c[ImGuiCol_Header] = color(0xe7e9fc);
    c[ImGuiCol_HeaderHovered] = color(0xeff0fc);
    c[ImGuiCol_HeaderActive] = color(0xdfe1ff);
    c[ImGuiCol_CheckMark] = color(accent);
    c[ImGuiCol_SliderGrab] = color(accent);
    c[ImGuiCol_SliderGrabActive] = color(0x4344c8);
    ImPlot::StyleColorsLight();
    auto &p = ImPlot::GetStyle();
    p.Colors[ImPlotCol_PlotBorder] = color(line);
    p.Colors[ImPlotCol_Line] = color(accent);
    p.Colors[ImPlotCol_AxisGrid] = color(line);
    p.PlotPadding = {9, 8};
    p.LineWeight = 2.2f;
    auto &io = GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    constexpr const char *system_fonts[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"
#endif
    };
    unsigned font_format = 0;
    for (const auto *file : system_fonts) {
        if (fs::is_regular_file(file)) {
            io.FontDefault = io.Fonts->AddFontFromFileTTF(file, 16);
            ++font_format;
            break;
        }
    }
    if (!font_format)
        io.FontDefault = io.Fonts->AddFontDefault();
}

void heading(const char *text, float size = 21)
{
    PushFont(nullptr, size);
    TextUnformatted(text);
    PopFont();
}
void small(const char *text) { TextColored(color(muted), "%s", text); }
void wrapped(const char *text)
{
    PushTextWrapPos(0.f);
    TextUnformatted(text);
    PopTextWrapPos();
}
bool primary(const char *label, ImVec2 size = {})
{
    PushStyleColor(ImGuiCol_Button, color(accent));
    PushStyleColor(ImGuiCol_ButtonHovered, color(0x6667f2));
    PushStyleColor(ImGuiCol_ButtonActive, color(0x4344c8));
    PushStyleColor(ImGuiCol_Text, color(paper));
    const bool clicked = Button(label, size);
    PopStyleColor(4);
    return clicked;
}
void badge(const char *text, unsigned background, unsigned foreground)
{
    const auto top = GetCursorScreenPos();
    const auto extent = CalcTextSize(text);
    GetWindowDrawList()->AddRectFilled(top, {top.x + extent.x + 18, top.y + extent.y + 10},
                                       rgba(background), 6);
    GetWindowDrawList()->AddText({top.x + 9, top.y + 5}, rgba(foreground), text);
    Dummy({extent.x + 18, extent.y + 10});
}

template <size_t N> bool choose(const char *label, uint32_t &value, const std::array<const char *, N> &names)
{
    int index = static_cast<int>(value);
    if (!Combo(label, &index, names.data(), static_cast<int>(N)))
        return false;
    value = static_cast<uint32_t>(index);
    return true;
}
void scalar(const char *label, double &value, double lo, double hi, const char *format)
{
    SliderScalar(label, ImGuiDataType_Double, &value, &lo, &hi, format, ImGuiSliderFlags_AlwaysClamp);
}
std::string value(double v, const char *format)
{
    if (!std::isfinite(v))
        return "N/D";
    char s[64]{};
    std::snprintf(s, sizeof(s), format, v);
    return s;
}

std::string utf8(const fs::path &path)
{
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}
fs::path path_from(const char *text)
{
    const std::string_view bytes(text);
    return fs::path(std::u8string(bytes.begin(), bytes.end()));
}

fs::path data_directory()
{
#ifdef _WIN32
    std::array<wchar_t, 32768> local{};
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (size && size < local.size())
        return fs::path(local.data()) / "Silicon LogiX" / "SLX SeaBalance";
#else
    if (const auto *data = std::getenv("XDG_DATA_HOME"))
        return fs::path(data) / "slx-seabalance";
    if (const auto *home = std::getenv("HOME"))
        return fs::path(home) / ".local/share/slx-seabalance";
#endif
    return fs::current_path() / "seabalance-data";
}

void open_target(const fs::path &path)
{
#ifdef _WIN32
    const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32)
        throw std::runtime_error("Impossibile aprire la risorsa");
#else
    std::string target = path.string();
    char tool[] = "xdg-open";
    char *argv[]{tool, target.data(), nullptr};
    pid_t child{};
    if (posix_spawnp(&child, tool, nullptr, nullptr, argv, environ) != 0)
        throw std::runtime_error("xdg-open non disponibile");
    std::thread([child] { waitpid(child, nullptr, 0); }).detach();
#endif
}

class Worker {
public:
    struct Result {
        std::optional<Session> session;
        std::optional<Comparison> pair;
        std::string error;
    };
    std::atomic_bool active{}, stop{}, ack{};
    std::atomic<uint32_t> mode{};
    std::atomic<double> manual{};
    ~Worker()
    {
        stop = true;
        if (thread_.joinable())
            thread_.join();
    }

    void start(Scenario s, fs::path folder, bool compare_arms, bool advanced)
    {
        if (active)
            return;
        if (thread_.joinable())
            thread_.join();
        {
            std::scoped_lock lock(mutex_);
            pending_.clear();
            result_.reset();
            update_.reset();
            receipt_.reset();
            update_busy_ = false;
        }
        stop = ack = false;
        mode = s.mode;
        manual = s.manual_rad;
        active = true;
        try {
            thread_ = std::thread([this, s = std::move(s), folder = std::move(folder), compare_arms, advanced] {
                Result result;
                try {
                    if (compare_arms) {
                        std::vector<Comparison> comparisons;
                        comparisons.push_back(slx::sb::compare(s, folder, advanced));
                        comparison_report(folder, comparisons);
                        result.pair.emplace(std::move(comparisons.front()));
                    } else {
                        result.session.emplace(run(s, folder, {
                            [this] { return OperatorInput{mode.load(), manual.load(), ack.exchange(false), stop.load()}; },
                            [this](const Row &row) {
                                std::scoped_lock lock(mutex_);
                                if (pending_.size() == 1000)
                                    pending_.pop_front();
                                pending_.push_back(row);
                            },
                            [this] {
                                std::scoped_lock lock(mutex_);
                                return std::exchange(update_, std::nullopt);
                            },
                            [this](UpdateReceipt receipt) {
                                std::scoped_lock lock(mutex_);
                                receipt_ = receipt;
                                update_busy_ = false;
                            }}));
                    }
                } catch (const std::exception &e) {
                    result.error = e.what();
                }
                {
                    std::scoped_lock lock(mutex_);
                    result_.emplace(std::move(result));
                    update_.reset();
                    update_busy_ = false;
                }
                active = false;
            });
        } catch (...) {
            active = false;
            throw;
        }
    }

    std::optional<Result> drain(std::deque<Row> &recent)
    {
        std::deque<Row> incoming;
        std::optional<Result> result;
        {
            std::scoped_lock lock(mutex_);
            incoming.swap(pending_);
            result.swap(result_);
        }
        for (const auto &row : incoming) {
            if (recent.size() == 1000)
                recent.pop_front();
            recent.push_back(row);
        }
        return result;
    }

    bool submit(const SLX_SB_ModelUpdate &update)
    {
        std::scoped_lock lock(mutex_);
        if (!active || stop || update_busy_ || result_)
            return false;
        update_ = update;
        update_busy_ = true;
        return true;
    }

    bool updating()
    {
        std::scoped_lock lock(mutex_);
        return update_busy_;
    }

    std::optional<UpdateReceipt> take_receipt()
    {
        std::scoped_lock lock(mutex_);
        return std::exchange(receipt_, std::nullopt);
    }

private:
    std::mutex mutex_;
    std::thread thread_;
    std::deque<Row> pending_;
    std::optional<Result> result_;
    std::optional<SLX_SB_ModelUpdate> update_;
    std::optional<UpdateReceipt> receipt_;
    bool update_busy_{};
};

struct Plot {
    std::vector<double> axis, a, b, c, d;
    fs::path primary_series, secondary_series;
    int selection{-1};

    static Row sample(const SampleSeries &rows, size_t i, size_t stride)
    {
        return stride > 6 ? rows.direct(i) : rows[i];
    }
    static const Row &sample(const std::deque<Row> &rows, size_t i, size_t) { return rows[i]; }
    template <class Rows>
    void load(const Rows &rows, const Session *other = nullptr, bool unwrapped = false)
    {
        axis.clear(); a.clear(); b.clear(); c.clear(); d.clear();
        const auto n = other ? std::min(rows.size(), other->rows.size()) : rows.size();
        const auto step = std::max<size_t>(1, (n + 2399) / 2400);
        for (size_t i = 0; i < n; i += step) {
            const auto &r = sample(rows, i, step);
            axis.push_back(r.sample.sim_ns * 1e-9);
            a.push_back((unwrapped ? r.reference.roll_unwrapped_rad : r.reference.roll_rad) * to_degrees);
            if (!unwrapped)
                b.push_back(other ? sample(other->rows, i, step).reference.roll_rad * to_degrees :
                    (r.command.flags & SLX_SB_EST_VALID) ? r.command.estimated_roll_rad * to_degrees : missing);
            c.push_back(r.reference.port_actual_rad * to_degrees);
            d.push_back(r.command.port_rad * to_degrees);
        }
    }
    template <class Rows>
    void fins(const Rows &rows, bool starboard)
    {
        load(rows);
        const auto step = std::max<size_t>(1, (rows.size() + 2399) / 2400);
        for (size_t j = 0, i = 0; i < rows.size(); i += step, ++j) {
            const auto &r = sample(rows, i, step);
            if (starboard) {
                c[j] = r.reference.starboard_actual_rad * to_degrees;
                d[j] = r.command.starboard_rad * to_degrees;
            }
        }
    }
    void from_session(const Session &session, int kind, const Session *other = nullptr)
    {
        const auto secondary = other ? other->directory : fs::path{};
        if (selection == kind && primary_series == session.directory && secondary_series == secondary)
            return;
        if (kind == 1 || kind == 2)
            fins(session.rows, kind == 2);
        else
            load(session.rows, other, kind == 4);
        selection = kind;
        primary_series = session.directory;
        secondary_series = secondary;
    }
    void draw(const char *id, float height, const char *first, const char *second,
              bool pair, bool fins = false, bool follow = false, double playhead = missing) const
    {
        if (!ImPlot::BeginPlot(id, {-1, height}, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus))
            return;
        ImPlot::SetupAxes("Tempo [s]", fins ? "Pinna [°]" : selection == 4 ? "Rollio continuo [°]" : "Rollio [°]");
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        if (follow) {
            const double end = axis.empty() ? 20 : std::max(20., axis.back());
            double range = 12;
            for (const auto *trace : {&a, &b}) {
                for (const double v : *trace)
                    if (std::isfinite(v)) range = std::max(range, std::ceil(std::abs(v) / 5.) * 5.);
            }
            ImPlot::SetupAxesLimits(end - 20, end, -range, range, ImPlotCond_Always);
        } else if (axis.empty()) {
            ImPlot::SetupAxesLimits(0, 20, -12, 12, ImPlotCond_Once);
        }
        const auto n = static_cast<int>(axis.size());
        if (n) {
            ImPlot::SetNextLineStyle(color(pair ? amber : accent));
            ImPlot::PlotLine(first, axis.data(), (fins ? c : a).data(), n);
            if (fins || !b.empty()) {
                ImPlot::SetNextLineStyle(color(pair ? accent : teal));
                ImPlot::PlotLine(second, axis.data(), (fins ? d : b).data(), n);
            }
            if (std::isfinite(playhead)) {
                ImPlot::SetNextLineStyle(color(navy), 1.f);
                ImPlot::PlotInfLines("##playhead", &playhead, 1);
            }
        }
        ImPlot::EndPlot();
    }
};

struct StabilityHistory {
    std::vector<double> times, reserve, energy, updates, faults, capsize;
    fs::path directory;
    uint64_t latest{UINT64_MAX};
    double ceiling{1};
    bool available{};

    template <class Rows> void load(const Rows &rows, bool live)
    {
        times.clear(); reserve.clear(); energy.clear(); updates.clear(); faults.clear(); capsize.clear();
        ceiling = 1;
        available = false;
        uint32_t previous_fault = 0, previous_flags = 0;
        const auto stride = std::max<size_t>(1, (rows.size() + 2399) / 2400);
        for (size_t i = 0; i < rows.size();) {
            const auto &row = Plot::sample(rows, i, stride);
            const auto &r = row.reference;
            const double t = r.sim_ns * 1e-9;
            const bool valid = (r.stability_flags & SLX_SB_STABILITY_AVAILABLE) != 0;
            available = available || valid;
            times.push_back(t);
            reserve.push_back(valid ? r.reserve_j * .001 : missing);
            energy.push_back(valid ? r.kinetic_j * .001 : missing);
            if (valid)
                ceiling = std::max({ceiling, reserve.back(), energy.back()});
            if (live) {
                const bool have_previous = i != 0 || row.sample.sequence == 0;
                if (row.live_fields & ~uint32_t{SLX_SB_UPDATE_FAULT}) updates.push_back(t);
                if ((have_previous && r.imposed_fault != previous_fault) || (row.live_fields & SLX_SB_UPDATE_FAULT))
                    faults.push_back(t);
                if (have_previous && (r.stability_flags & ~previous_flags & SLX_SB_CAPSIZE_RECORDED))
                    capsize.push_back(t);
                previous_fault = r.imposed_fault;
                previous_flags = r.stability_flags;
            }
            if (i + 1 == rows.size())
                break;
            i += std::min(stride, rows.size() - 1 - i);
        }
    }

    void from_live(const std::deque<Row> &rows)
    {
        if (rows.empty() || rows.back().sample.sequence == latest)
            return;
        latest = rows.back().sample.sequence;
        load(rows, true);
    }

    void from_session(const Session &session)
    {
        if (directory == session.directory)
            return;
        directory = session.directory;
        load(session.rows, false);
        for (const auto &event : session.events) {
            const double t = event.sim_ns * 1e-9;
            if (event.name == "live_update" && (event.code & ~uint32_t{SLX_SB_UPDATE_FAULT})) updates.push_back(t);
            if ((event.name == "fault_start" || event.name == "fault_end" ||
                 event.name == "fault_injected" || event.name == "fault_cancelled") &&
                (faults.empty() || faults.back() != t))
                faults.push_back(t);
            if (event.name == "capsize") capsize.push_back(t);
        }
    }

    void draw(const char *id, float height, bool live, double cursor = missing) const
    {
        if (!available) {
            small("La riserva e l’energia compaiono con i campioni di stabilità.");
            return;
        }
        if (!ImPlot::BeginPlot(id, {-1,height}, ImPlotFlags_NoTitle | ImPlotFlags_NoMenus))
            return;
        ImPlot::SetupAxes("Tempo [s]", "Energia [kJ]");
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        if (live) {
            const double end = std::max(4., times.back());
            ImPlot::SetupAxesLimits(std::max(0., end - 20), end, 0., ceiling * 1.15, ImPlotCond_Always);
        }
        ImPlot::SetNextLineStyle(color(teal), 2.f);
        ImPlot::PlotLine("Riserva di raddrizzamento", times.data(), reserve.data(), static_cast<int>(times.size()));
        ImPlot::SetNextLineStyle(color(accent), 2.f);
        ImPlot::PlotLine("Energia del rollio", times.data(), energy.data(), static_cast<int>(times.size()));
        auto mark = [](const char *label, const std::vector<double> &values, unsigned tint) {
            if (!values.empty()) {
                ImPlot::SetNextLineStyle(color(tint), 1.f);
                ImPlot::PlotInfLines(label, values.data(), static_cast<int>(values.size()));
            }
        };
        mark("Interventi", updates, 0x637fac);
        mark("Guasti", faults, amber);
        mark("Ribaltamento", capsize, red);
        if (std::isfinite(cursor)) {
            ImPlot::SetNextLineStyle(color(navy), 1.f);
            ImPlot::PlotInfLines("##tempo", &cursor, 1);
        }
        ImPlot::EndPlot();
    }
};

View recorded_view(const Row &row, const Scenario &scenario)
{
    auto config = scenario.model;
    config.speed_mps = row.sample.speed_mps;
    config.wave_height_m = row.conditions.wave_height_m;
    config.wave_period_s = row.conditions.wave_period_s;
    config.sea_kind = row.conditions.sea_kind;
    return {row.reference, config};
}

struct Launch {
    fs::path shot, replay_dir, ab_dir;
    int width{1440}, height{940}, page{-1};
    uint32_t clip{};
    double at{15};
};

class App {
public:
    explicit App(const Launch &launch) : root_(data_directory()), executable_(executable_directory()),
                                         page_(std::max(0, launch.page)), clip_(launch.clip)
    {
        if (launch.shot.empty()) {
            fs::create_directories(root_ / "sessions");
            if (fs::is_regular_file(root_ / "settings.json")) {
                try {
                    const auto j = read_json(root_ / "settings.json");
                    setup_ = scenario_from(j.at("scenario"));
                    visuals_ = j.at("render_3d").get<bool>();
                } catch (const std::exception &) {
                    message_ = "Impostazioni locali non leggibili. Uso il profilo dimostrativo.";
                }
            }
        }
        std::snprintf(peer_.data(), peer_.size(), "%s", setup_.peer.c_str());
        if (!launch.replay_dir.empty()) {
            session_.emplace(replay(launch.replay_dir));
            session_metrics_ = measure(*session_);
            page_ = launch.page >= 0 ? launch.page : 2;
            inspect_replay_ = page_ == 6;
            playhead_ = std::min(launch.at, duration(*session_));
        }
        if (!launch.ab_dir.empty()) {
            load_comparison(launch.ab_dir);
            playhead_ = std::min(launch.at, duration(pair_->pid));
            page_ = 1;
        }
        if (visuals_)
            load_renderer();
        if (!launch.shot.empty() && !renderer_)
            throw std::runtime_error(message_);
    }

    void frame()
    {
        receive();
        const auto window = GetMainViewport();
        SetNextWindowPos(window->Pos);
        SetNextWindowSize(window->Size);
        PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
        Begin("SeaBalance", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        masthead();
        nav();
        PushStyleVar(ImGuiStyleVar_WindowPadding, {20,15});
        PushStyleColor(ImGuiCol_ChildBg, color(canvas));
        BeginChild("workspace", {0,0}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoSavedSettings);
        PopStyleColor();
        PopStyleVar();
        PushStyleVar(ImGuiStyleVar_WindowPadding, {13,10});
        switch (page_) {
        case 0: bench(); break;
        case 1: comparison(); break;
        case 2: sessions(); break;
        case 3: presentation(); break;
        case 4: settings(); break;
        case 5: about(); break;
        case 6: stability(); break;
        }
        PopStyleVar();
        EndChild();
        End();
        PopStyleVar();
    }

private:
    fs::path root_, executable_;
    Scenario setup_{demo_scenario()}, live_setup_{setup_};
    std::array<char,64> peer_{};
    std::array<char,1024> session_path_{};
    std::array<char,1024> comparison_path_{};
    std::unique_ptr<Renderer> renderer_;
    Camera camera_;
    Worker worker_;
    Plot live_plot_, replay_plot_;
    StabilityHistory live_energy_, replay_energy_;
    std::deque<Row> live_rows_;
    std::optional<Session> session_;
    std::optional<Comparison> pair_;
    Metrics session_metrics_, off_metrics_, pid_metrics_, slx_metrics_;
    std::string message_, graphics_;
    int page_{};
    uint32_t clip_{}, playback_speed_{2};
    double playhead_{}, demo_time_{};
    bool advance_playback_{}, animate_demo_{}, assembly_{}, use_slx_{}, right_slx_{};
    bool visuals_{true}, fins_plot_{}, starboard_plot_{}, unwrapped_roll_{true}, compare_pending_{};
    bool inspect_replay_{};
    bool show_live_energy_{}, show_replay_energy_{}, open_live_editor_{};
    uint32_t live_fault_{SLX_SB_MISSING_IMU};
    double live_fault_duration_{3.};
    SLX_SB_ModelUpdate live_draft_{}, live_base_{};
    std::string update_message_;
    std::array<char, 1024> gz_path_{};
    std::string curve_key_, curve_error_;
    SLX_SB_StabilityCurve preview_curve_{};

    static double duration(const Session &s) { return s.rows.empty() ? 0 : (s.rows.size() - 1) * dt; }
    bool available_slx() const { return fs::is_regular_file(core_file(executable_, false)); }

    void load_renderer()
    {
        try {
            renderer_ = std::make_unique<Renderer>(executable_ / "assets");
            graphics_.clear();
        } catch (const std::exception &e) {
            graphics_ = e.what();
            message_ = "Scena 3D · " + graphics_;
        }
    }
    void set_comparison(Comparison comparison)
    {
        pair_.emplace(std::move(comparison));
        off_metrics_ = measure(pair_->off);
        pid_metrics_ = measure(pair_->pid);
        slx_metrics_ = pair_->slx ? measure(*pair_->slx) : Metrics{};
        playhead_ = 0;
        right_slx_ = false;
    }
    void load_comparison(const fs::path &root)
    {
        Comparison comparison{};
        comparison.off = replay(root / "off");
        comparison.pid = replay(root / "pid");
        if (fs::is_regular_file(root / "slx/manifest.json"))
            comparison.slx.emplace(replay(root / "slx"));
        auto expected = configuration(comparison.off.scenario);
        auto matches = [&](const Session &candidate, uint32_t mode) {
            auto config = configuration(candidate.scenario);
            config["mode"] = SLX_SB_OFF;
            return candidate.scenario.mode == mode && config == expected;
        };
        if (comparison.off.scenario.mode != SLX_SB_OFF || !matches(comparison.pid, SLX_SB_PID) ||
            (comparison.slx && !matches(*comparison.slx, SLX_SB_SLX)))
            throw std::runtime_error("Il confronto richiede bracci dello stesso scenario");
        set_comparison(std::move(comparison));
    }
    void receive()
    {
        auto result = worker_.drain(live_rows_);
        if (const auto receipt = worker_.take_receipt()) {
            if (receipt->result == SLX_SB_OK)
                update_message_ = "Intervento applicato a " + value(receipt->sequence * dt, "%.2f s");
            else if (receipt->panel_has_authority)
                update_message_ = "La centralina ha acquisito il comando della manovra.";
            else if (receipt->result == SLX_SB_E_ABI)
                update_message_ = "Serve il core modello della versione 0.3.0 per gli interventi in corso.";
            else
                update_message_ = receipt->result == SLX_SB_E_SEQUENCE ?
                    "L’intervento è arrivato alla fine della prova." : "Valori fuori intervallo. Intervento respinto.";
        }
        if (!result)
            return;
        compare_pending_ = false;
        if (!result->error.empty())
            message_ = result->error;
        if (result->session) {
            session_.emplace(std::move(*result->session));
            session_metrics_ = measure(*session_);
            message_ = session_->manifest.value("reason", "");
            if (message_.empty())
                message_ = session_->manifest.value("status", "") == "stopped" ?
                    "Prova arrestata. Registrazione disponibile in Sessioni." :
                    "Prova completata. Registrazione disponibile in Sessioni.";
            const auto path = utf8(session_->directory);
            std::snprintf(session_path_.data(), session_path_.size(), "%s", path.c_str());
        }
        if (result->pair) {
            set_comparison(std::move(*result->pair));
            message_ = "Confronto registrato.";
        }
    }

    void masthead()
    {
        const auto pos = GetCursorScreenPos();
        const float width = GetContentRegionAvail().x;
        const ImVec2 bottom{pos.x + width, pos.y + 70};
        auto *draw = GetWindowDrawList();
        draw->AddRectFilled(pos, bottom, rgba(navy));
        draw->AddRectFilledMultiColor({pos.x, bottom.y-3}, bottom, rgba(accent), rgba(0xae43ed),
                                    rgba(0xae43ed), rgba(accent));
        if (renderer_) {
            draw->AddImageRounded(tex(renderer_->logo()), {pos.x+21,pos.y+15}, {pos.x+95,pos.y+55},
                                  {0,0}, {1,1}, IM_COL32_WHITE, 4);
        }
        const float x = pos.x + (renderer_ ? 112 : 21);
        draw->AddText(GetFont(), 26, {x,pos.y+9}, rgba(paper), "SLX SeaBalance");
        draw->AddText(GetFont(), 14, {x,pos.y+41}, rgba(0xb9c7df), "Stabilizzazione attiva del rollio");
        const bool current_run = !compare_pending_ && (worker_.active || !live_rows_.empty());
        const auto run_timing = page_ == 0 && current_run ? live_setup_.timing : setup_.timing;
        const char *timing = timing_labels[static_cast<size_t>(run_timing)];
        if (page_ == 2 && session_)
            timing = timing_labels[static_cast<size_t>(session_->scenario.timing)];
        if (page_ == 6 && inspect_replay_ && session_ && !worker_.active)
            timing = timing_labels[static_cast<size_t>(session_->scenario.timing)];
        if (page_ == 1)
            timing = pair_ ? timing_labels[static_cast<size_t>(pair_->off.scenario.timing)] : "Software";
        const char *source =
            page_ == 2 || (page_ == 6 && inspect_replay_ && session_ && !worker_.active) || (page_ == 1 && pair_) ?
            "Replay sintetico" : "Sintetica";
        if (page_ == 3) {
            timing = "Presentazione";
            source = renderer_ && renderer_->has_presentation() ? "Clip illustrative" : "Ispezione 3D";
        }
        const float badges_width = CalcTextSize(timing).x + CalcTextSize(source).x + 65;
        SetCursorScreenPos({bottom.x - badges_width, pos.y+20});
        badge(timing, 0x283754, paper);
        SameLine();
        badge(source, 0x283754, 0xd7dcff);
        SetCursorScreenPos({pos.x,bottom.y});
    }
    void nav()
    {
        constexpr std::array labels{"Banco di prova", "Stabilità", "Confronto A/B", "Sessioni",
                                    "Presentazione", "Impostazioni", "Informazioni"};
        constexpr std::array pages{0, 6, 1, 2, 3, 4, 5};
        PushStyleColor(ImGuiCol_ChildBg, color(paper));
        PushStyleVar(ImGuiStyleVar_WindowPadding, {20,8});
        BeginChild("navigation", {0,51}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            if (i) SameLine();
            if (pages[i] == page_) {
                PushStyleColor(ImGuiCol_Button, color(0xe7e9fc));
                PushStyleColor(ImGuiCol_Text, color(accent));
            }
            const bool selected = Button(labels[i], {0,34});
            if (pages[i] == page_) PopStyleColor(2);
            if (selected) {
                page_ = pages[i];
                advance_playback_ = false;
            }
        }
        EndChild();
        PopStyleVar();
        PopStyleColor();
    }

    void action(const auto &call)
    {
        try {
            call();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    void start(bool ab)
    {
        action([&] {
            auto s = setup_;
            if (ab) s.timing = wire::Timing::software;
            s.peer = peer_.data();
            s.pace_software = !ab;
            validate(s);
            live_setup_ = s;
            const auto dir = root_ / "sessions" /
                (std::string(ab ? "confronto-" : "prova-") + std::to_string(wire::nonce()));
            if (!ab) {
                live_rows_.clear();
                live_energy_ = {};
                update_message_.clear();
                live_fault_ = s.model.fault ? s.model.fault : uint32_t{SLX_SB_MISSING_IMU};
                if (s.timing == wire::Timing::software && live_fault_ == SLX_SB_LINK_LOSS)
                    live_fault_ = SLX_SB_MISSING_IMU;
            }
            worker_.start(s, dir, ab, use_slx_ && available_slx());
            compare_pending_ = ab;
            message_ = ab ? "Calcolo dei bracci con seed comune..." : "";
            if (!ab)
                page_ = 0;
        });
    }

    void bench()
    {
        const bool live = worker_.active && !compare_pending_;
        const Row *row = live_rows_.empty() ? nullptr : &live_rows_.back();
        const float width = GetContentRegionAvail().x;
        heading("Banco di prova", 27);
        SameLine(std::max(0.f, width - 265));
        BeginDisabled(worker_.active);
        if (primary("Avvia prova", {125,37})) start(false);
        EndDisabled();
        SameLine();
        BeginDisabled(!live);
        if (Button("Arresta prova", {128,37})) worker_.stop = true;
        EndDisabled();
        small(live && worker_.stop ? "Chiusura e analisi della registrazione..." :
              message_.empty() ? "Modello di rollio · Campionamento IMU 50 Hz" : message_.c_str());
        if (row) {
            SameLine();
            const auto state = stability_state(row->reference.stability_flags);
            TextColored(color(state.second), "%s", state.first);
        }
        Spacing();
        if (BeginTable("bench-columns", 2, ImGuiTableFlags_SizingStretchProp)) {
            TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 2.8f);
            TableSetupColumn("Scenario", ImGuiTableColumnFlags_WidthFixed, width < 1130 ? 297.f : 337.f);
            TableNextColumn();
            const float h = std::clamp(GetMainViewport()->Size.y * .37f - 20.f, 228.f, 458.f);
            BeginChild("vessel-card", {0, h+69}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, panel_flags);
            heading("Scena 3D", 19);
            camera_buttons();
            View view;
            view.scenario = row ? live_setup_.model : setup_.model;
            if (row) view = recorded_view(*row, live_setup_);
            viewport("bench-vessel", view, 0, {GetContentRegionAvail().x, GetContentRegionAvail().y},
                     live ? "SIMULAZIONE LIVE" : row ? "ULTIMO CAMPIONE" : "POSA INIZIALE");
            EndChild();
            spacing_row();
            readings(row);
            spacing_row();
            BeginChild("live-chart", {0,show_live_energy_ ? 248.f : 211.f}, ImGuiChildFlags_Borders |
                       ImGuiChildFlags_AlwaysUseWindowPadding, panel_flags);
            if (RadioButton("Rollio", !show_live_energy_)) show_live_energy_ = false;
            SameLine();
            if (RadioButton("Riserva ed energia", show_live_energy_)) show_live_energy_ = true;
            if (show_live_energy_) {
                live_energy_.from_live(live_rows_);
                live_energy_.draw("energy-live", 164, true);
                small("Il margine si riduce quando l’energia del rollio si avvicina alla riserva.");
            } else {
                live_plot_.load(live_rows_);
                live_plot_.draw("roll-live", 146, "Riferimento", "Stima IMU", false, false, true);
            }
            EndChild();
            TableNextColumn();
            scenario_card(live, row);
            spacing_row();
            fin_card(row, live);
            EndTable();
        }
    }
    static void spacing_row() { Dummy({0,3}); }
    void camera_buttons()
    {
        SameLine();
        if (Button(camera_.dry ? "In navigazione" : "Ispeziona")) {
            camera_.dry = !camera_.dry;
            camera_.elevation = camera_.dry ? -.15f : Camera{}.elevation;
        }
        SameLine();
        if (Button("Centra vista")) camera_ = {};
    }
    void viewport(const char *id, const View &view, unsigned slot, ImVec2 size, const char *tag)
    {
        size.y = std::max(size.y, 10.f);
        const auto at = GetCursorScreenPos();
        auto *draw = GetWindowDrawList();
        const ImVec2 end{at.x+size.x,at.y+size.y};
        unsigned image{};
        if (renderer_ && visuals_ && graphics_.empty()) {
            try {
                image = renderer_->render(slot, static_cast<int>(size.x), static_cast<int>(size.y), view, camera_);
            } catch (const std::exception &e) {
                graphics_ = e.what();
            }
        }
        if (image) {
            draw->AddImageRounded(tex(image), at, end, {0,1}, {1,0}, IM_COL32_WHITE, 8);
        } else {
            draw->AddRectFilled(at, end, rgba(0xe7ecf5), 8);
            draw->AddText({at.x+14, at.y+size.y*.45f}, rgba(ink),
                          graphics_.empty() ? "Rendering disattivato" : graphics_.c_str());
        }
        const auto label = CalcTextSize(tag);
        draw->AddRectFilled({at.x+12,at.y+11}, {at.x+32+label.x,at.y+22+label.y}, rgba(navy), 5);
        draw->AddText({at.x+22,at.y+16}, rgba(paper), tag);
        if (image) {
            draw->AddText({at.x+13,end.y-24}, rgba(paper), "Trascina per orbitare  ·  Rotella per zoom");
        }
        InvisibleButton(id, size);
        if (IsItemActive() && IsMouseDragging(ImGuiMouseButton_Left)) {
            camera_.yaw -= GetIO().MouseDelta.x * .009f;
            camera_.elevation = std::clamp(camera_.elevation + GetIO().MouseDelta.y*.006f, -.74f, 1.34f);
        }
        if (IsItemHovered())
            camera_.distance = std::clamp(camera_.distance * std::exp(-GetIO().MouseWheel * .12f), 14.f, 70.f);
    }

    void submit_update(const SLX_SB_ModelUpdate &update)
    {
        if (worker_.submit(update))
            update_message_ = "Attesa del campione successivo…";
        else
            update_message_ = "Attendi l’esito dell’intervento precedente.";
    }

    void live_controls(const Row *row)
    {
        const auto &v = row ? row->conditions : live_conditions(live_setup_.model);
        Text("Onde %.2f m  ·  %.1f s", v.wave_height_m, v.wave_period_s);
        if (row)
            small(("Velocità obiettivo " + value(row->navigation.target_speed_mps, "%.2f m/s")).c_str());
        const bool requested = std::exchange(open_live_editor_, false);
        if (Button("Modifica condizioni") || requested) {
            live_draft_ = abi_struct<SLX_SB_ModelUpdate>();
            live_draft_.conditions = v;
            live_draft_.speed_mps = row ? row->navigation.target_speed_mps : live_setup_.model.speed_mps;
            live_draft_.relative_heading_rad = row ? row->navigation.target_heading_rad : live_setup_.model.relative_heading_rad;
            live_base_ = live_draft_;
            OpenPopup("Condizioni durante la prova");
        }
        Separator();
        const auto fault = row ? row->reference.imposed_fault : uint32_t{SLX_SB_NO_FAULT};
        heading("Iniezione guasti", 17);
        TextColored(color(fault ? red : muted), "%s", fault ? fault_labels.at(fault) : "Nessun guasto attivo");
        constexpr auto names = fault_labels;
        SetNextItemWidth(-105);
        if (BeginCombo("Guasto", names.at(live_fault_))) {
            for (uint32_t i = 1; i < names.size(); ++i) {
                if (i == SLX_SB_LINK_LOSS && live_setup_.timing == wire::Timing::software)
                    continue;
                if (Selectable(names[i], i == live_fault_))
                    live_fault_ = i;
            }
            EndCombo();
        }
        SetNextItemWidth(-105);
        InputDouble("Durata [s]", &live_fault_duration_, .5, 2., "%.2f");
        if (live_fault_ == SLX_SB_LINK_LOSS)
            wrapped("Il watchdog Ethernet chiude la prova alla perdita del comando.");
        else
            small("0 per mantenere il guasto fino a rimozione");
        BeginDisabled(worker_.updating() || worker_.stop);
        BeginDisabled(!std::isfinite(live_fault_duration_) || live_fault_duration_ < 0.);
        if (primary("Inietta guasto")) {
            auto update = abi_struct<SLX_SB_ModelUpdate>();
            update.fields = SLX_SB_UPDATE_FAULT;
            update.fault = live_fault_;
            update.fault_duration_s = live_fault_duration_;
            submit_update(update);
        }
        EndDisabled();
        SameLine();
        if (Button("Rimuovi guasto")) {
            auto update = abi_struct<SLX_SB_ModelUpdate>();
            update.fields = SLX_SB_UPDATE_FAULT;
            submit_update(update);
        }
        EndDisabled();
        if (!update_message_.empty())
            wrapped(update_message_.c_str());
    }

    void live_editor(bool live, bool board_owns)
    {
        SetNextWindowSizeConstraints({480,0}, {480,720});
        SetNextWindowPos(GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f,.5f});
        if (!BeginPopupModal("Condizioni durante la prova", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (!live) {
            CloseCurrentPopup();
            EndPopup();
            return;
        }
        auto &v = live_draft_.conditions;
        const auto &b = live_base_.conditions;
        constexpr float label_width = -165;
        SetNextItemWidth(label_width);
        choose("Mare", v.sea_kind, std::array{"Regolare", "Irregolare"});
        SetNextItemWidth(label_width); scalar("Altezza onde", v.wave_height_m, 0, 7, "%.2f m");
        SetNextItemWidth(label_width); scalar("Periodo", v.wave_period_s, 2, 20, "%.1f s");
        Separator();
        BeginDisabled(board_owns);
        SetNextItemWidth(label_width); scalar("Velocità obiettivo", live_draft_.speed_mps, 0, 9, "%.2f m/s");
        double degrees = live_draft_.relative_heading_rad * to_degrees;
        SetNextItemWidth(label_width);
        constexpr double heading_min = -180., heading_max = 180.;
        if (SliderScalar("Rotta all’onda", ImGuiDataType_Double, &degrees, &heading_min, &heading_max, "%+.0f°",
                         ImGuiSliderFlags_AlwaysClamp))
            live_draft_.relative_heading_rad = degrees / to_degrees;
        EndDisabled();
        if (board_owns)
            small("Manovra assegnata al pannello della centralina.");
        Separator();
        SetNextItemWidth(label_width); scalar("Fattore di carico", v.load_ratio, .65, 1.65, "%.2f");
        SetNextItemWidth(label_width); scalar("Quota baricentro", v.cg_rise_m, -.20, .90, "%+.2f m");
        SetNextItemWidth(label_width); scalar("Superficie libera", v.free_surface_loss_m, 0, .9, "%.2f m GM");
        SetNextItemWidth(label_width); scalar("Carico trasversale", v.load_offset_m, -.70, .70, "%+.2f m");
        SetNextItemWidth(label_width); scalar("Ritardo attuatore", v.actuator_delay_s, 0, 1, "%.2f s");
        uint32_t fields = 0;
        if (v.sea_kind != b.sea_kind || v.wave_height_m != b.wave_height_m || v.wave_period_s != b.wave_period_s)
            fields |= SLX_SB_UPDATE_SEA;
        if (v.load_ratio != b.load_ratio || v.cg_rise_m != b.cg_rise_m ||
            v.free_surface_loss_m != b.free_surface_loss_m || v.load_offset_m != b.load_offset_m)
            fields |= SLX_SB_UPDATE_LOAD;
        if (v.actuator_delay_s != b.actuator_delay_s)
            fields |= SLX_SB_UPDATE_DELAY;
        if (!board_owns && (live_draft_.speed_mps != live_base_.speed_mps ||
            live_draft_.relative_heading_rad != live_base_.relative_heading_rad))
            fields |= SLX_SB_UPDATE_NAVIGATION;
        Spacing();
        BeginDisabled(!fields || worker_.updating() || worker_.stop);
        if (primary("Applica alla prova")) {
            live_draft_.fields = fields;
            submit_update(live_draft_);
            CloseCurrentPopup();
        }
        EndDisabled();
        SameLine();
        if (Button("Annulla"))
            CloseCurrentPopup();
        EndPopup();
    }

    void scenario_card(bool live, const Row *row)
    {
        BeginChild("configure-run", {0,0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                    ImGuiChildFlags_AlwaysUseWindowPadding, panel_flags);
        heading(live ? "Scenario in corso" : "Scenario", 19);
        if (live) {
            live_controls(row);
        } else {
            BeginDisabled(worker_.active);
            auto t = static_cast<uint32_t>(setup_.timing);
            if (choose("Esecuzione", t, execution_labels)) setup_.timing = static_cast<wire::Timing>(t);
            if (setup_.timing != wire::Timing::software) {
                SetNextItemWidth(-115);
                InputText("IPv4 centralina", peer_.data(), peer_.size());
            }
            scalar("Velocità", setup_.model.speed_mps, 0, 9, "%.1f m/s");
            choose("Mare", setup_.model.sea_kind, std::array{"Regolare", "Irregolare"});
            scalar("Altezza", setup_.model.wave_height_m, 0, 7, "%.2f m");
            scalar("Periodo", setup_.model.wave_period_s, 2, 20, "%.1f s");
            double direction = setup_.model.relative_heading_rad * to_degrees;
            scalar("Direzione", direction, -180, 180, "%.0f°");
            setup_.model.relative_heading_rad = direction / to_degrees;
            bool scheduled = setup_.duration_s > 0;
            if (Checkbox("Arresto programmato", &scheduled))
                setup_.duration_s = scheduled ? std::max(120., setup_.warmup_s + 30.) : 0.;
            if (scheduled) {
                InputDouble("Durata [s]", &setup_.duration_s, 0, 0, "%.1f");
                if (std::isfinite(setup_.duration_s) && setup_.duration_s >= dt)
                    setup_.warmup_s = std::min(setup_.warmup_s, setup_.duration_s - dt);
            } else {
                small("Simulazione continua fino ad arresto");
            }
            choose("Guasto", setup_.model.fault, fault_labels);
            if (setup_.model.fault != SLX_SB_NO_FAULT) {
                InputDouble("Inizio [s]", &setup_.model.fault_at_s, 0, 0, "%.1f");
                InputDouble("Durata guasto [s]", &setup_.model.fault_duration_s, 0, 0, "%.1f");
                if (setup_.model.fault_duration_s == 0)
                    small("Guasto persistente");
            }
            EndDisabled();
            Separator();
        }
        const bool board_owns = live && row && (row->order.flags & wire::operator_local);
        live_editor(live, board_owns);
        if (live && live_setup_.timing != wire::Timing::software)
            small(board_owns ? "Sorgente comandi · pannello centralina" : "Sorgente comandi · PC");
        BeginDisabled(compare_pending_ || board_owns);
        SetNextItemWidth(-105);
        if (BeginCombo("Richiesta PC", mode_labels.at(setup_.mode))) {
            for (uint32_t mode = 0; mode < mode_labels.size(); ++mode) {
                const bool local = live_setup_.timing == wire::Timing::software;
                const bool other_strategy = live && local &&
                    ((mode == SLX_SB_SLX && live_setup_.mode != SLX_SB_SLX) ||
                     (mode == SLX_SB_PID && live_setup_.mode == SLX_SB_SLX));
                BeginDisabled(other_strategy ||
                    (mode == SLX_SB_SLX && setup_.timing == wire::Timing::software && !available_slx()));
                if (Selectable(mode_labels[mode], mode == setup_.mode)) {
                    setup_.mode = mode;
                    if (live)
                        worker_.mode = mode;
                }
                EndDisabled();
            }
            EndCombo();
        }
        if (setup_.mode == SLX_SB_MANUAL) {
            double degrees = setup_.manual_rad * to_degrees;
            scalar("Pinna BB", degrees, -25, 25, "%.1f°");
            setup_.manual_rad = degrees / to_degrees;
            if (live) worker_.manual = setup_.manual_rad;
        }
        EndDisabled();
        EndChild();
    }

    void readings(const Row *r)
    {
        const bool estimated = r && (r->command.flags & SLX_SB_EST_VALID);
        auto tile = [](const char *id, const char *label, double reading, const char *fmt) {
            BeginChild(id, {0,73}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, panel_flags);
            small(label);
            heading(value(reading, fmt).c_str(), 23);
            EndChild();
        };
        if (BeginTable("live-values",4)) {
            TableNextColumn();
            tile("ref","Rollio riferimento",r ? r->reference.roll_rad * to_degrees : missing,"%.2f°");
            TableNextColumn();
            tile("imu","Rollio stimato",estimated ? r->command.estimated_roll_rad * to_degrees : missing,"%.2f°");
            TableNextColumn();
            tile("rate","Velocità di rollio",estimated ? r->command.estimated_rate_rad_s * to_degrees : missing,"%.2f°/s");
            TableNextColumn();
            tile("time","Tempo simulato",r ? r->sample.sim_ns * 1e-9 : missing,"%.2f s");
            EndTable();
        }
    }

    void fin_card(const Row *r, bool live)
    {
        BeginChild("actuators", {0,0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                   ImGuiChildFlags_AlwaysUseWindowPadding, panel_flags);
        heading("Attuatori e allarmi", 19);
        if (r) {
            const bool board_owns = r->order.flags & wire::operator_local;
            Text("%s · %s", mode_labels.at(r->command.effective_mode), board_owns ? "Pannello" : "PC");
            if (r->order.flags & wire::operator_mark) {
                SameLine();
                TextColored(color(accent), "Marcatore video");
            }
            small(("Rotta all'onda " + value(r->reference.relative_heading_rad * to_degrees,"%+.0f°") +
                   "  ·  " + value(r->sample.speed_mps,"%.2f m/s")).c_str());
            if (BeginTable("fin-values",3,ImGuiTableFlags_BordersInnerH)) {
                TableSetupColumn("Pinna"); TableSetupColumn("Comando"); TableSetupColumn("Posizione");
                TableHeadersRow();
                for (bool port : {true,false}) {
                    TableNextColumn(); TextUnformatted(port ? "Babordo" : "Tribordo");
                    TableNextColumn(); Text("%+.1f°", (port ? r->command.port_rad : r->command.starboard_rad) * to_degrees);
                    TableNextColumn(); Text("%+.1f°", (port ? r->reference.port_actual_rad : r->reference.starboard_actual_rad) * to_degrees);
                }
                EndTable();
            }
            const auto c = r->command.cause;
            if (c)
                TextColored(color(red), "%s", cause_labels.at(c));
            else
                TextColored(color(teal), "%s", live ? "Nessun allarme" : "Ultimo campione registrato");
            if (r->command.flags & SLX_SB_SPEED_LIMIT) wrapped("Autorità pinne insufficiente alla velocità impostata.");
            if (r->reference.limited_fins || (r->command.flags & SLX_SB_AT_LIMIT))
                TextColored(color(amber), "Corsa o comando al limite");
            if (r->reference.stability_flags & SLX_SB_STABILITY_AVAILABLE)
                Text("Fattore d'immersione BB %.2f · TB %.2f",
                     r->reference.port_authority, r->reference.starboard_authority);
            if (r->command.flags & SLX_SB_ACCEL_REJECTED) small("Accelerometro escluso dalla correzione");
            if (std::isfinite(r->rtt_ms)) Text("RTT %.2f ms", r->rtt_ms);
            BeginDisabled(!live || r->command.state != SLX_SB_AWAIT_ACK);
            if (Button("Riconosci allarme")) worker_.ack = true;
            EndDisabled();
        } else {
            small("In attesa dei campioni");
        }
        EndChild();
    }

    void player(double end)
    {
        constexpr std::array speeds{.25,.5,1.,2.,4.};
        if (advance_playback_) {
            playhead_ = std::min(end, playhead_ + GetIO().DeltaTime * speeds[playback_speed_]);
            if (playhead_ >= end)
                advance_playback_ = false;
        }
        if (Button(advance_playback_ ? "Pausa" : "Riproduci", {108,0})) {
            if (playhead_ >= end) playhead_ = 0;
            advance_playback_ = !advance_playback_;
        }
        SameLine();
        SetNextItemWidth(102);
        choose("##velocita-replay", playback_speed_, std::array{"0,25×","0,5×","1×","2×","4×"});
        SameLine();
        SetNextItemWidth(std::min(570.f, GetContentRegionAvail().x));
        scalar("##tempo-replay", playhead_, 0., std::max(end,.001), "%.2f s");
        if (end >= 1200.) {
            SetNextItemWidth(165);
            if (InputDouble("Vai a [s]", &playhead_, 0., 0., "%.2f"))
                playhead_ = std::isfinite(playhead_) ? std::clamp(playhead_, 0., end) : 0.;
        }
    }

    void comparison()
    {
        heading("Confronto A/B", 27);
        SameLine();
        BeginDisabled(worker_.active);
        if (primary("Esegui confronto")) start(true);
        SameLine();
        if (Button("Apri confronto")) OpenPopup("Carica confronto");
        if (BeginPopupModal("Carica confronto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            SetNextItemWidth(465);
            InputTextWithHint("##confronto-percorso", "Cartella contenente off, pid e slx", comparison_path_.data(),
                              comparison_path_.size());
            if (primary("Carica replay"))
                action([&] { load_comparison(path_from(comparison_path_.data())); message_.clear(); CloseCurrentPopup(); });
            SameLine();
            if (Button("Annulla")) CloseCurrentPopup();
            if (!message_.empty()) wrapped(message_.c_str());
            EndPopup();
        }
        SameLine();
        BeginDisabled(!available_slx());
        Checkbox("Includi SLX", &use_slx_);
        EndDisabled();
        EndDisabled();
        small(compare_pending_ ? message_.c_str() : "Stesso scenario, seed e condizioni iniziali nei diversi bracci.");
        if (!pair_) return;
        auto &p = *pair_;
        const Session &controlled = right_slx_ && p.slx ? *p.slx : p.pid;
        const auto &metrics = right_slx_ && p.slx ? slx_metrics_ : pid_metrics_;
        if (p.slx) Checkbox("Mostra SLX sul lato destro", &right_slx_);
        const double end = std::min(duration(p.off), duration(controlled));
        playhead_ = std::min(playhead_, end);
        player(end);
        SameLine();
        if (Button("Rapporto"))
            action([&] { open_target(p.off.directory.parent_path() / "comparison.html"); });
        if (p.off.rows.empty() || controlled.rows.empty()) {
            wrapped("Una delle registrazioni è priva di campioni. Apri il rapporto per lo stato dei bracci.");
            return;
        }
        const bool valid = p.off.manifest.value("status","") == "complete" &&
                           controlled.manifest.value("status","") == "complete" &&
                           p.off.rows.size() == controlled.rows.size();
        const auto gain = valid && !std::isfinite(off_metrics_.first_capsize_s) &&
                                 !std::isfinite(metrics.first_capsize_s) ?
                                 improvement(off_metrics_.rms_deg, metrics.rms_deg) : missing;
        Text("RMS spento %s   ·   RMS %s %s   ·   Δ %s",
             value(off_metrics_.rms_deg, "%.2f°").c_str(), right_slx_ ? "SLX" : "PID",
             value(metrics.rms_deg, "%.2f°").c_str(), value(gain, "%+.1f%%").c_str());
        if (std::isfinite(off_metrics_.first_capsize_s) || std::isfinite(metrics.first_capsize_s))
            TextColored(color(red), "Ribaltamento registrato · A %s · B %s",
                        value(off_metrics_.first_capsize_s, "%.2f s").c_str(),
                        value(metrics.first_capsize_s, "%.2f s").c_str());
        if (BeginTable("compare-scenes", 2)) {
            TableNextColumn();
            viewport("compare-off", recorded_view(p.off.rows[row_at(p.off,playhead_)],p.off.scenario),
                     0,{GetContentRegionAvail().x,310},"A · SPENTO · REPLAY");
            TableNextColumn();
            viewport("compare-on", recorded_view(controlled.rows[row_at(controlled,playhead_)],controlled.scenario),
                     1,{GetContentRegionAvail().x,310},right_slx_ ? "B · SLX · REPLAY" : "B · PID · REPLAY");
            EndTable();
        }
        replay_plot_.from_session(p.off, 3, &controlled);
        replay_plot_.draw("compare-chart", 220, "Spento", right_slx_ ? "SLX" : "PID", true, false, false, playhead_);
    }

    void sessions()
    {
        heading("Sessioni", 27);
        SameLine();
        if (Button("Apri cartella sessioni"))
            action([&] { open_target(root_ / "sessions"); });
        SetNextItemWidth(std::min(690.f, GetContentRegionAvail().x - 83));
        InputTextWithHint("##path", "Percorso cartella registrazione", session_path_.data(), session_path_.size());
        SameLine();
        if (primary("Apri replay")) {
            action([&] {
                Session imported = replay(path_from(session_path_.data()));
                session_.emplace(std::move(imported));
                session_metrics_ = measure(*session_);
                playhead_ = 0;
                advance_playback_ = false;
                message_.clear();
            });
        }
        if (!message_.empty()) small(message_.c_str());
        if (!session_) return;
        auto &s = *session_;
        Text("%s  ·  %s  ·  %zu campioni", utf8(s.directory.filename()).c_str(),
             s.partial ? "incompleta" : s.manifest.value("status", "").c_str(), s.rows.size());
        if (s.rows.empty()) return;
        player(duration(s));
        SameLine();
        BeginDisabled(!fs::is_regular_file(s.directory / "analysis-v1/report.html"));
        if (Button("Rapporto"))
            action([&] { open_target(s.directory / "analysis-v1/report.html"); });
        EndDisabled();
        const auto &r = s.rows[row_at(s, playhead_)];
        if (s.curve.count) {
            auto jump = [&](const char *label, double time) {
                if (std::isfinite(time) && Button(label)) {
                    playhead_ = time;
                    advance_playback_ = false;
                }
                if (std::isfinite(time)) SameLine();
            };
            jump("Prima immersione", session_metrics_.first_downflood_s);
            jump("Perdita raddrizzamento", session_metrics_.first_righting_loss_s);
            jump("Ribaltamento", session_metrics_.first_capsize_s);
            const auto state = stability_state(r.reference.stability_flags);
            TextColored(color(state.second), "%s", state.first);
            if (Button("Analizza stabilità del replay")) {
                inspect_replay_ = true;
                page_ = 6;
            }
        }
        if (BeginTable("replay-panels", 2, ImGuiTableFlags_SizingStretchProp)) {
            TableSetupColumn("Scena", ImGuiTableColumnFlags_WidthStretch, 2.f);
            TableSetupColumn("Dati", ImGuiTableColumnFlags_WidthStretch, 1.f);
            TableNextColumn();
            viewport("session-vessel", recorded_view(r,s.scenario),0,{GetContentRegionAvail().x,320},"REPLAY · SINTETICA");
            TableNextColumn();
            heading("Metriche della sessione", 19);
            Text("Rollio RMS %s", value(session_metrics_.rms_deg,"%.2f°").c_str());
            Text("Picco %s", value(session_metrics_.peak_deg,"%.2f°").c_str());
            Text("Errore stima RMS %s", value(session_metrics_.estimate_rms_deg,"%.2f°").c_str());
            Text("Attività pinne %s", value(session_metrics_.activity,"%.3f").c_str());
            Text("IMU invalidi %zu", session_metrics_.invalid_imu);
            Text("Scadenze mancate %zu", session_metrics_.deadline_missed);
            fin_card(&r, false);
            EndTable();
        }
        if (Checkbox("Traccia pinne", &fins_plot_) && fins_plot_)
            show_replay_energy_ = false;
        const bool can_show_energy = (r.reference.stability_flags & SLX_SB_STABILITY_AVAILABLE) != 0;
        if (can_show_energy) {
            SameLine();
            if (Checkbox("Riserva ed energia", &show_replay_energy_) && show_replay_energy_)
                fins_plot_ = false;
        }
        bool unwrapped = false;
        const bool energy = can_show_energy && show_replay_energy_;
        if (fins_plot_) {
            SameLine(); Checkbox("Tribordo", &starboard_plot_);
        } else if (!energy && std::isfinite(session_metrics_.first_capsize_s)) {
            SameLine(); Checkbox("Rollio continuo", &unwrapped_roll_);
            unwrapped = unwrapped_roll_;
        }
        if (energy) {
            replay_energy_.from_session(s);
            replay_energy_.draw("energy-replay", 250, false, playhead_);
        } else {
            replay_plot_.from_session(s, fins_plot_ ? (starboard_plot_ ? 2 : 1) : unwrapped ? 4 : 0);
            replay_plot_.draw(unwrapped ? "roll-continuous" : "recorded-history", 220, fins_plot_ ? "Posizione" : "Riferimento",
                              fins_plot_ ? "Comando" : "Stima IMU", false, fins_plot_, false, playhead_);
        }
        heading("Eventi registrati",19);
        if (s.event_count > s.events.size())
            Text("Ultimi %zu di %zu · serie completa in events.csv", s.events.size(), s.event_count);
        if (BeginTable("events",4,ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            TableSetupColumn("Tempo [s]"); TableSetupColumn("Origine"); TableSetupColumn("Evento"); TableSetupColumn("Codice");
            TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(s.events.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto &e = s.events[static_cast<size_t>(i)];
                    TableNextColumn();
                    PushID(i);
                    if (Selectable(value(e.sim_ns * 1e-9, "%.2f").c_str())) {
                        playhead_ = std::min(duration(s), e.sim_ns * 1e-9);
                        advance_playback_ = false;
                    }
                    PopID();
                    TableNextColumn(); TextUnformatted(e.origin.c_str());
                    TableNextColumn();
                    TextUnformatted(e.name == "video_mark" ? "Marcatore video" :
                        e.name == "effective_mode" ? "Modalità effettiva" :
                        e.name == "takes_control" ? "Comando al pannello" :
                        e.name == "returns_control" ? "Comando al PC" :
                        e.name == "live_update" ? "Condizioni applicate" :
                        e.name == "live_update_rejected" ? "Intervento respinto" :
                        e.name == "fault_injected" ? "Iniezione guasto" :
                        e.name == "fault_cancelled" ? "Rimozione guasto" :
                        e.name == "navigation_target" ? "Obiettivo manovra" : e.name.c_str());
                    TableNextColumn();
                    if ((e.name == "video_mark" || e.name == "effective_mode") && e.code < mode_labels.size())
                        TextUnformatted(mode_labels[e.code]);
                    else if ((e.name == "fault_injected" || e.name == "fault_start" || e.name == "fault_end") &&
                             e.code < fault_labels.size())
                        TextUnformatted(fault_labels[e.code]);
                    else
                        Text("%u",e.code);
                }
            }
            EndTable();
        }
    }

    void presentation()
    {
        heading("Presentazione tecnica",27);
        if (!renderer_ || !renderer_->has_presentation()) {
            small("Vista 3D della nave e delle pinne");
            camera_buttons();
            View view;
            view.scenario = setup_.model;
            viewport("presentation-vessel", view, 0, {GetContentRegionAvail().x,
                std::max(335.f, GetContentRegionAvail().y - 6)}, "ISPEZIONE 3D");
            return;
        }
        small("Clip illustrative del modello 3D · Durata 8 s");
        if (Checkbox("Gruppo pinna e attuatore", &assembly_)) {
            clip_ = assembly_ ? 2 : 0;
            demo_time_ = 0;
            animate_demo_ = false;
        }
        BeginDisabled(assembly_);
        SetNextItemWidth(286);
        if (choose("Clip", clip_, clip_labels)) {
            demo_time_ = 0;
            animate_demo_ = false;
        }
        EndDisabled();
        SameLine();
        BeginDisabled(clip_ == 4 && demo_time_ >= 8);
        if (primary(animate_demo_ ? "Pausa" : "Riproduci")) animate_demo_ = !animate_demo_;
        EndDisabled();
        SameLine();
        if (Button("Ripristina clip")) { demo_time_ = 0; animate_demo_ = false; }
        camera_buttons();
        const bool once = clip_ == 4;
        if (animate_demo_) {
            demo_time_ += GetIO().DeltaTime;
            if (demo_time_ >= 8) {
                if (once) { demo_time_ = 8; animate_demo_ = false; }
                else demo_time_ = std::fmod(demo_time_,8.);
            }
        }
        SetNextItemWidth(370);
        scalar("##tempo-clip", demo_time_, 0, 8, "%.2f s");
        SameLine();
        small(once ? "Riproduzione singola · pinna BB ferma dal secondo 3" : "Riproduzione ciclica");
        View view;
        view.clip = clips[clip_];
        view.clip_s = demo_time_;
        view.assembly = assembly_;
        viewport("presentation-vessel",view,0,{GetContentRegionAvail().x,
            std::max(335.f,GetContentRegionAvail().y - 6)},"CLIP ILLUSTRATIVA");
    }

    void refresh_curve(const Scenario &settings)
    {
        const auto key = configuration(settings).at("model").dump();
        if (curve_key_ == key)
            return;
        curve_key_ = key;
        try {
            Model preview(core_file(executable_, true), settings.model);
            preview_curve_ = preview.stability_curve();
            curve_error_.clear();
        } catch (const std::exception &e) {
            preview_curve_ = {};
            curve_error_ = e.what();
        }
    }

    void gz_plot(const SLX_SB_StabilityCurve &curve, const Row *row)
    {
        if (curve.count == 0) {
            wrapped(curve_error_.c_str());
            return;
        }
        std::array<double, SLX_SB_GZ_SAMPLES> heels{}, arms{};
        for (uint32_t i = 0; i < curve.count; ++i) {
            heels[i] = curve.points[i].heel_rad * to_degrees;
            arms[i] = curve.points[i].arm_m;
        }
        if (ImPlot::BeginPlot("##gz", {-1,258}, ImPlotFlags_NoMenus)) {
            ImPlot::SetupAxes("Sbandamento [°]", "Braccio GZ [m]");
            ImPlot::SetupAxisLimits(ImAxis_X1, -180, 180, ImPlotCond_Once);
            ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
            ImPlot::SetNextLineStyle(color(accent), 2.7f);
            ImPlot::PlotLine("GZ iniziale", heels.data(), arms.data(), static_cast<int>(curve.count));
            if (row) {
                const auto &ref = row->reference;
                const double heel = ref.roll_rad * to_degrees;
                const double limit = ref.positive_range_rad * to_degrees;
                const double boundaries[]{-limit, limit};
                ImPlot::SetNextLineStyle(color(amber), 1.4f);
                ImPlot::PlotInfLines("Limite attuale GZ positivo", boundaries, 2);
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6, color(teal));
                ImPlot::PlotScatter("GZ al campione", &heel, &ref.gz_m, 1);
            }
            ImPlot::EndPlot();
        }
    }

    void stability_loads(const Row *row)
    {
        heading("Bilancio dei momenti", 19);
        if (!row) {
            small("I momenti e l'acqua imbarcata compaiono durante una prova.");
            return;
        }
        const auto &ref = row->reference;
        const std::array labels{"Onde", "Pinne", "Galleggiamento · GZ", "Smorzamento", "Carico, acqua e virata"};
        const std::array loads{ref.wave_moment_nm, ref.fin_moment_nm, ref.righting_moment_nm,
                               ref.damping_moment_nm, ref.heel_moment_nm};
        const std::array colors{accent, teal, 0x317aa4u, 0x8090a8u, amber};
        double scale = 1;
        for (const auto moment : loads)
            scale = std::max(scale, std::abs(moment));
        if (BeginTable("moment-balance", 3, ImGuiTableFlags_BordersInnerH)) {
            TableSetupColumn("Fonte", ImGuiTableColumnFlags_WidthFixed, 165.f);
            TableSetupColumn("Momento", ImGuiTableColumnFlags_WidthStretch);
            TableSetupColumn("Valore", ImGuiTableColumnFlags_WidthFixed, 99.f);
            for (size_t i = 0; i < labels.size(); ++i) {
                TableNextColumn();
                TextUnformatted(labels[i]);
                TableNextColumn();
                const auto p = GetCursorScreenPos();
                const float width = std::max(10.f, GetContentRegionAvail().x);
                const float center = p.x + width * .5f;
                const float end = center + static_cast<float>(.49 * width * loads[i] / scale);
                auto *draw = GetWindowDrawList();
                draw->AddRectFilled({p.x,p.y+5},{p.x+width,p.y+17},rgba(0xeef1f7),4);
                draw->AddRectFilled({std::min(center,end),p.y+5},{std::max(center,end),p.y+17},rgba(colors[i]),3);
                draw->AddLine({center,p.y+2},{center,p.y+20},rgba(navy),1);
                Dummy({width,22});
                TableNextColumn();
                Text("%+.1f kNm", loads[i] * .001);
            }
            EndTable();
        }
        Text("Riserva %.1f kJ  ·  Energia di rollio %.1f kJ", ref.reserve_j * .001, ref.kinetic_j * .001);
        Text("GM %.2f m  ·  Acqua imbarcata %.0f kg", ref.gm_m, ref.flooded_kg);
    }

    void stability()
    {
        heading("Stabilità e scenari critici", 27);
        const bool running = worker_.active;
        const bool available_replay = session_ && session_->curve.count && !session_->rows.empty() && !running;
        if (available_replay)
            Checkbox("Analizza sessione registrata", &inspect_replay_);
        const bool replaying = available_replay && inspect_replay_;
        const auto &profile = replaying ? session_->scenario.profile : setup_.profile;
        auto scenario = std::find_if(presets.begin(), presets.end(), [&](const auto &item) { return item.id == profile; });
        if (!replaying) {
            BeginDisabled(running);
            SetNextItemWidth(339);
            if (BeginCombo("Scenario", scenario != presets.end() ? scenario->title : "Curva GZ personalizzata")) {
                for (const auto &item : presets) {
                    if (Selectable(item.title, setup_.profile == item.id)) {
                        auto preset = scenario_preset(item.id);
                        setup_.model = preset.model;
                        setup_.profile = preset.profile;
                        scenario = std::find_if(presets.begin(), presets.end(),
                            [&](const auto &p) { return p.id == setup_.profile; });
                    }
                }
                EndCombo();
            }
            SameLine();
            if (primary("Avvia scenario"))
                start(false);
            SameLine();
            if (Button("Confronta A/B")) {
                page_ = 1;
                start(true);
            }
            EndDisabled();
        } else {
            heading(scenario != presets.end() ? scenario->title : "Curva GZ personalizzata", 18);
        }
        if (scenario != presets.end())
            small(scenario->observation);
        Row recorded;
        if (replaying) {
            player(duration(*session_));
            recorded = session_->rows[row_at(*session_, playhead_)];
        }
        const Row *row = replaying ? &recorded : live_rows_.empty() ? nullptr : &live_rows_.back();
        const bool inspect = row && (replaying || running ||
            configuration(live_setup_).at("model") == configuration(setup_).at("model"));
        if (!replaying)
            refresh_curve(inspect ? live_setup_ : setup_);
        if (BeginTable("stability-grid",2,ImGuiTableFlags_SizingStretchProp)) {
            TableSetupColumn("GZ", ImGuiTableColumnFlags_WidthStretch, 2.15f);
            TableSetupColumn("Configurazione", ImGuiTableColumnFlags_WidthStretch, 1.f);
            TableNextColumn();
            BeginChild("gz-card", {0,0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                       ImGuiChildFlags_AutoResizeY, panel_flags);
            heading("Curva di raddrizzamento", 20);
            if (inspect) {
                const auto state = stability_state(row->reference.stability_flags);
                TextColored(color(state.second), "%s", state.first);
            }
            gz_plot(replaying ? session_->curve : preview_curve_, inspect ? row : nullptr);
            Separator();
            stability_loads(inspect ? row : nullptr);
            if (inspect) {
                Spacing();
                heading("Riserva ed energia nel tempo", 19);
                if (replaying) {
                    replay_energy_.from_session(*session_);
                    replay_energy_.draw("stability-replay-energy", 214, false, playhead_);
                } else {
                    live_energy_.from_live(live_rows_);
                    live_energy_.draw("stability-live-energy", 214, true);
                }
            }
            EndChild();
            TableNextColumn();
            BeginChild("hydrostatic-settings", {0,0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                       ImGuiChildFlags_AutoResizeY, panel_flags);
            if (replaying) {
                const auto &m = session_->scenario.model;
                const auto &v = recorded.conditions;
                heading("Carico e aperture", 19);
                Text("Fattore di carico %.2f", v.load_ratio);
                Text("Quota baricentro %+.2f m", v.cg_rise_m);
                Text("Superficie libera −%.2f m GM", v.free_surface_loss_m);
                Text("Carico trasversale %+.2f m", v.load_offset_m);
                Text("Apertura a secco %.0f°", m.downflood_rad * to_degrees);
                Text("Area di ingresso %.3f m²", m.flood_area_m2);
                Text("Capacità acqua %.0f kg", m.flood_capacity_kg);
                Separator();
                heading("Eccitazione", 18);
                Text("Onde obiettivo %.2f m · %.2f s", v.wave_height_m, v.wave_period_s);
                Text("Modulazione GM %.2f", m.gm_modulation);
                if (m.packet_height_m > 0)
                    Text("Gruppo d'onda %.2f m a %.1f s", m.packet_height_m, m.packet_center_s);
                Separator();
                small(m.gz_point_count ? "Curva GZ importata" : "Curva GZ parametrica");
            } else {
                stability_settings(running);
            }
            EndChild();
            EndTable();
        }
    }

    void stability_settings(bool running)
    {
        heading("Carico e aperture",19);
        BeginDisabled(running);
        auto &m = setup_.model;
        auto angle = [](const char *title, double &value, double lo, double hi) {
            double degrees = value * to_degrees;
            scalar(title, degrees, lo, hi, "%.0f°");
            value = degrees / to_degrees;
        };
        SetNextItemWidth(-147); scalar("Superficie libera", m.free_surface_loss_m, 0, .9, "−%.2f m GM");
        SetNextItemWidth(-147); scalar("Carico trasversale", m.load_offset_m, -.7, .7, "%+.2f m");
        BeginDisabled(m.gz_point_count != 0);
        SetNextItemWidth(-147); angle("Limite GZ a secco", m.positive_range_rad, 32, 165);
        EndDisabled();
        SetNextItemWidth(-147); angle("Apertura a secco", m.downflood_rad, 15, 83);
        SetNextItemWidth(-147); scalar("Area di ingresso", m.flood_area_m2, 0, .35, "%.3f m²");
        SetNextItemWidth(-147); scalar("Capacità acqua", m.flood_capacity_kg, 1, 16000, "%.0f kg");
        Separator();
        heading("Eccitazione",18);
        SetNextItemWidth(-147); scalar("Modulazione GM", m.gm_modulation, 0, .8, "%.2f");
        SetNextItemWidth(-147); scalar("Gruppo d’onda", m.packet_height_m, 0, 8, "%.2f m");
        if (m.packet_height_m > 0) {
            SetNextItemWidth(-147); InputDouble("Centro gruppo [s]", &m.packet_center_s, 0, 0, "%.1f");
            SetNextItemWidth(-147); scalar("Larghezza gruppo", m.packet_width_s, .25, 60, "%.1f s");
        }
        Separator();
        heading("Curva da dati del cliente", 18);
        small(m.gz_point_count ? "Curva importata · profilo simmetrico" : "Curva parametrica del profilo");
        SetNextItemWidth(-1);
        InputTextWithHint("##gz-csv", "Percorso CSV heel_deg,gz_m", gz_path_.data(), gz_path_.size());
        if (Button("Importa GZ")) {
            action([&] { import_curve(setup_, path_from(gz_path_.data())); message_.clear(); });
        }
        if (m.gz_point_count) {
            SameLine();
            if (Button("Rimuovi curva")) { m.gz_point_count = 0; setup_.profile = "workboat"; }
        }
        EndDisabled();
        if (!message_.empty())
            wrapped(message_.c_str());
    }

    void settings()
    {
        heading("Impostazioni",27);
        if (worker_.active && !compare_pending_ && Button("Modifica condizioni in corso")) {
            open_live_editor_ = true;
            page_ = 0;
        }
        SetNextItemWidth(265);
        BeginDisabled(worker_.active);
        auto &m = setup_.model;
        InputScalar("Seed del mare", ImGuiDataType_U64, &m.seed);
        SetNextItemWidth(265); scalar("Fattore di carico",m.load_ratio,.65,1.65,"%.2f");
        SetNextItemWidth(265); scalar("Quota baricentro",m.cg_rise_m,-.20,.90,"%+.2f m");
        SetNextItemWidth(265); scalar("Ritardo attuatore",m.actuator_delay_s,0,1,"%.2f s");
        SetNextItemWidth(265); InputDouble("Esclusione transitorio [s]", &setup_.warmup_s, 0, 0, "%.1f");
        SetNextItemWidth(265); InputScalar("Porta UDP", ImGuiDataType_U16, &setup_.port);
        SetNextItemWidth(265); InputText("Indirizzo controllore", peer_.data(), peer_.size());
        SetNextItemWidth(265);
        constexpr uint32_t min = 20, max = 1000;
        SliderScalar("Timeout",ImGuiDataType_U32,&setup_.timeout_ms,&min,&max,"%u ms",ImGuiSliderFlags_AlwaysClamp);
        EndDisabled();
        if (Checkbox("Rendering 3D", &visuals_) && visuals_ && !renderer_)
            load_renderer();
        if (primary("Salva impostazioni")) {
            action([&] {
                setup_.peer = peer_.data();
                validate(setup_);
                save_json(root_ / "settings.json", {{"copyright",copyright},{"scenario",configuration(setup_)},
                                                   {"render_3d",visuals_}});
                message_ = "Impostazioni salvate.";
            });
        }
        if (!message_.empty()) wrapped(message_.c_str());
    }

    void about()
    {
        heading("SLX SeaBalance",27);
        small("Versione 0.3.0 · Modello ABI 2 · SLXB 1 · Asset 0.1.0");
        wrapped("Dimostratore di stabilizzazione attiva del rollio per un workboat in navigazione.");
        wrapped("Marco Pezzullo · Silicon LogiX · © 2026");
        Separator();
        if (Button("Sito e contatti"))
            action([&] {
#ifdef _WIN32
                ShellExecuteW(nullptr,L"open",L"https://www.siliconlogix.it/",nullptr,nullptr,SW_SHOWNORMAL);
#else
                open_target(fs::path("https://www.siliconlogix.it/"));
#endif
            });
        SameLine();
        if (Button("Licenza"))
            action([&] { open_target(executable_ / "LICENSE"); });
        SameLine();
        if (Button("Avvisi di terze parti"))
            action([&] { open_target(executable_ / "THIRD_PARTY_NOTICES.md"); });
        Spacing();
        heading("Su commessa",19);
        wrapped("Identificazione e taratura su dati indipendenti, controllo predittivo o adattativo, "
                "coordinamento di attuatori, stabilizzazione da fermo, modellazione degli azionamenti "
                "e integrazione con sensori e reti di bordo.");
    }
};

Launch launch_args(int argc, char **argv)
{
    Launch launch;
    for (int i = 1; i < argc; ++i) {
        const auto option = std::string_view(argv[i]);
        if (++i >= argc) throw std::invalid_argument("Missing launch argument");
        if (option == "--screenshot") launch.shot = path_from(argv[i]);
        else if (option == "--replay") launch.replay_dir = path_from(argv[i]);
        else if (option == "--compare") launch.ab_dir = path_from(argv[i]);
        else if (option == "--page") launch.page = std::clamp(std::stoi(argv[i]),0,6);
        else if (option == "--clip") launch.clip = static_cast<uint32_t>(std::clamp(std::stoi(argv[i]),0,4));
        else if (option == "--at") launch.at = std::max(0., std::stod(argv[i]));
        else if (option == "--width") launch.width = std::clamp(std::stoi(argv[i]),1024,1920);
        else if (option == "--height") launch.height = std::clamp(std::stoi(argv[i]),720,1200);
        else throw std::invalid_argument("Unknown launch option");
    }
    return launch;
}

int desktop(int argc, char **argv)
{
    try {
        const auto launch = launch_args(argc,argv);
        glfwSetErrorCallback([](int, const char *description) { std::cerr << description << '\n'; });
        if (!glfwInit())
            throw std::runtime_error("GLFW non disponibile. Usa slx-seabalance-cli per il banco senza grafica.");
        struct Glfw { ~Glfw() { glfwTerminate(); } } glfw;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        if (!launch.shot.empty()) glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        auto *window = glfwCreateWindow(launch.width,launch.height,"SLX SeaBalance",nullptr,nullptr);
        if (!window)
            throw std::runtime_error("La scena richiede OpenGL 3.3. Usa slx-seabalance-cli per il banco senza grafica.");
        glfwSetWindowSizeLimits(window,1024,720,GLFW_DONT_CARE,GLFW_DONT_CARE);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(launch.shot.empty() ? 1 : 0);
        gl::load();
        IMGUI_CHECKVERSION();
        CreateContext();
        ImPlot::CreateContext();
        style();
        ImGui_ImplGlfw_InitForOpenGL(window,true);
        ImGui_ImplOpenGL3_Init("#version 330");
        struct Ui {
            ~Ui() {
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImPlot::DestroyContext();
                DestroyContext();
            }
        } ui;
        App app(launch);
        unsigned frames = 0;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (glfwGetWindowAttrib(window,GLFW_ICONIFIED)) {
                glfwWaitEventsTimeout(.10);
                continue;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            NewFrame();
            app.frame();
            Render();
            int w{}, h{};
            glfwGetFramebufferSize(window,&w,&h);
            gl::Viewport(0,0,w,h);
            gl::ClearColor(.95f,.96f,.98f,1.f);
            gl::Clear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(GetDrawData());
            if (!launch.shot.empty() && ++frames >= 8) {
                if (gl::GetError() != GL_NO_ERROR)
                    throw std::runtime_error("OpenGL reported an error during the GUI smoke test");
                Renderer::screenshot(launch.shot,w,h);
                break;
            }
            glfwSwapBuffers(window);
        }
        return 0;
    } catch (const std::exception &e) {
#ifdef _WIN32
        if (argc > 1)
            std::cerr << e.what() << '\n';
        else
            MessageBoxA(nullptr,e.what(),"SLX SeaBalance",MB_ICONERROR);
#else
        std::cerr << e.what() << '\n';
#endif
        return 1;
    }
}
} // namespace
} // namespace slx::sb

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return slx::sb::desktop(__argc,__argv);
}
#else
int main(int argc, char **argv) { return slx::sb::desktop(argc,argv); }
#endif
