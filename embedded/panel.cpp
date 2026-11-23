// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "runtime.hpp"
#include "wire.hpp"

#include <gtk/gtk.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <numbers>

namespace slx::sb {
namespace {
using json = nlohmann::json;
constexpr double degrees = 180 / std::numbers::pi;
constexpr std::array modes{"Spento","Manuale","PID","SLX"};
constexpr std::array timing{"Software","Lockstep","Temporizzata"};
constexpr std::array alarm{"Nessuno", "Qualità IMU", "IMU obsoleta", "Posizione pinna",
                           "Perdita collegamento", "Riavvio controllore"};

constexpr char css[] =
    "window { background: #f3f5f9; color: #192541; font: 14px Sans; }"
    "#title { background: #111c31; color: #fff; padding: 7px 12px; font: bold 17px Sans; }"
    "#status { color: #56647a; font: bold 12px Sans; }"
    "#mode { color: #192541; font: bold 19px Sans; }"
    "#control { color: #192541; font: bold 15px Sans; }"
    "#good { color: #087762; font-weight: bold; }"
    "#alert { color: #a63e52; font-weight: bold; }"
    "button { color: #192541; border: 1px solid #c9d3e2; border-radius: 5px; font-weight: bold;"
    " min-height: 1px; padding: 3px 8px; }"
    "button.selected { color: #fff; background: #494bd1; border-color: #494bd1; }"
    "button:disabled { color: #657288; background: #e7ecf4; }";

constexpr std::array page_names{"overview","balance","navigation","alarm"};
constexpr std::array page_titles{"Assetto","Bilancio","Manovra","Allarmi"};

template <size_t N> const char *label_at(const std::array<const char *,N> &items, uint32_t i)
{
    return i < N ? items[i] : "Sconosciuto";
}

GtkWidget *label(const char *text, const char *id = nullptr)
{
    auto *w = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(w),0);
    if (id) gtk_widget_set_name(w,id);
    return w;
}
void text(GtkWidget *widget, const char *value)
{
    gtk_label_set_text(GTK_LABEL(widget),value);
}

void selected(GtkWidget *widget, bool is_selected)
{
    auto *style = gtk_widget_get_style_context(widget);
    if (is_selected)
        gtk_style_context_add_class(style,"selected");
    else
        gtk_style_context_remove_class(style,"selected");
}

struct Panel {
    enum Action {
        take = 1, mark, set_off, set_manual, set_pid, set_slx, manual_down, manual_up,
        speed_down, speed_up, heading_down, heading_up
    };
    fs::path directory, screenshot, logo;
    GtkWidget *window{}, *stack{}, *drawing{}, *connection{}, *operating{}, *attitude{}, *fins{}, *quality{};
    GtkWidget *alarm_label{}, *alarm_history{}, *diagnostics{}, *ack{};
    GtkWidget *authority{}, *video{}, *balance{}, *manual_value{}, *course{}, *speed_target{}, *heading_target{};
    std::array<GtkWidget *,4> tabs{}, mode_buttons{}, navigation_buttons{};
    std::array<GtkWidget *,2> manual_buttons{};
    bool values{}, estimate{}, operator_enabled{}, panel_local{}, has_slx{};
    double roll{}, manual_angle{}, speed_mps{}, heading_rad{};
    wire::Navigation navigation;
    uint64_t session{}, revision{}, pending_revision{}, pending_since{};
    uint32_t requested_mode{}, effective_mode{};
    bool failed_order{};
    unsigned screen_result{};
    guint timer{}, capture_timer{};

    GtkWidget *button(GtkWidget *row, const char *caption, Action id, int height = 35)
    {
        auto *w = gtk_button_new_with_label(caption);
        gtk_widget_set_size_request(w,-1,height);
        g_object_set_data(G_OBJECT(w),"action",GINT_TO_POINTER(id));
        g_signal_connect(w,"clicked",G_CALLBACK(+[](GtkButton *source,gpointer p) {
            const auto command = static_cast<Action>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(source),"action")));
            static_cast<Panel *>(p)->send(command);
        }),this);
        gtk_box_pack_start(GTK_BOX(row),w,TRUE,TRUE,0);
        return w;
    }

    static GtkWidget *row(GtkWidget *column)
    {
        auto *w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
        gtk_box_set_homogeneous(GTK_BOX(w),TRUE);
        gtk_box_pack_start(GTK_BOX(column),w,FALSE,FALSE,0);
        return w;
    }

    void build_controls()
    {
        auto *controls = gtk_box_new(GTK_ORIENTATION_VERTICAL,3);
        gtk_stack_add_named(GTK_STACK(stack),controls,"balance");
        balance = label("In attesa della prova","control");
        gtk_box_pack_start(GTK_BOX(controls),balance,FALSE,FALSE,0);
        auto *modes_row = row(controls);
        for (size_t i = 0; i < modes.size(); ++i)
            mode_buttons[i] = button(modes_row,modes[i],static_cast<Action>(set_off + i));
        auto *manual = row(controls);
        manual_buttons[0] = button(manual,"Manuale −2°",manual_down);
        manual_value = label("BB 0° · TB 0°");
        gtk_label_set_xalign(GTK_LABEL(manual_value),.5f);
        gtk_box_pack_start(GTK_BOX(manual),manual_value,TRUE,TRUE,0);
        manual_buttons[1] = button(manual,"Manuale +2°",manual_up);
        auto *source = row(controls);
        authority = button(source,"Prendi comando",take);
        video = button(source,"Marcatore video",mark);

        auto *manoeuvre = gtk_box_new(GTK_ORIENTATION_VERTICAL,5);
        gtk_stack_add_named(GTK_STACK(stack),manoeuvre,"navigation");
        gtk_box_pack_start(GTK_BOX(manoeuvre), label("Manovra virtuale · Rotta relativa all’onda","control"),FALSE,FALSE,0);
        course = label("Velocità N/D · Rotta N/D","status");
        gtk_box_pack_start(GTK_BOX(manoeuvre),course,FALSE,FALSE,0);
        auto *speed = row(manoeuvre);
        navigation_buttons[0] = button(speed,"−0,5 m/s",speed_down,39);
        speed_target = label("Velocità obiettivo");
        gtk_label_set_xalign(GTK_LABEL(speed_target),.5f);
        gtk_box_pack_start(GTK_BOX(speed),speed_target,TRUE,TRUE,0);
        navigation_buttons[1] = button(speed,"+0,5 m/s",speed_up,39);
        auto *heading = row(manoeuvre);
        navigation_buttons[2] = button(heading,"−15°",heading_down,39);
        heading_target = label("Rotta obiettivo");
        gtk_label_set_xalign(GTK_LABEL(heading_target),.5f);
        gtk_box_pack_start(GTK_BOX(heading),heading_target,TRUE,TRUE,0);
        navigation_buttons[3] = button(heading,"+15°",heading_up,39);
        refresh_controls();
    }

    void send(Action action)
    {
        if (!operator_enabled || pending_revision || (action != take && action != mark && !panel_local))
            return;
        wire::OperatorOrder order{revision + 1, panel_local ? wire::operator_local : 0u, requested_mode,
                                  manual_angle, navigation.target_speed_mps, navigation.target_heading_rad};
        switch (action) {
        case take: order.flags ^= wire::operator_local; break;
        case mark: order.flags |= wire::operator_mark; break;
        case set_off: case set_manual: case set_pid: case set_slx:
            order.mode = static_cast<uint32_t>(action - set_off);
            break;
        case manual_down: case manual_up:
            order.manual_rad = std::clamp(order.manual_rad + (action == manual_down ? -2 : 2) / degrees,
                                         -SLX_SB_FIN_LIMIT_RAD, SLX_SB_FIN_LIMIT_RAD);
            break;
        case speed_down: case speed_up:
            order.flags |= wire::operator_navigation;
            order.speed_mps = std::clamp(order.speed_mps + (action == speed_down ? -.5 : .5),0.,9.);
            break;
        case heading_down: case heading_up:
            order.flags |= wire::operator_navigation;
            order.relative_heading_rad = std::remainder(order.relative_heading_rad +
                (action == heading_down ? -15 : 15) / degrees,2 * std::numbers::pi);
            break;
        }
        const auto temporary = directory / "operator.request.partial";
        std::ofstream marker_file(temporary,std::ios::binary);
        marker_file << json{{"copyright","Copyright (c) 2026 Marco Pezzullo (Silicon LogiX)"},
            {"version",1},{"session",session},{"revision",order.revision},{"flags",order.flags},
            {"mode",order.mode},{"manual_rad",order.manual_rad},{"speed_mps",order.speed_mps},
            {"heading_rad",order.relative_heading_rad}}.dump() << '\n';
        marker_file.close();
        std::error_code error;
        if (marker_file)
            fs::rename(temporary,directory / "operator.request",error);
        failed_order = !marker_file || static_cast<bool>(error);
        if (!failed_order) {
            pending_revision = order.revision;
            pending_since = wire::monotonic_ns();
        }
        refresh_controls();
    }

    void refresh_controls()
    {
        const bool shared = operator_enabled && pending_revision == 0;
        const bool local = shared && panel_local;
        gtk_button_set_label(GTK_BUTTON(authority),panel_local ? "Restituisci al PC" : "Prendi comando");
        gtk_widget_set_sensitive(authority,shared);
        gtk_widget_set_sensitive(video,shared);
        for (size_t i = 0; i < mode_buttons.size(); ++i) {
            gtk_widget_set_sensitive(mode_buttons[i],local && (i != SLX_SB_SLX || has_slx));
            selected(mode_buttons[i],values && i == effective_mode);
        }
        for (auto *w : manual_buttons)
            gtk_widget_set_sensitive(w,local && requested_mode == SLX_SB_MANUAL);
        for (auto *w : navigation_buttons)
            gtk_widget_set_sensitive(w,local);
        std::array<char,160> buf{};
        if (values) {
            std::snprintf(buf.data(),buf.size(),"%s · %s%s",panel_local ? "Pannello" : "PC",
                label_at(modes,effective_mode),pending_revision ? " · Invio…" :
                failed_order ? " · Comando non confermato" : "");
            text(balance,buf.data());
        } else text(balance,"In attesa della prova");
        if (values) {
            std::snprintf(buf.data(),buf.size(),"BB %+.0f° · TB %+.0f°",manual_angle*degrees,-manual_angle*degrees);
            text(manual_value,buf.data());
        } else text(manual_value,"Manuale N/D");
        if (operator_enabled) {
            std::snprintf(buf.data(),buf.size(),"Attuali   %.2f m/s   ·   %+.0f°%s",speed_mps,heading_rad*degrees,
                          panel_local ? "" : "   ·   Prendi comando in Bilancio");
            text(course,buf.data());
            std::snprintf(buf.data(),buf.size(),"Obiettivo %.1f m/s",navigation.target_speed_mps);
            text(speed_target,buf.data());
            std::snprintf(buf.data(),buf.size(),"Obiettivo %+.0f°",navigation.target_heading_rad*degrees);
            text(heading_target,buf.data());
        } else {
            text(course,values ? "Estensione manovra non disponibile" : "Velocità N/D · Rotta N/D");
            text(speed_target,"Velocità obiettivo");
            text(heading_target,"Rotta obiettivo");
        }
    }

    void build(bool fullscreen)
    {
        auto *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, css, -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window),"SLX SeaBalance · Pannello");
        gtk_window_set_default_size(GTK_WINDOW(window),480,272);
        gtk_window_set_resizable(GTK_WINDOW(window),FALSE);
        if (fullscreen) {
            gtk_window_set_decorated(GTK_WINDOW(window),FALSE);
            gtk_window_fullscreen(GTK_WINDOW(window));
        }
        g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),nullptr);

        auto *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
        gtk_container_add(GTK_CONTAINER(window),outer);
        auto *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
        gtk_widget_set_name(header,"title");
        gtk_box_pack_start(GTK_BOX(outer),header,FALSE,FALSE,0);
        auto *title = label("SLX SeaBalance");
        gtk_label_set_markup(GTK_LABEL(title),"<b>SLX SeaBalance</b>");
        gtk_box_pack_start(GTK_BOX(header),title,TRUE,TRUE,0);
        if (!logo.empty()) {
            auto *image = gdk_pixbuf_new_from_file_at_scale(logo.string().c_str(),45,24,TRUE,nullptr);
            if (image) {
                gtk_box_pack_end(GTK_BOX(header),gtk_image_new_from_pixbuf(image),FALSE,FALSE,0);
                g_object_unref(image);
            }
        }
        auto *content = gtk_box_new(GTK_ORIENTATION_VERTICAL,5);
        gtk_container_set_border_width(GTK_CONTAINER(content),9);
        gtk_box_pack_start(GTK_BOX(outer),content,TRUE,TRUE,0);
        connection = label("Servizio in attesa","status");
        gtk_box_pack_start(GTK_BOX(content),connection,FALSE,FALSE,0);
        stack = gtk_stack_new();
        gtk_box_pack_start(GTK_BOX(content),stack,TRUE,TRUE,0);

        auto *overview = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,9);
        gtk_stack_add_named(GTK_STACK(stack),overview,"overview");
        drawing = gtk_drawing_area_new();
        gtk_widget_set_size_request(drawing,208,142);
        gtk_box_pack_start(GTK_BOX(overview),drawing,FALSE,FALSE,0);
        g_signal_connect(drawing,"draw",G_CALLBACK(+[](GtkWidget *w,cairo_t *cr,gpointer p) -> gboolean {
            return static_cast<Panel *>(p)->draw(w,cr);
        }),this);
        auto *readings = gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
        gtk_box_pack_start(GTK_BOX(overview),readings,TRUE,TRUE,0);
        operating = label("In attesa","mode");
        attitude = label("Rollio N/D · Velocità N/D");
        fins = label("Pinna   Posizione / Comando");
        quality = label("Stima non disponibile","status");
        for (auto *widget : {operating,attitude,fins,quality})
            gtk_box_pack_start(GTK_BOX(readings),widget,FALSE,FALSE,0);

        build_controls();
        auto *detail = gtk_box_new(GTK_ORIENTATION_VERTICAL,5);
        gtk_stack_add_named(GTK_STACK(stack),detail,"alarm");
        alarm_label = label("Allarmi","mode");
        alarm_history = label("Ultimo allarme N/D");
        diagnostics = label("Sessione N/D");
        ack = gtk_button_new_with_label("Riconosci allarme");
        gtk_widget_set_size_request(ack, -1, 35);
        for (auto *widget : {alarm_label,alarm_history,diagnostics,ack})
            gtk_box_pack_start(GTK_BOX(detail),widget,FALSE,FALSE,0);
        gtk_stack_set_visible_child_name(GTK_STACK(stack),"overview");

        auto *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
        gtk_box_set_homogeneous(GTK_BOX(actions),TRUE);
        gtk_box_pack_end(GTK_BOX(content),actions,FALSE,FALSE,0);
        for (size_t i = 0; i < tabs.size(); ++i) {
            auto *tab = gtk_button_new_with_label(page_titles[i]);
            tabs[i] = tab;
            gtk_widget_set_size_request(tab,-1,42);
            gtk_box_pack_start(GTK_BOX(actions),tab,TRUE,TRUE,0);
            selected(tab,i == 0);
            g_object_set_data(G_OBJECT(tab),"page",GUINT_TO_POINTER(i));
            g_signal_connect(tab,"clicked",G_CALLBACK(+[](GtkButton *source,gpointer p) {
                auto &panel = *static_cast<Panel *>(p);
                const auto i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(source),"page"));
                gtk_stack_set_visible_child_name(GTK_STACK(panel.stack),page_names[i]);
                for (size_t j = 0; j < panel.tabs.size(); ++j)
                    selected(panel.tabs[j],j == i);
            }),this);
        }
        gtk_widget_set_sensitive(ack,FALSE);
        g_signal_connect(ack,"clicked",G_CALLBACK(+[](GtkButton *,gpointer p) {
            auto &panel = *static_cast<Panel *>(p);
            const auto temporary = panel.directory / "ack.request.partial";
            std::ofstream marker(temporary,std::ios::binary);
            marker << "# Silicon LogiX / SLX SeaBalance - operator acknowledgement\n" << panel.session << '\n';
            marker.close();
            std::error_code error;
            if (marker)
                fs::rename(temporary, panel.directory / "ack.request", error);
            if (marker && !error)
                gtk_widget_set_sensitive(panel.ack,FALSE);
            else
                text(panel.quality,"Riconoscimento non inviato");
        }),this);
        gtk_widget_show_all(window);
        timer = g_timeout_add(100,+[](gpointer p) -> gboolean {
            static_cast<Panel *>(p)->update();
            return G_SOURCE_CONTINUE;
        },this);
        if (!screenshot.empty()) {
            capture_timer = g_timeout_add(350,+[](gpointer p) -> gboolean {
                auto &panel = *static_cast<Panel *>(p);
                auto *pix = gdk_pixbuf_get_from_window(gtk_widget_get_window(panel.window),0,0,
                    gtk_widget_get_allocated_width(panel.window),gtk_widget_get_allocated_height(panel.window));
                if (!pix || !gdk_pixbuf_save(pix,panel.screenshot.string().c_str(),"png",nullptr,nullptr))
                    panel.screen_result = 1;
                if (pix) g_object_unref(pix);
                panel.capture_timer = 0;
                gtk_main_quit();
                return G_SOURCE_REMOVE;
            },this);
        }
    }

    gboolean draw(GtkWidget *w,cairo_t *cr) const
    {
        const auto width = gtk_widget_get_allocated_width(w);
        const auto height = gtk_widget_get_allocated_height(w);
        const double x = width*.5, y = height*.5, radius = std::min(x-2,y-2);
        cairo_arc(cr,x,y,radius,0,2*std::numbers::pi);
        cairo_clip(cr);
        cairo_set_source_rgb(cr,.10,.16,.25);
        cairo_paint(cr);
        if (estimate) {
            cairo_save(cr);
            cairo_translate(cr,x,y);
            cairo_rotate(cr,-roll);
            cairo_set_source_rgb(cr,.24,.50,.69);
            cairo_rectangle(cr,-2*radius,-2*radius,4*radius,2*radius);
            cairo_fill(cr);
            cairo_set_source_rgb(cr,.52,.34,.20);
            cairo_rectangle(cr,-2*radius,0,4*radius,2*radius);
            cairo_fill(cr);
            cairo_set_source_rgba(cr,1,1,1,.80);
            cairo_set_line_width(cr,1);
            for (int i : {-48,-24,0,24,48}) {
                cairo_move_to(cr,i == 0 ? -radius : -18,i);
                cairo_line_to(cr,i == 0 ? radius : 18,i);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
        } else {
            cairo_set_source_rgb(cr,.86,.90,.96);
            cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr,18);
            cairo_move_to(cr,x-18,y+6);
            cairo_show_text(cr,"N/D");
        }
        cairo_reset_clip(cr);
        cairo_new_path(cr);
        cairo_set_source_rgb(cr,.22,.33,.49);
        cairo_set_line_width(cr,2);
        cairo_arc(cr,x,y,radius-1,0,2*std::numbers::pi);
        cairo_stroke(cr);
        if (estimate) {
            cairo_set_source_rgb(cr,1,.86,.42);
            cairo_set_line_width(cr,2.5);
            cairo_move_to(cr,x-53,y); cairo_line_to(cr,x-14,y);
            cairo_line_to(cr,x,y+8); cairo_line_to(cr,x+14,y); cairo_line_to(cr,x+53,y);
            cairo_stroke(cr);
        }
        return FALSE;
    }

    void update()
    {
        values = estimate = operator_enabled = false;
        try {
            const auto path = directory / "status.json";
            if (fs::file_size(path) > 4096)
                throw std::runtime_error("Panel snapshot too large");
            std::ifstream in(path,std::ios::binary);
            const auto j = json::parse(in);
            const auto schema = j.at("version").get<unsigned>();
            if ((schema != 1 && schema != 2) || j.at("source") != "synthetic")
                throw std::runtime_error("Panel schema");
            const auto now = wire::monotonic_ns();
            const auto update = j.at("updated_ns").get<uint64_t>();
            const bool fresh = update <= now && now-update < 350000000;
            const bool connected = j.at("connected").get<bool>();
            values = connected && fresh && j.at("sampled").get<bool>();
            const auto flags = j.at("flags").get<uint32_t>();
            estimate = values && (flags & SLX_SB_EST_VALID);
            roll = j.at("roll_rad").get<double>();
            const auto cause = j.at("cause").get<uint32_t>();
            const auto state = j.at("state").get<uint32_t>();
            const auto current_session = j.at("session").get<uint64_t>();
            if (current_session != session) {
                session = current_session;
                pending_revision = 0;
                failed_order = false;
            }
            requested_mode = j.at("requested_mode").get<uint32_t>();
            effective_mode = j.at("mode").get<uint32_t>();
            speed_mps = j.at("speed_mps").get<double>();
            if (schema == 2) {
                operator_enabled = values && j.at("operator_enabled").get<bool>();
                has_slx = j.at("has_slx").get<bool>();
                revision = j.at("operator_revision").get<uint64_t>();
                const auto op_flags = j.at("operator_flags").get<uint32_t>();
                panel_local = operator_enabled && (op_flags & wire::operator_local);
                manual_angle = j.at("manual_rad").get<double>();
                heading_rad = j.at("heading_rad").get<double>();
                navigation = {heading_rad,j.at("target_speed_mps").get<double>(),j.at("target_heading_rad").get<double>()};
                if (pending_revision && (revision >= pending_revision || now - pending_since > 1500000000)) {
                    failed_order = revision < pending_revision;
                    pending_revision = 0;
                }
                gtk_button_set_label(GTK_BUTTON(video),
                    operator_enabled && (op_flags & wire::operator_mark) ? "Marcato · Nuovo" : "Marcatore video");
            }
            std::array<char,256> buffer{};
            std::snprintf(buffer.data(),buffer.size(),"%s · Sintetica · %s",
                label_at(timing,j.at("timing").get<uint32_t>()),
                values ? "Collegato" : connected && fresh ? "Attesa campioni" :
                connected ? "Dati obsoleti" : "Scollegato");
            text(connection,buffer.data());
            if (values) {
                std::snprintf(buffer.data(),buffer.size(),"%s · %.1f m/s",
                    label_at(modes,j.at("mode").get<uint32_t>()),j.at("speed_mps").get<double>());
                text(operating,buffer.data());
                if (estimate) {
                    std::snprintf(buffer.data(),buffer.size(),"%+.1f°   %+.1f°/s",roll*degrees,
                                  j.at("roll_rate_rad_s").get<double>()*degrees);
                    text(attitude,buffer.data());
                }
                if (j.at("sensor_valid").get<uint32_t>() & SLX_SB_FINS_VALID) {
                    std::snprintf(buffer.data(),buffer.size(),
                        "Pinna        Posizione / Comando\nBB            %+.1f° / %+.1f°\nTB            %+.1f° / %+.1f°",
                        j.at("port_rad").get<double>()*degrees,j.at("port_cmd_rad").get<double>()*degrees,
                        j.at("starboard_rad").get<double>()*degrees,j.at("starboard_cmd_rad").get<double>()*degrees);
                    text(fins,buffer.data());
                } else text(fins,"Feedback pinne N/D");
                text(quality,cause ? label_at(alarm,cause) : flags & SLX_SB_SPEED_LIMIT ?
                    "Velocità insufficiente" : flags & SLX_SB_ACCEL_REJECTED ? "Correzione accel. esclusa" : "IMU valida");
            } else {
                text(operating,"In attesa");
                text(fins,"Posizioni e comandi N/D");
                text(quality,connected ? "Campione non disponibile" : "In attesa della sessione");
            }
            gtk_widget_set_name(quality,cause || !values ? "alert" : "good");
            if (!estimate)
                text(attitude,"Rollio N/D · Velocità N/D");
            text(alarm_label,label_at(alarm,cause));
            std::snprintf(buffer.data(),buffer.size(),"Ultimo allarme · %s",label_at(alarm,j.at("last_alarm").get<uint32_t>()));
            text(alarm_history,buffer.data());
            std::snprintf(buffer.data(),buffer.size(),"Campione %llu · Datagrammi scartati %u\n%s",
                static_cast<unsigned long long>(j.at("sequence").get<uint64_t>()),
                j.at("invalid_frames").get<unsigned>(),state == SLX_SB_AWAIT_ACK ?
                    "In attesa di riconoscimento" : cause ? "Controllo degradato" : "Nessun allarme attivo");
            text(diagnostics,buffer.data());
            gtk_widget_set_sensitive(ack,values && state == SLX_SB_AWAIT_ACK);
        } catch (const std::exception &) {
            values = estimate = operator_enabled = panel_local = false;
            text(connection,"Stato servizio non disponibile");
            text(operating,"In attesa");
            text(attitude,"Rollio N/D · Velocità N/D");
            text(fins,"Posizioni e comandi N/D");
            text(quality,"Attesa stato controllore");
            text(alarm_label,"Stato allarmi N/D");
            text(alarm_history,"Ultimo allarme N/D");
            text(diagnostics,"Sessione N/D");
            gtk_widget_set_sensitive(ack,FALSE);
        }
        refresh_controls();
        gtk_widget_queue_draw(drawing);
    }
};
} // namespace
} // namespace slx::sb

int main(int argc, char **argv)
{
    try {
        slx::sb::Panel panel;
        panel.directory = "/run/slx-seabalance";
        bool fullscreen = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--fullscreen") fullscreen = true;
            else if (arg == "--status" && i + 1 < argc) panel.directory = argv[++i];
            else if (arg == "--logo" && i + 1 < argc) panel.logo = argv[++i];
            else if (arg == "--screenshot" && i + 1 < argc) panel.screenshot = argv[++i];
            else if (arg == "--help") {
                std::cout << "slx-seabalance-panel [--status DIR] [--fullscreen] [--logo PNG] [--screenshot FILE]\n";
                return 0;
            } else throw std::invalid_argument("Unsupported panel argument");
        }
        if (!gtk_init_check(nullptr,nullptr))
            throw std::runtime_error("GTK display unavailable");
        panel.build(fullscreen);
        gtk_main();
        if (panel.timer) g_source_remove(panel.timer);
        if (panel.capture_timer) g_source_remove(panel.capture_timer);
        return static_cast<int>(panel.screen_result);
    } catch (const std::exception &e) {
        std::cerr << "SeaBalance panel: " << e.what() << '\n';
        return 1;
    }
}
