// mav.hpp — MAVLink-з'єднання з R2D2.
// Переклад потрібної плагіну частини api/r2mav.py: підключення по UDP,
// HEARTBEAT раз на секунду, прийом повідомлень, фільтр HEARTBEAT від
// автопілота, лічильники для діагностики, MAV_CMD_SET_MESSAGE_INTERVAL.
//
// Відмінність від Python: тут немає окремого потоку прийому. Головний цикл
// плагіна викликає poll() кожні 50 мс, і poll() забирає всі пакети, що
// прийшли. Затримка не більша за 50 мс, а програма простіша й надійніша.
//
// Рядок підключення — як у pymavlink:
//   udpout:127.0.0.1:14550  — слати на цю адресу, відповіді приходять назад
//                             (так підключається плагін до R2D2; за замовч.)
//   udpin:0.0.0.0:14550     — слухати порт, відповідати останньому відправнику
//   udp:...                 — те саме, що udpin
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace zr10 {

// Дані повідомлення 285 GIMBAL_DEVICE_ATTITUDE_STATUS, потрібні плагіну.
struct GimbalAttitude {
    std::uint32_t time_boot_ms = 0;
    float q[4] = {0, 0, 0, 0};  // w, x, y, z
    std::uint16_t flags = 0;
    std::uint32_t failure_flags = 0;
    std::uint8_t gimbal_device_id = 0;
    std::uint8_t src_system = 0;
    std::uint8_t src_component = 0;
};

class Mav {
public:
    static constexpr std::uint32_t MSG_HEARTBEAT = 0;
    static constexpr std::uint32_t MSG_GIMBAL_DEVICE_ATTITUDE_STATUS = 285;

    struct Stats {
        unsigned long rx_total = 0;    // коректних повідомлень
        unsigned long rx_bad = 0;      // зіпсованих кадрів (помилка CRC)
        unsigned long rx_errors = 0;   // помилок прийому з мережі
        unsigned long hb_sent = 0;
        unsigned long hb_ignored = 0;  // HEARTBEAT не від автопілота
        unsigned long tunnel_rx = 0;   // завжди 0: TUNNEL плагіну не потрібен
        unsigned long cb_errors = 0;   // завжди 0: у C++ колбеки не кидають винятків
        std::map<std::uint32_t, unsigned long> by_type;  // id повідомлення → кількість
    };

    // Значення за замовчуванням ті самі, що в R2Mav.__init__.
    explicit Mav(std::string connect = "udpout:127.0.0.1:14550",
                 std::uint8_t source_system = 255, std::uint8_t source_component = 0,
                 double heartbeat_hz = 1.0, std::uint8_t ap_system = 1,
                 std::uint8_t ap_component = 1, bool debug = false);
    ~Mav();
    Mav(const Mav&) = delete;
    Mav& operator=(const Mav&) = delete;

    // Відкрити з'єднання. false — не вдалося (рядок або сокет); програма не
    // падає, poll() просто нічого не робитиме.
    bool start();
    void stop();

    // Забрати всі пакети, що прийшли, викликати колбеки, вчасно надіслати
    // HEARTBEAT, написати попередження/статистику. Не чекає.
    void poll();

    // Колбек для кожного повідомлення 285 (від будь-якого відправника).
    void on_gimbal(std::function<void(const GimbalAttitude&)> callback) {
        gimbal_cb_ = std::move(callback);
    }

    // Попросити автопілот слати повідомлення msg_id з частотою rate_hz
    // (rate_hz <= 0 — повернути частоту за замовчуванням). Як у r2mav.py.
    void set_message_interval(std::uint32_t msg_id, double rate_hz);

    const Stats& stats() const { return stats_; }
    unsigned long count_of(std::uint32_t msg_id) const;
    const std::string& connect_string() const { return connect_; }
    bool sending_mavlink2() const { return out_mavlink2_; }

    // Назва повідомлення для журналу (відомі плагіну; інші — "MSG_<id>").
    static std::string message_name(std::uint32_t msg_id);
    std::string stats_line() const;

    static constexpr double NO_DATA_WARN_S = 5.0;    // як у r2mav.py
    static constexpr double STATS_INTERVAL_S = 5.0;  // як у r2mav.py

private:
    void send_heartbeat();
    void send_message(const void* mavlink_message);
    void handle_byte(std::uint8_t byte);
    void debug_tick(double now);
    static double now_s();

    std::string connect_;
    std::uint8_t source_system_, source_component_;
    double heartbeat_interval_;  // 0 — не слати
    std::uint8_t ap_system_, ap_component_;
    bool debug_;

    int sock_ = -1;
    bool listen_mode_ = false;      // udpin: відповідати останньому відправнику
    unsigned char target_[16] = {};  // sockaddr_in
    bool have_target_ = false;
    bool running_ = false;
    bool decided_version_ = false;  // як pymavlink: версію визначає перший пакет
    bool out_mavlink2_ = false;     // спершу MAVLink 1, як pymavlink без MAVLINK20

    double last_hb_ = 0.0;
    double last_rx_ = 0.0;
    bool no_data_warned_ = false;
    double stats_t_ = 0.0;
    unsigned long stats_total_ = 0;
    bool bad_data_logged_ = false;
    std::map<std::uint32_t, bool> first_seen_;
    std::map<std::uint32_t, bool> hb_ignored_seen_;  // ключ: (sys << 8) | comp

    Stats stats_;
    std::function<void(const GimbalAttitude&)> gimbal_cb_;
};

}  // namespace zr10
