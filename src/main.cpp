// main.cpp — плагін R2D2 для гімбала SIYI ZR10 (C++ версія).
// Переклад головної частини plugin.py: отримує MAVLink-повідомлення 285
// GIMBAL_DEVICE_ATTITUDE_STATUS, перетворює кватерніон у кути, пише журнал і
// малює на OSD pitch/yaw/статус та індикатор напрямку камери.
// Плагін нічого не шле на гімбал. REQUEST_RATE_HZ = 0 за замовчуванням.
//
// Запуск:  zr10-attitude [шлях/до/config.json]
// Без аргументу config.json шукається поруч із програмою, потім /app/config.json.
// Зупинка: Ctrl+C або SIGTERM — плагін прибирає свої елементи з екрана.
//
// Свідомі відмінності від Python-версії — див. PLAN.md (розділ «Свідомі
// відмінності»). PYTHON_COMPAT=true у config.json вимикає їх (для перевірки).
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include "attitude.hpp"
#include "config.hpp"
#include "draw.hpp"
#include "mav.hpp"
#include "osd.hpp"

#ifdef _WIN32
#include <windows.h>  // SetConsoleOutputCP
#endif

namespace {

constexpr const char* VERSION = "0.3.1";
constexpr const char* MESSAGE_TYPE = "GIMBAL_DEVICE_ATTITUDE_STATUS";
constexpr int MESSAGE_ID = 285;
constexpr double LOOP_SLEEP_S = 0.05;  // як у plugin.py
constexpr double NO_DATA_S = 5.0;      // як у plugin.py

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Число у вигляді, як його друкує Python repr (0.0, 2.0, 0.1, 1e-05).
std::string py_repr(double v) {
    std::string s;
    zr10::Osd::append_number(s, v);
    return s;
}

bool file_exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

std::string dir_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Останні дані гімбала (як змінні latest / sample_count / last_rx у plugin.py).
struct State {
    bool have_sample = false;
    zr10::GimbalSample sample;  // yaw тут — для журналу (без округлення)
    double heading = 0.0;       // напрямок для малювання
    float q[4] = {0, 0, 0, 0};
    const char* frame = "UNSPECIFIED";
    unsigned long count = 0;
    double last_rx = -1.0;
    const char* callback_error = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // ---- налаштування ----
    std::string config_path;
    if (argc > 1) {
        config_path = argv[1];
    } else {
        const std::string near_exe = dir_of(argv[0]) + "config.json";
        if (file_exists(near_exe)) config_path = near_exe;
        else if (file_exists("/app/config.json")) config_path = "/app/config.json";
    }
    zr10::Config cfg;
    const bool config_ok = !config_path.empty() && cfg.load_file(config_path);
    const zr10::PluginSettings s = zr10::settings_from_config(cfg);

    zr10::DrawSettings draw_settings;
    draw_settings.x = s.x;
    draw_settings.y = s.y;
    draw_settings.dy = s.dy;
    draw_settings.size = s.size;
    draw_settings.yaw_widget_x = s.yaw_widget_x;
    draw_settings.yaw_widget_y = s.yaw_widget_y;
    draw_settings.yaw_widget_scale = s.yaw_widget_scale;
    draw_settings.yaw_style = s.python_compat        ? zr10::YawStyle::Python
                              : s.yaw_style == "line"  ? zr10::YawStyle::Line
                              : s.yaw_style == "arrow" ? zr10::YawStyle::Arrow
                              : s.yaw_style == "arrow_base" ? zr10::YawStyle::ArrowBase
                              : s.yaw_style == "triangle" ? zr10::YawStyle::Triangle
                              : s.yaw_style == "fan"   ? zr10::YawStyle::Fan
                                                       : zr10::YawStyle::Sector;  // 0.3.1
    draw_settings.panel = s.panel;  // плашка, 2026-10-28
    draw_settings.panel_alpha = static_cast<int>(s.panel_alpha);
    draw_settings.panel_w = s.panel_w;
    draw_settings.plane_image = s.plane_image;
    draw_settings.panel_image = s.panel_image;
    draw_settings.panel_pos = s.panel_pos == "bottom_left" ? zr10::PanelPos::BottomLeft
                              : s.panel_pos == "top_right" ? zr10::PanelPos::TopRight
                              : s.panel_pos == "top_left"  ? zr10::PanelPos::TopLeft
                              : s.panel_pos == "xy"        ? zr10::PanelPos::XY
                                                           : zr10::PanelPos::BottomRight;

    std::printf("INFO: zr10-attitude-test starting connect=%s gimbal_id=%lld draw=%s "
                "request_rate_hz=%s\n",
                s.connect.c_str(), s.gimbal_id, s.draw ? "True" : "False",
                py_repr(s.request_rate_hz).c_str());
    std::printf("INFO: C++ version %s config=%s osd_only_changes=%s osd_resend_s=%s "
                "log_interval_s=%s python_compat=%s yaw_style=%s panel=%s panel_alpha=%lld "
                "panel_w=%s panel_pos=%s plane_image=%s panel_image=%s\n",
                VERSION,
                config_ok ? config_path.c_str()
                          : (config_path.empty() ? "(not found, defaults)"
                                                 : "(unreadable, defaults)"),
                s.osd_only_changes ? "True" : "False", py_repr(s.osd_resend_s).c_str(),
                py_repr(s.log_interval_s).c_str(), s.python_compat ? "True" : "False",
                s.python_compat ? "python" : s.yaw_style.c_str(), s.panel ? "True" : "False",
                s.panel_alpha, py_repr(s.panel_w).c_str(), s.panel_pos.c_str(),
                s.plane_image.empty() ? "(lines)" : s.plane_image.c_str(),
                s.panel_image.empty() ? "(rect)" : s.panel_image.c_str());
    std::fflush(stdout);

    // ---- MAVLink і OSD ----
    zr10::Mav mav(s.connect, 255, 0, 1.0, 1, 1, /*debug=*/true);  // debug=True, як у plugin.py
    State st;
    mav.on_gimbal([&](const zr10::GimbalAttitude& g) {
        // Як on_gimbal() у plugin.py.
        if (static_cast<long long>(g.gimbal_device_id) != s.gimbal_id) return;
        zr10::Euler e;
        if (!zr10::quat_to_euler_deg(g.q, e)) {
            st.callback_error = "ValueError";  // як type(exc).__name__ у Python
            return;
        }
        double heading = e.yaw;
        if (!s.python_compat) zr10::camera_heading_deg(g.q, heading);  // рішення 2026-10-22
        st.sample.id = g.gimbal_device_id;
        st.sample.flags = g.flags;
        st.sample.failure = static_cast<int>(g.failure_flags);
        st.sample.roll = e.roll;
        st.sample.pitch = e.pitch;
        st.sample.yaw = heading;
        st.heading = heading;
        st.frame = zr10::yaw_frame(g.flags);
        for (int i = 0; i < 4; ++i) st.q[i] = g.q[i];
        st.have_sample = true;
        ++st.count;
        st.last_rx = now_s();
    });
    mav.start();  // якщо не вдалося — журнал уже має ERROR; плагін не падає

    zr10::Osd* osd = nullptr;
    zr10::Osd osd_instance;  // 127.0.0.1:14777, як R2Draw()
    if (s.draw) {
        osd = &osd_instance;
        if (s.osd_only_changes) osd->set_only_changes(true, s.osd_resend_s);
        zr10::clear_unused_elements(*osd, draw_settings);
    }

    if (s.request_rate_hz > 0.0) {
        mav.set_message_interval(MESSAGE_ID, s.request_rate_hz);
        std::printf("INFO: requested message_id=%d rate_hz=%s\n", MESSAGE_ID,
                    py_repr(s.request_rate_hz).c_str());
    }

    unsigned long last_printed = 0, last_drawn = 0;
    bool drawn_once = false, last_stale = false, warned_no_data = false;
    double last_log = -1e9, last_draw_t = -1e9;
    const double started_at = now_s();

    std::printf("INFO: waiting for %s id=%d\n", MESSAGE_TYPE, MESSAGE_ID);
    std::fflush(stdout);

    while (!g_stop) {
        mav.poll();  // тут викликається колбек on_gimbal для нових 285

        const double now = now_s();
        const double age = st.last_rx >= 0.0 ? now - st.last_rx : now - started_at;
        const bool stale = age > NO_DATA_S;

        if (st.count != last_printed && st.have_sample) {
            warned_no_data = false;
            if (s.log_interval_s <= 0.0 || now - last_log >= s.log_interval_s) {
                std::printf("INFO: gimbal285 n=%lu id=%d flags=%d frame=%s failure=%d "
                            "roll_deg=%.6f pitch_deg=%.6f yaw_body_deg=%.6f\n",
                            st.count, st.sample.id, st.sample.flags, st.frame,
                            st.sample.failure, st.sample.roll, st.sample.pitch, st.sample.yaw);
                std::printf("DEBUG: gimbal285 q=(%s, %s, %s, %s)\n",
                            py_repr(st.q[0]).c_str(), py_repr(st.q[1]).c_str(),
                            py_repr(st.q[2]).c_str(), py_repr(st.q[3]).c_str());
                std::fflush(stdout);
                last_printed = st.count;
                last_log = now;
            }
        }

        if (st.callback_error != nullptr) {
            std::fprintf(stderr, "ERROR: gimbal285 callback failed: %s\n", st.callback_error);
            std::fflush(stderr);
            st.callback_error = nullptr;
        }

        if (stale && !warned_no_data) {
            const auto& stats = mav.stats();
            std::fprintf(stderr,
                         "WARNING: no selected gimbal285 for %.1fs rx_total=%lu rx_bad=%lu "
                         "rx_errors=%lu gimbal285=%lu\n",
                         age, stats.rx_total, stats.rx_bad, stats.rx_errors,
                         mav.count_of(MESSAGE_ID));
            std::fflush(stderr);
            warned_no_data = true;
        }

        if (osd != nullptr) {
            const bool changed = !drawn_once || st.count != last_drawn || stale != last_stale;
            // Повтор для надійності: навіть без нових даних раз на OSD_RESEND_S
            // віддаємо кадр модулю OSD (він сам надішле лише те, що пора повторити).
            const bool resend_due = s.osd_only_changes && now - last_draw_t >= s.osd_resend_s;
            if (changed || resend_due) {
                zr10::GimbalSample shown = st.sample;
                if (!s.python_compat) shown.yaw = std::round(st.heading * 10.0) / 10.0;
                zr10::draw_sample(*osd, st.have_sample ? &shown : nullptr, stale, draw_settings);
                drawn_once = true;
                last_drawn = st.count;
                last_stale = stale;
                last_draw_t = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(LOOP_SLEEP_S));
    }

    // ---- як блок finally у plugin.py ----
    if (osd != nullptr) {
        zr10::clear_plugin_elements(*osd, draw_settings.yaw_style);
        osd->close();
    }
    mav.stop();
    return 0;
}
