// main.cpp — плагін R2D2 для гімбала SIYI ZR10 (C++, 0.3.8).
//
// Що робить:
//   * двічі на секунду питає ZR10 напряму (протокол SIYI, 37260, лише запит
//     кутів 0x0D) і малює на відео оператора плашку: літачок, синє віяло
//     напрямку камери, «ZR10 Pitch …» і «ZR10 Yaw …»;
//   * читає з польотного контролера MAVLink 285 (кути, які контролер отримує
//     від гімбала по UART) і порівнює з кутами гімбала — лише для журналу:
//     «FC link lost», коли контролер повторює старі кути.
// Плагін не керує ні гімбалом, ні апаратом.
//
// Запуск:  zr10-attitude [шлях/до/config.json]
// Без аргументу config.json шукається у робочій теці (так його кладе R2D2 для
// native-плагінів), поруч із програмою, потім /app/config.json.
// Зупинка: Ctrl+C або SIGTERM — плагін прибирає свої елементи з екрана.
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include "attitude.hpp"
#include "config.hpp"
#include "draw.hpp"
#include "log.hpp"
#include "mav.hpp"
#include "osd.hpp"
#include "siyi.hpp"

#ifdef _WIN32
#include <windows.h>  // SetConsoleOutputCP
#endif

namespace {

constexpr const char* VERSION = "0.3.8";
constexpr double LOOP_SLEEP_S = 0.05;
constexpr double FC_STALE_S = 5.0;      // 285 від контролера старше — не порівнюємо
constexpr double LOG_INTERVAL_S = 1.0;  // рядок журналу раз на секунду
constexpr double OSD_RESEND_S = 5.0;    // незмінні елементи OSD — повтор раз на 5 с
constexpr int SIYI_PORT = 37260;
constexpr double SIYI_RATE_HZ = 2.0;
// ArduPilot повертає yaw SIYI з протилежним знаком; на плашці yaw — як у 285
// (0 — вперед, + — вправо). Перевірено на камері 2026-11-11.
constexpr int SIYI_YAW_SIGN = -1;

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
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

// Останні кути від польотного контролера (MAVLink 285) — для журналу.
struct FcState {
    bool have = false;
    double heading = 0.0;  // 0 — вперед, + — вправо
    double pitch = 0.0;
    int flags = 0;
    int failure = 0;
    unsigned long count = 0;
    double last_rx = -1.0;
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
        if (file_exists("config.json")) config_path = "./config.json";
        else if (file_exists(near_exe)) config_path = near_exe;
        else if (file_exists("/app/config.json")) config_path = "/app/config.json";
    }
    zr10::Config cfg;
    const bool config_ok = !config_path.empty() && cfg.load_file(config_path);
    const zr10::PluginSettings s = zr10::settings_from_config(cfg);

    std::printf("INFO: zr10-attitude %s config=%s connect=%s gimbal_id=%lld cam=%s siyi=%s\n", VERSION,
                config_ok ? config_path.c_str()
                          : (config_path.empty() ? "(not found, defaults)" : "(unreadable, defaults)"),
                s.connect.c_str(), s.gimbal_id,
                s.cam == zr10::PluginSettings::NO_CAM ? "(all)" : std::to_string(s.cam).c_str(),
                s.siyi_ip.empty() ? "(off)" : (s.siyi_ip + ":" + std::to_string(SIYI_PORT)).c_str());
    std::fflush(stdout);

    // ---- MAVLink (лише читання 285 для журналу) ----
    zr10::Mav mav(s.connect, 255, 0, 1.0, 1, 1, /*debug=*/true);
    FcState fc;
    mav.on_gimbal([&](const zr10::GimbalAttitude& g) {
        if (static_cast<long long>(g.gimbal_device_id) != s.gimbal_id) return;
        zr10::Euler e;
        double heading = 0.0;
        if (!zr10::quat_to_euler_deg(g.q, e) || !zr10::camera_heading_deg(g.q, heading)) return;
        fc.heading = heading;
        fc.pitch = e.pitch;
        fc.flags = g.flags;
        fc.failure = static_cast<int>(g.failure_flags);
        fc.have = true;
        ++fc.count;
        fc.last_rx = now_s();
    });
    mav.start();  // якщо не вдалося — у журналі ERROR; плагін працює далі

    // ---- OSD ----
    zr10::Osd osd;  // 127.0.0.1:14777
    osd.set_only_changes(true, OSD_RESEND_S);
    if (s.cam != zr10::PluginSettings::NO_CAM) osd.set_cam(s.cam);
    {
        // Що R2D2 каже про себе і свої камери (для CAM).
        std::string pong = osd.ping(0.5);
        if (pong.size() > 1500) pong = pong.substr(0, 1500) + "...";
        std::printf("INFO: OSD pong: %s\n", pong.empty() ? "(no answer)" : pong.c_str());
        std::fflush(stdout);
    }
    zr10::clear_legacy_elements(osd);

    // ---- гімбал напряму ----
    zr10::SiyiReader siyi(s.siyi_ip, SIYI_PORT, SIYI_RATE_HZ);
    zr10::FcLinkCheck fc_check(SIYI_YAW_SIGN);

    bool drawn_once = false, last_have = false;
    unsigned long last_drawn = 0;
    double last_draw_t = -1e9, last_log = -1e9;

    while (!g_stop) {
        mav.poll();  // тут викликається on_gimbal для нових 285
        const double now = now_s();

        if (siyi.poll(now)) {
            if (siyi.state() == zr10::SiyiState::Ok)
                zr10::log_line("siyi", "INFO", "gimbal %s replies (yaw=%.1f pitch=%.1f)", siyi.ip().c_str(),
                               siyi.yaw(), siyi.pitch());
            else if (siyi.state() == zr10::SiyiState::Lost)
                zr10::log_line("siyi", "WARNING", "gimbal %s: no reply for %.0f s", siyi.ip().c_str(),
                               zr10::SiyiReader::LOST_S);
        }
        const bool siyi_ok = siyi.state() == zr10::SiyiState::Ok;
        const bool fc_fresh = fc.have && now - fc.last_rx <= FC_STALE_S;

        // Порівняння кутів контролера з кутами гімбала — лише журнал.
        if (fc_check.update(now, fc_fresh, fc.heading, fc.pitch, siyi_ok, siyi.yaw(), siyi.pitch(),
                            siyi.last_motion() < zr10::FcLinkCheck::STILL_DEG)) {
            if (fc_check.lost())
                zr10::log_line("siyi", "WARNING",
                               "FC link lost: flight controller angles differ from gimbal "
                               "(yaw diff %.1f, pitch diff %.1f)",
                               fc_check.yaw_diff(), fc_check.pitch_diff());
            else if (siyi_ok && fc_fresh)
                zr10::log_line("siyi", "INFO", "FC link ok: angles match again");
            else
                zr10::log_line("siyi", "INFO", "FC link check paused: no data");
        }
        if (now - last_log >= LOG_INTERVAL_S) {
            last_log = now;
            std::printf("INFO: gimbal state=%s yaw=%.1f pitch=%.1f | fc %s yaw=%.1f pitch=%.1f n=%lu | "
                        "diff yaw=%.1f pitch=%.1f moving=%.1f fc_link=%s req=%lu rep=%lu\n",
                        zr10::siyi_state_name(siyi.state()), SIYI_YAW_SIGN * siyi.yaw(), siyi.pitch(),
                        fc_fresh ? "ok" : (fc.have ? "stale" : "none"), fc.heading, fc.pitch, fc.count,
                        fc_check.yaw_diff(), fc_check.pitch_diff(), siyi.last_motion(),
                        fc_check.lost() ? "LOST" : "ok", siyi.requests(), siyi.replies());
            std::fflush(stdout);
        }

        // Плашка: кути з гімбала; не відповідає 3 с (або ще не відповів) — «--».
        const bool have = siyi.have_angles() && siyi_ok;
        const bool changed = !drawn_once || have != last_have || (have && siyi.replies() != last_drawn);
        const bool resend_due = now - last_draw_t >= OSD_RESEND_S;
        if (changed || resend_due) {
            zr10::ShownAngles a;
            a.pitch = siyi.pitch();
            a.yaw = std::round(SIYI_YAW_SIGN * siyi.yaw() * 10.0) / 10.0;
            zr10::draw_panel(osd, have ? &a : nullptr);
            drawn_once = true;
            last_have = have;
            last_drawn = siyi.replies();
            last_draw_t = now;
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(LOOP_SLEEP_S));
    }

    zr10::clear_plugin_elements(osd);
    osd.close();
    mav.stop();
    return 0;
}
