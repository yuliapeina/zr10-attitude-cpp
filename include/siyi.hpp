// siyi.hpp — читання кутів ZR10 напряму (протокол SIYI SDK по UDP), 0.3.5.
//
// Навіщо: польотний контролер (ArduPilot) отримує кути гімбала по UART і
// пересилає їх у MAVLink 285. Коли UART обривається, контролер далі шле
// ОСТАННІ кути без жодної ознаки збою. Тому плагін питає гімбал сам і
// порівнює.
//
// Що надсилається гімбалу: ЛИШЕ запит «дай поточні кути» (CMD 0x0D, без
// даних), не частіше SIYI_RATE_HZ. Ні руху, ні зуму, ні налаштувань. Тест
// siyi_probe на камері (2026-11-11): 57/60 відповідей, ~29 мс, керування з
// R2D2 при цьому працювало як завжди. Відповіді на чужі команди (наприклад
// 0x05 — зум від R2D2) гімбал теж присилає — вони ігноруються.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace zr10 {

// --- протокол (окремо — для тестів) ---------------------------------------
std::uint16_t siyi_crc16(const std::uint8_t* p, std::size_t n);
// Запит кутів (10 байт) у out. Повертає довжину.
std::size_t siyi_attitude_request(std::uint8_t* out, std::uint16_t seq);
// Розбір відповіді 0x0D: true, якщо це правильний кадр із кутами (градуси).
bool siyi_parse_attitude(const std::uint8_t* p, std::size_t n, double* yaw, double* pitch,
                         double* roll);

enum class SiyiState { Off, Unknown, Ok, Lost };
const char* siyi_state_name(SiyiState s);

class SiyiReader {
public:
    static constexpr double LOST_S = 3.0;  // без відповіді стільки — «Gimbal lost»

    // ip == "" — вимкнено.
    SiyiReader(std::string ip, int port, double rate_hz);
    ~SiyiReader();
    SiyiReader(const SiyiReader&) = delete;
    SiyiReader& operator=(const SiyiReader&) = delete;

    // Викликати часто (кожен цикл плагіна): надсилає запит, коли пора, і
    // читає всі відповіді, що прийшли. Повертає true, якщо змінився стан.
    bool poll(double now);

    SiyiState state() const { return state_; }
    bool enabled() const { return enabled_; }
    bool have_angles() const { return have_; }
    double yaw() const { return yaw_; }
    double pitch() const { return pitch_; }
    double roll() const { return roll_; }
    double last_reply() const { return last_reply_; }
    // Зміна кутів між двома останніми відповідями (градуси) — чи рухається.
    double last_motion() const { return motion_; }
    unsigned long replies() const { return replies_; }
    unsigned long requests() const { return requests_; }
    const std::string& ip() const { return ip_; }

    // Для тестів: подати відповідь без мережі.
    void feed(const std::uint8_t* p, std::size_t n, double now);

private:
    std::string ip_;
    int port_ = 37260;
    double period_ = 0.5;
    bool enabled_ = false;
    int sock_ = -1;
    unsigned char addr_[16] = {};
    double next_ = 0.0;
    double started_ = -1.0;
    double last_reply_ = -1e9;
    std::uint16_t seq_ = 0;
    SiyiState state_ = SiyiState::Off;
    bool have_ = false;
    double yaw_ = 0, pitch_ = 0, roll_ = 0, motion_ = 0;
    unsigned long replies_ = 0, requests_ = 0;
    void update_state(double now);
};

// --- порівняння з кутами від польотного контролера ------------------------
//
// ArduPilot (AP_Mount_Siyi) повертає yaw SIYI з протилежним знаком; pitch —
// той самий. Тому yaw_fc ≈ yaw_sign * yaw_siyi (yaw_sign = -1 за замовч.).
// «FC link lost» — лише коли гімбал СТОЇТЬ (рух між відповідями < STILL_DEG):
// під час швидкого повороту дані контролера трохи запізнюються.
class FcLinkCheck {
public:
    static constexpr double MISMATCH_DEG = 3.0;
    static constexpr double STILL_DEG = 0.5;
    static constexpr double HOLD_S = 2.0;
    static constexpr double CLEAR_S = 1.0;  // 0.3.7: збіг на нерухомому гімбалі стільки — знову ok

    explicit FcLinkCheck(int yaw_sign = -1) : yaw_sign_(yaw_sign) {}

    // fc_* — останні кути з 285; siyi_* — останні з гімбала; still — гімбал
    // не рухається. Повертає true, якщо змінився стан lost().
    bool update(double now, bool have_fc, double fc_yaw, double fc_pitch, bool have_siyi,
                double siyi_yaw, double siyi_pitch, bool still);
    bool lost() const { return lost_; }
    double yaw_diff() const { return yaw_diff_; }
    double pitch_diff() const { return pitch_diff_; }

private:
    int yaw_sign_;
    bool lost_ = false;
    double mismatch_since_ = -1.0;
    double match_since_ = -1.0;
    double yaw_diff_ = 0, pitch_diff_ = 0;
};

// Найменша різниця кутів (−180..180].
double angle_diff_deg(double a, double b);

}  // namespace zr10
