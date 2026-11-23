// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "session.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace slx::sb {
using json = nlohmann::json;
namespace {
struct TimingHistogram {
    static constexpr double scale = 64.;
    std::array<size_t, 1024> bins{};
    size_t count{};
    double maximum{};

    void add(double v)
    {
        const auto bin = std::min(bins.size() - 1,
            static_cast<size_t>(scale * std::log1p(std::min(v, 1e6) / .01)));
        ++bins[bin];
        ++count;
        maximum = std::max(maximum, v);
    }

    double p95() const
    {
        if (!count)
            return missing;
        const auto required = count - count / 20;
        size_t sum = 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            sum += bins[i];
            if (sum >= required)
                return i + 1 == bins.size() ? maximum : std::min(maximum, .01 * std::expm1((i + 1) / scale));
        }
        return maximum;
    }
};

double lag(const Session &s, size_t start)
{
    const auto &rows = s.rows;
    if (rows.size() <= start + 150)
        return missing;
    struct Correlation { double a{}, b{}, aa{}, bb{}, ab{}, n{}; };
    constexpr size_t radius = 25, window = 2 * radius + 1;
    std::array<Correlation, window> correlations{};
    std::array<double, window> truth{}, estimate{};
    for (size_t i = start; i < rows.size(); ++i) {
        const auto row = rows[i];
        const auto &estimated = row.command;
        truth[i % window] = row.reference.roll_rad;
        estimate[i % window] = (estimated.flags & SLX_SB_EST_VALID) ? estimated.estimated_roll_rad : missing;
        if (i < start + window - 1)
            continue;
        const size_t center = i - radius;
        const auto x = truth[center % window];
        for (size_t j = 0; j < window; ++j) {
            const auto y = estimate[(i + j + 1) % window];
            if (!std::isfinite(y))
                continue;
            auto &c = correlations[j];
            c.a += x; c.b += y; c.aa += x * x; c.bb += y * y; c.ab += x * y; ++c.n;
        }
    }
    double best = -1, answer = missing;
    for (size_t j = 0; j < correlations.size(); ++j) {
        const auto [a,b,aa,bb,ab,n] = correlations[j];
        const double variance_a = aa * n - a * a, variance_b = bb * n - b * b;
        if (n < 100 || std::min(variance_a, variance_b) < n * n * 1e-8)
            continue;
        const double score = (n * ab - a * b) / std::sqrt(variance_a * variance_b);
        if (score > best) {
            best = score;
            answer = (static_cast<int>(j) - static_cast<int>(radius)) * dt;
        }
    }
    return best > .5 ? answer : missing;
}

std::string fmt(double value, int precision = 2)
{
    if (!std::isfinite(value))
        return "N/D";
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(precision) << value;
    return s.str();
}

std::string escaped(std::string_view text)
{
    std::string result;
    for (const auto c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result += c; break;
        }
    }
    return result;
}

void html_start(std::ostream &out, const char *title)
{
    out << "<!doctype html>\n<!-- " << copyright << " / SLX SeaBalance -->\n"
        << "<html lang=\"it\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
        << "<title>" << title << " | SLX SeaBalance</title>"
        << "<style>body{font:16px system-ui;background:#f3f5f9;color:#192541;margin:0}"
           "header{background:#111c31;color:white;padding:24px max(5vw,20px)}"
           "main{max-width:1150px;margin:auto;padding:28px 20px}section{background:white;border:1px solid #e3e8f0;"
           "border-radius:12px;margin:18px 0;padding:20px}table{width:100%;border-collapse:collapse}"
           "th,td{text-align:left;padding:.55rem;border-bottom:1px solid #e3e8f0;font-variant-numeric:tabular-nums}"
           "svg{width:100%;font-size:12px}.pid{color:#5354e8}.slx{color:#087762}.off{color:#9a6823}.scroll{overflow:auto}"
           "</style><header>Silicon LogiX &nbsp; / &nbsp; SLX SeaBalance</header><main><h1>"
        << title << "</h1>\n";
}

void chart(std::ostream &out, const Session &s, const Session *other = nullptr)
{
    if (s.rows.empty())
        return;
    double max_angle = 1.;
    for (size_t i = 0; i < s.rows.size(); ++i)
        max_angle = std::max(max_angle, std::abs(s.rows[i].reference.roll_rad * to_degrees));
    if (other) {
        for (size_t i = 0; i < other->rows.size(); ++i)
            max_angle = std::max(max_angle, std::abs(other->rows[i].reference.roll_rad * to_degrees));
    } else {
        for (size_t i = 0; i < s.rows.size(); ++i) {
            const auto r = s.rows[i];
            if (r.command.flags & SLX_SB_EST_VALID)
                max_angle = std::max(max_angle, std::abs(r.command.estimated_roll_rad * to_degrees));
        }
    }
    const auto last = std::max<size_t>(1, std::max(s.rows.size(), other ? other->rows.size() : 0) - 1);
    out << "<section><h2>Rollio nel tempo</h2><p>"
        << (other ? "<span class=\"off\">Controllo spento</span> · <span class=\"pid\">PID</span>" :
                    "<span class=\"pid\">Riferimento</span> · <span class=\"slx\">Stima</span>")
        << "</p><svg viewBox=\"0 0 860 218\" role=\"img\" aria-label=\"Rollio in gradi nel tempo\">"
           "<path d=\"M64 12V184H840 M64 98H840\" stroke=\"#c8d1e3\" fill=\"none\"/>"
        << "<text x=\"2\" y=\"22\">" << fmt(max_angle, 1) << " °</text><text x=\"2\" y=\"184\">-"
        << fmt(max_angle, 1) << " °</text><text x=\"64\" y=\"206\">0 s</text><text x=\"775\" y=\"206\">"
        << fmt(last * dt, 1) << " s</text>";
    auto trace = [&](const Session &source, const char *color, bool estimate) {
        out << "<path fill=\"none\" stroke=\"" << color << "\" stroke-width=\"1.5\" d=\"";
        bool new_segment = true;
        const size_t step = std::max<size_t>(1, source.rows.size() / 1400);
        for (size_t i = 0; i < source.rows.size(); i += std::min(step, source.rows.size() - i)) {
            const auto r = step > 8 ? source.rows.direct(i) : source.rows[i];
            if (estimate && !(r.command.flags & SLX_SB_EST_VALID)) {
                new_segment = true;
                continue;
            }
            const double angle = (estimate ? r.command.estimated_roll_rad : r.reference.roll_rad) * to_degrees;
            out << (new_segment ? 'M' : 'L') << fmt(64 + 776. * i / last, 1) << ' '
                << fmt(98 - 86 * angle / max_angle, 1) << ' ';
            new_segment = false;
        }
        out << "\"/>";
    };
    trace(s, other ? "#9a6823" : "#5354e8", false);
    if (other)
        trace(*other, "#5354e8", false);
    else
        trace(s, "#087762", true);
    out << "</svg></section>\n";
}
} // namespace

Metrics measure(const Session &s)
{
    Metrics result;
    const auto start = static_cast<size_t>(std::max(0., std::ceil(s.scenario.warmup_s / dt - 1e-8)));
    double energy = 0, peak = 0, estimate_energy = 0, movement = 0, saturated = 0;
    TimingHistogram rtt, jitter;
    SLX_SB_Reference previous{};
    for (size_t i = 0; i < s.rows.size(); ++i) {
        const auto &row = s.rows[i];
        const auto &r = row.reference;
        const auto &c = row.command;
        if (r.stability_flags & SLX_SB_STABILITY_AVAILABLE) {
            auto first = [&](uint32_t flag, double &v) {
                if ((r.stability_flags & flag) && !std::isfinite(v))
                    v = r.sim_ns * 1e-9;
            };
            first(SLX_SB_OPENING_IMMERSED, result.first_downflood_s);
            first(SLX_SB_RIGHTING_LOST, result.first_righting_loss_s);
            first(SLX_SB_CAPSIZE_RECORDED, result.first_capsize_s);
            result.min_gm_m = std::isfinite(result.min_gm_m) ? std::min(result.min_gm_m, r.gm_m) : r.gm_m;
            result.peak_flooded_kg = std::isfinite(result.peak_flooded_kg) ?
                std::max(result.peak_flooded_kg, r.flooded_kg) : r.flooded_kg;
            const double reserve = (r.reserve_j - r.kinetic_j) * .001;
            result.remaining_reserve_min_kj = std::isfinite(result.remaining_reserve_min_kj) ?
                std::min(result.remaining_reserve_min_kj, reserve) : reserve;
        }
        if (i < start)
            continue;
        const double angle = r.roll_rad * to_degrees;
        energy += angle * angle;
        peak = std::max(peak, std::abs(angle));
        if (c.flags & SLX_SB_EST_VALID) {
            const auto error = std::remainder(c.estimated_roll_rad - r.roll_rad, 2 * std::numbers::pi) * to_degrees;
            estimate_energy += error * error;
            ++result.estimate_count;
        }
        constexpr uint32_t both = SLX_SB_ACCEL_VALID | SLX_SB_GYRO_VALID;
        if ((row.sample.valid & both) != both || row.sample.sim_ns - row.sample.imu_ns > 3 * SLX_SB_STEP_NS)
            ++result.invalid_imu;
        if (c.flags & SLX_SB_ACCEL_REJECTED)
            ++result.accel_rejected;
        if (r.limited_fins || (c.flags & SLX_SB_AT_LIMIT))
            saturated += dt;
        if (i > start) {
            movement += std::abs(r.port_actual_rad - previous.port_actual_rad) +
                        std::abs(r.starboard_actual_rad - previous.starboard_actual_rad);
        }
        previous = r;
        if (std::isfinite(row.rtt_ms))
            rtt.add(row.rtt_ms);
        if (std::isfinite(row.release_jitter_ms))
            jitter.add(std::abs(row.release_jitter_ms));
        if (row.late)
            ++result.deadline_missed;
        ++result.count;
    }
    if (result.count >= 50) {
        result.rms_deg = std::sqrt(energy / result.count);
        result.peak_deg = peak;
        result.saturation_s = saturated;
        result.activity = movement / (2 * s.scenario.model.actuator_rate_rad_s * dt * (result.count - 1));
    }
    if (result.estimate_count >= 50) {
        result.estimate_rms_deg = std::sqrt(estimate_energy / result.estimate_count);
        result.estimate_lag_s = lag(s, start);
    }
    if (s.scenario.model.wave_height_m == 0 && s.scenario.model.packet_height_m == 0 &&
        s.scenario.model.load_offset_m == 0 && std::abs(s.scenario.model.initial_roll_rad) > .002) {
        const double criterion = std::max(.1 / to_degrees, .05 * std::abs(s.scenario.model.initial_roll_rad));
        size_t last_outside = 0;
        for (size_t i = 0; i < s.rows.size(); ++i) {
            if (std::abs(s.rows[i].reference.roll_rad) > criterion)
                last_outside = i;
        }
        if (s.rows.size() > last_outside + 101 && s.manifest.value("status", "") == "complete")
            result.settling_s = (last_outside + 1) * dt;
    }
    result.rtt_p95_ms = rtt.p95();
    if (rtt.count)
        result.rtt_max_ms = rtt.maximum;
    result.release_p95_ms = jitter.p95();
    return result;
}

json metric_json(const Metrics &m)
{
    return {{"rows", m.count}, {"estimate_rows", m.estimate_count}, {"invalid_imu", m.invalid_imu},
            {"accel_rejected", m.accel_rejected}, {"deadline_missed", m.deadline_missed},
            {"roll_rms_deg", m.rms_deg}, {"roll_peak_deg", m.peak_deg},
            {"estimate_rms_deg", m.estimate_rms_deg}, {"estimate_lag_s", m.estimate_lag_s},
            {"fin_activity_normalized", m.activity}, {"saturation_s", m.saturation_s},
            {"settling_s", m.settling_s}, {"rtt_p95_ms", m.rtt_p95_ms}, {"rtt_max_ms", m.rtt_max_ms},
            {"release_jitter_p95_ms", m.release_p95_ms},
            {"first_opening_immersion_s", m.first_downflood_s},
            {"first_righting_loss_s", m.first_righting_loss_s}, {"first_capsize_s", m.first_capsize_s},
            {"min_effective_gm_m", m.min_gm_m}, {"peak_flooded_kg", m.peak_flooded_kg},
            {"min_reserve_minus_kinetic_kj", m.remaining_reserve_min_kj}};
}

double improvement(double baseline, double controlled)
{
    return baseline > .01 && std::isfinite(controlled) ? 100 * (1 - controlled / baseline) : missing;
}

void write_analysis(const Session &s, const fs::path &output, std::string_view note)
{
    if (note.size() > 2048)
        throw std::invalid_argument("Analysis annotation exceeds 2048 UTF-8 bytes");
    const auto path = output.empty() ? s.directory / "analysis-v1" : output;
    if (!fs::create_directory(path))
        throw std::runtime_error("Analysis already exists");
    const auto stats = measure(s);
    save_json(path / "metrics.json", {{"copyright", copyright}, {"criteria_version", 2},
              {"session", s.manifest.value("session", uint64_t{0})}, {"partial", s.partial},
              {"warmup_s", s.scenario.warmup_s}, {"annotation", note}, {"metrics", metric_json(stats)}});
    std::ofstream out(path / "report.html", std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    html_start(out, "Rapporto sessione");
    out << "<p>Dati sintetici · " << escaped(timings[static_cast<size_t>(s.scenario.timing)])
        << " · " << escaped(mode_name(s.scenario.mode)) << " · "
        << escaped(s.manifest.value("status", "recording")) << "</p>"
        << "<p>Seed " << s.scenario.model.seed << " · Velocità " << fmt(s.scenario.model.speed_mps)
        << " m/s · Onda " << fmt(s.scenario.model.wave_height_m) << " m · Periodo "
        << fmt(s.scenario.model.wave_period_s) << " s · Transitorio escluso "
        << fmt(s.scenario.warmup_s) << " s</p>";
    const auto reason = s.manifest.value("reason", "");
    if (!reason.empty())
        out << "<p>" << escaped(reason) << "</p>";
    if (!note.empty())
        out << "<p>" << escaped(note) << "</p>";
    if (s.curve.count) {
        out << "<section><h2>Stabilità · " << escaped(s.scenario.profile) << "</h2>"
            << "<p>Prima immersione " << fmt(stats.first_downflood_s)
            << " s · Perdita di raddrizzamento " << fmt(stats.first_righting_loss_s)
            << " s · Ribaltamento " << fmt(stats.first_capsize_s) << " s</p>"
            << "<p>GM minimo " << fmt(stats.min_gm_m)
            << " m · Acqua imbarcata massima " << fmt(stats.peak_flooded_kg, 0)
            << " kg · Minimo riserva meno energia " << fmt(stats.remaining_reserve_min_kj)
            << " kJ</p>";
        double y_limit = .01;
        for (uint32_t i = 0; i < s.curve.count; ++i)
            y_limit = std::max(y_limit, std::abs(s.curve.points[i].arm_m));
        out << "<svg viewBox=\"0 0 860 202\" role=\"img\" aria-label=\"Curva GZ iniziale\">"
               "<path d=\"M64 12V171H840 M64 92H840 M452 12V171\" stroke=\"#c8d1e3\" fill=\"none\"/>"
            << "<text x=\"2\" y=\"22\">" << fmt(y_limit, 2) << " m</text>"
            << "<text x=\"64\" y=\"194\">−180°</text><text x=\"800\" y=\"194\">+180°</text>"
            << "<path fill=\"none\" stroke=\"#5354e8\" stroke-width=\"2\" d=\"";
        for (uint32_t i = 0; i < s.curve.count; ++i) {
            const auto &point = s.curve.points[i];
            out << (i ? 'L' : 'M') << fmt(452 + point.heel_rad * 388 / std::numbers::pi, 1) << ' '
                << fmt(92 - 77 * point.arm_m / y_limit, 1) << ' ';
        }
        out << "\"/></svg></section>";
    }
    chart(out, s);
    out << "<section><h2>Metriche</h2><table><tbody>";
    const auto metrics = metric_json(stats);
    for (const auto &[name, value] : metrics.items()) {
        out << "<tr><th>" << escaped(name) << "</th><td>"
            << (value.is_null() ? "N/D" : value.is_number_float() ? fmt(value.get<double>(), 3) : value.dump())
            << "</td></tr>";
    }
    out << "</tbody></table></section><section><h2>Eventi</h2><table><thead>"
           "<tr><th>s</th><th>Origine</th><th>Evento</th><th>Codice</th></tr></thead><tbody>";
    for (const auto &e : s.events)
        out << "<tr><td>" << fmt(e.sim_ns * 1e-9) << "</td><td>" << escaped(e.origin)
            << "</td><td>" << escaped(e.name) << "</td><td>" << e.code << "</td></tr>";
    out << "</tbody></table><p>Eventi riportati " << s.events.size() << " su " << s.event_count
        << ". La serie completa è nel CSV della sessione.</p></section><p>Risultati del profilo a un grado di libertà. Attività pinne "
           "espressa come frazione adimensionale della corsa per unità di tempo.</p></main></html>\n";
    out.close();
}

void comparison_report(const fs::path &directory, const std::vector<Comparison> &comparisons)
{
    json cases = json::array();
    std::ofstream out(directory / "comparison.html", std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    html_start(out, "Confronto stabilizzazione");
    out << "<p>Dati sintetici · Stessa realizzazione del mare e stesse condizioni iniziali per ogni caso.</p>"
           "<section class=\"scroll\"><h2>Rollio RMS</h2><table><thead><tr><th>Caso</th><th>Set</th>"
           "<th>Spento °</th><th>PID °</th><th>SLX °</th><th>PID Δ %</th><th>SLX Δ %</th></tr></thead><tbody>";
    double worst = -1, aggregate = 0;
    size_t evaluated = 0;
    std::string worst_label;
    for (const auto &c : comparisons) {
        const auto a = measure(c.off), b = measure(c.pid), d = c.slx ? measure(*c.slx) : Metrics{};
        const bool complete = !c.off.partial && !c.pid.partial && c.off.rows.size() == c.pid.rows.size() &&
            c.off.manifest.value("status", "") == "complete" && c.pid.manifest.value("status", "") == "complete";
        const bool advanced_complete = complete && c.slx && c.slx->rows.size() == c.off.rows.size() &&
            c.slx->manifest.value("status", "") == "complete";
        const double gain = complete && !std::isfinite(a.first_capsize_s) && !std::isfinite(b.first_capsize_s) ?
            improvement(a.rms_deg, b.rms_deg) : missing;
        const double advanced = advanced_complete && !std::isfinite(a.first_capsize_s) &&
                                                 !std::isfinite(d.first_capsize_s) ?
            improvement(a.rms_deg, d.rms_deg) : missing;
        if (c.set == "evaluation" && std::isfinite(gain)) {
            aggregate += gain;
            ++evaluated;
            if (b.rms_deg > worst) {
                worst = b.rms_deg;
                worst_label = c.label;
            }
        }
        cases.push_back({{"case", c.label}, {"set", c.set}, {"off", metric_json(a)}, {"pid", metric_json(b)},
                         {"slx", c.slx ? metric_json(d) : json(nullptr)},
                         {"pid_improvement_pct", gain}, {"slx_improvement_pct", advanced}});
        out << "<tr><th>" << escaped(c.label) << "</th><td>" << escaped(c.set) << "</td><td>"
            << fmt(a.rms_deg) << "</td><td>" << fmt(b.rms_deg) << "</td><td>" << fmt(d.rms_deg)
            << "</td><td>" << fmt(gain) << "</td><td>" << fmt(advanced) << "</td></tr>";
    }
    out << "</tbody></table></section>";
    out << "<section class=\"scroll\"><h2>Esiti critici</h2><table><thead>"
           "<tr><th>Caso</th><th>Spento [s]</th><th>PID [s]</th><th>SLX [s]</th></tr></thead><tbody>";
    for (const auto &item : cases) {
        auto found = [](const json &metrics) {
            return metrics.is_null() || metrics.at("first_capsize_s").is_null() ?
                std::string("—") : fmt(metrics.at("first_capsize_s").get<double>());
        };
        out << "<tr><th>" << escaped(item.at("case").get<std::string>()) << "</th><td>"
            << found(item.at("off")) << "</td><td>" << found(item.at("pid"))
            << "</td><td>" << found(item.at("slx")) << "</td></tr>";
    }
    out << "</tbody></table><p>Tempo della prima inversione sostenuta registrata.</p></section>";
    if (evaluated)
        out << "<p>Valutazione · " << evaluated << " casi comparabili · Media Δ PID "
            << fmt(aggregate / evaluated) << " % · RMS PID più alto " << fmt(worst) << " ° ("
            << escaped(worst_label) << ")</p>";
    if (comparisons.size() == 1)
        chart(out, comparisons[0].off, &comparisons[0].pid);
    out << "<p>Δ = 100 × (1 − RMS con controllo / RMS spento). N/D per bracci incompleti, "
           "ribaltamento, finestre insufficienti o riferimento ≤ 0,01° RMS.</p></main></html>\n";
    out.close();
    save_json(directory / "comparison.json", {{"copyright", copyright}, {"schema", 1}, {"cases", cases},
              {"evaluation_count", evaluated}, {"pid_mean_improvement_pct", evaluated ? aggregate / evaluated : missing},
              {"pid_worst_case", worst_label}, {"pid_worst_rms_deg", evaluated ? worst : missing}});
}
} // namespace slx::sb
