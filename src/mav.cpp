// mav.cpp — див. пояснення в mav.hpp.
#include "mav.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "log.hpp"
#include "net.hpp"

// Офіційна бібліотека MAVLink (third_party/mavlink), діалект ardupilotmega —
// той самий, що dialect="ardupilotmega" у r2mav.py.
#include "ardupilotmega/mavlink.h"

namespace zr10 {

namespace {

// Окремі канали бібліотеки для прийому і відправки, щоб налаштування версії
// відправки не залежали від того, що приходить.
constexpr mavlink_channel_t RX_CHAN = MAVLINK_COMM_1;
constexpr mavlink_channel_t TX_CHAN = MAVLINK_COMM_0;

bool parse_connect(const std::string& s, bool& listen, std::string& ip, int& port) {
    std::string rest;
    if (s.rfind("udpout:", 0) == 0) {
        listen = false;
        rest = s.substr(7);
    } else if (s.rfind("udpin:", 0) == 0) {
        listen = true;
        rest = s.substr(6);
    } else if (s.rfind("udp:", 0) == 0) {
        listen = true;
        rest = s.substr(4);
    } else {
        return false;
    }
    const auto colon = rest.rfind(':');
    if (colon == std::string::npos) return false;
    ip = rest.substr(0, colon);
    port = std::atoi(rest.c_str() + colon + 1);
    return !ip.empty() && port > 0 && port < 65536;
}

}  // namespace

Mav::Mav(std::string connect, std::uint8_t source_system, std::uint8_t source_component,
         double heartbeat_hz, std::uint8_t ap_system, std::uint8_t ap_component, bool debug)
    : connect_(std::move(connect)),
      source_system_(source_system),
      source_component_(source_component),
      heartbeat_interval_(heartbeat_hz > 0.0 ? 1.0 / heartbeat_hz : 0.0),
      ap_system_(ap_system),
      ap_component_(ap_component),
      debug_(debug) {}

Mav::~Mav() { stop(); }

double Mav::now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool Mav::start() {
    if (running_) return true;
    if (debug_) {
        log_line("r2mav", "INFO",
                 "connecting %s (dialect=ardupilotmega, source=%d/%d, autopilot=%d/%d)",
                 connect_.c_str(), source_system_, source_component_, ap_system_,
                 ap_component_);
    }
    std::string ip;
    int port = 0;
    if (!parse_connect(connect_, listen_mode_, ip, port)) {
        log_line("r2mav", "ERROR", "failed to open %s: unsupported connection string",
                 connect_.c_str());
        return false;
    }
    sock_ = net::udp_socket();
    if (sock_ < 0) {
        log_line("r2mav", "ERROR", "failed to open %s: cannot create socket", connect_.c_str());
        return false;
    }
    const sockaddr_in addr = net::make_addr(ip.c_str(), port);
    if (listen_mode_) {
        if (::bind(sock_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            log_line("r2mav", "ERROR", "failed to open %s: cannot bind", connect_.c_str());
            net::close_socket(sock_);
            sock_ = -1;
            return false;
        }
        have_target_ = false;  // відповідатимемо тому, хто напише першим
    } else {
        std::memcpy(target_, &addr, sizeof(addr));
        have_target_ = true;
    }
    net::set_nonblocking(sock_);

    // Як pymavlink без MAVLINK20: відправляємо MAVLink 1, доки перший пакет
    // від R2D2 не покаже, що там MAVLink 2.
    mavlink_get_channel_status(TX_CHAN)->flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
    out_mavlink2_ = false;
    decided_version_ = false;

    running_ = true;
    last_rx_ = now_s();
    stats_t_ = last_rx_;
    // Одразу один HEARTBEAT, щоб R2D2 дізнався нашу адресу (як у r2mav.py).
    if (heartbeat_interval_ > 0.0) send_heartbeat();
    return true;
}

void Mav::stop() {
    if (!running_) return;
    running_ = false;
    net::close_socket(sock_);
    sock_ = -1;
    if (debug_) log_line("r2mav", "INFO", "stopped (%s)", stats_line().c_str());
}

void Mav::send_message(const void* message) {
    if (sock_ < 0 || !have_target_) return;
    std::uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const std::uint16_t len =
        mavlink_msg_to_send_buffer(buf, static_cast<const mavlink_message_t*>(message));
    sockaddr_in to;
    std::memcpy(&to, target_, sizeof(to));
    net::send_to(sock_, buf, len, to);
}

void Mav::send_heartbeat() {
    if (sock_ < 0) return;
    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack_chan(source_system_, source_component_, TX_CHAN, &msg,
                                    MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, 0);
    send_message(&msg);
    last_hb_ = now_s();
    ++stats_.hb_sent;
}

void Mav::poll() {
    if (!running_) return;
    const double now = now_s();
    if (heartbeat_interval_ > 0.0 && now - last_hb_ >= heartbeat_interval_) {
        send_heartbeat();
    }

    std::uint8_t buf[2048];
    for (int i = 0; i < 1000; ++i) {  // обмеження: не застрягти, якщо пакетів дуже багато
        sockaddr_in from{};
        const int n = net::recv_from(sock_, buf, sizeof(buf), from);
        if (n == 0) break;
        if (n < 0) {
            ++stats_.rx_errors;
            break;
        }
        if (listen_mode_) {
            std::memcpy(target_, &from, sizeof(from));
            have_target_ = true;
        }
        if (!decided_version_) {
            decided_version_ = true;
            if (buf[0] == MAVLINK_STX) {  // 0xFD — MAVLink 2
                out_mavlink2_ = true;
                mavlink_get_channel_status(TX_CHAN)->flags &= ~MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
            }
        }
        for (int k = 0; k < n; ++k) handle_byte(buf[k]);
    }

    if (debug_) debug_tick(now_s());
}

void Mav::handle_byte(std::uint8_t byte) {
    mavlink_message_t msg;
    mavlink_status_t status;
    const std::uint8_t result = mavlink_frame_char(RX_CHAN, byte, &msg, &status);
    if (result == MAVLINK_FRAMING_INCOMPLETE) return;
    if (result != MAVLINK_FRAMING_OK) {  // зіпсований кадр або невідоме повідомлення
        ++stats_.rx_bad;
        if (debug_ && !bad_data_logged_) {
            bad_data_logged_ = true;
            log_line("r2mav", "DEBUG",
                     "BAD_DATA seen (corrupt frames or unknown dialect; counted in stats)");
        }
        return;
    }

    ++stats_.rx_total;
    last_rx_ = now_s();
    if (no_data_warned_) {
        no_data_warned_ = false;
        log_line("r2mav", "INFO", "MAVLink data resumed");
    }

    // Лише HEARTBEAT від самого автопілота; гімбали, компаньйони, наземні
    // станції теж шлють HEARTBEAT (як у r2mav.py).
    if (msg.msgid == MSG_HEARTBEAT &&
        (msg.sysid != ap_system_ || msg.compid != ap_component_)) {
        ++stats_.hb_ignored;
        const std::uint32_t key = (static_cast<std::uint32_t>(msg.sysid) << 8) | msg.compid;
        if (!hb_ignored_seen_[key]) {
            hb_ignored_seen_[key] = true;
            if (debug_) {
                log_line("r2mav", "DEBUG",
                         "ignoring HEARTBEAT from %d/%d (autopilot filter is %d/%d)",
                         msg.sysid, msg.compid, ap_system_, ap_component_);
            }
        }
        return;
    }

    ++stats_.by_type[msg.msgid];
    if (!first_seen_[msg.msgid]) {
        first_seen_[msg.msgid] = true;
        if (debug_) {
            log_line("r2mav", "DEBUG", "first %s from %d/%d", message_name(msg.msgid).c_str(),
                     msg.sysid, msg.compid);
        }
    }

    if (msg.msgid == MSG_GIMBAL_DEVICE_ATTITUDE_STATUS && gimbal_cb_) {
        mavlink_gimbal_device_attitude_status_t m;
        mavlink_msg_gimbal_device_attitude_status_decode(&msg, &m);
        GimbalAttitude g;
        g.time_boot_ms = m.time_boot_ms;
        std::memcpy(g.q, m.q, sizeof(g.q));
        g.flags = m.flags;
        g.failure_flags = m.failure_flags;
        g.gimbal_device_id = m.gimbal_device_id;
        g.src_system = msg.sysid;
        g.src_component = msg.compid;
        gimbal_cb_(g);
    }
}

void Mav::debug_tick(double now) {
    // Одне попередження, коли потік затих (як _debug_tick у r2mav.py).
    const double silent = now - last_rx_;
    if (silent >= NO_DATA_WARN_S && !no_data_warned_) {
        no_data_warned_ = true;
        log_line("r2mav", "WARNING", "no valid MAVLink data for %.1fs on %s", silent,
                 connect_.c_str());
    }
    // Періодичний рядок статистики — заодно показує, що прийом живий.
    if (now - stats_t_ >= STATS_INTERVAL_S) {
        const double rate = static_cast<double>(stats_.rx_total - stats_total_) / (now - stats_t_);
        stats_t_ = now;
        stats_total_ = stats_.rx_total;
        log_line("r2mav", "DEBUG", "%.1f msg/s | %s", rate, stats_line().c_str());
    }
}

unsigned long Mav::count_of(std::uint32_t msg_id) const {
    const auto it = stats_.by_type.find(msg_id);
    return it == stats_.by_type.end() ? 0 : it->second;
}

std::string Mav::message_name(std::uint32_t id) {
    // Назви найчастіших повідомлень ArduPilot. Повна таблиця назв займала б
    // сотні кілобайт, тож для інших пишемо MSG_<id>.
    switch (id) {
        case 0: return "HEARTBEAT";
        case 1: return "SYS_STATUS";
        case 2: return "SYSTEM_TIME";
        case 24: return "GPS_RAW_INT";
        case 30: return "ATTITUDE";
        case 33: return "GLOBAL_POSITION_INT";
        case 65: return "RC_CHANNELS";
        case 74: return "VFR_HUD";
        case 76: return "COMMAND_LONG";
        case 77: return "COMMAND_ACK";
        case 111: return "TIMESYNC";
        case 147: return "BATTERY_STATUS";
        case 158: return "MOUNT_STATUS";
        case 253: return "STATUSTEXT";
        case 285: return "GIMBAL_DEVICE_ATTITUDE_STATUS";
        case 385: return "TUNNEL";
        default: return "MSG_" + std::to_string(id);
    }
}

std::string Mav::stats_line() const {
    char head[256];
    std::snprintf(head, sizeof(head),
                  "rx=%lu bad=%lu rx_err=%lu hb_sent=%lu hb_ignored=%lu tunnel=%lu cb_err=%lu",
                  stats_.rx_total, stats_.rx_bad, stats_.rx_errors, stats_.hb_sent,
                  stats_.hb_ignored, stats_.tunnel_rx, stats_.cb_errors);
    std::string line = head;
    // Як у Python: типи за абеткою назв.
    std::map<std::string, unsigned long> named;
    for (const auto& kv : stats_.by_type) named[message_name(kv.first)] += kv.second;
    bool first = true;
    for (const auto& kv : named) {
        line += first ? " | " : ", ";
        first = false;
        line += kv.first + "=" + std::to_string(kv.second);
    }
    return line;
}

}  // namespace zr10
