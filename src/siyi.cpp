// siyi.cpp — див. siyi.hpp.
#include "siyi.hpp"

#include <cmath>
#include <cstring>
#include <utility>

#include "net.hpp"

namespace zr10 {

namespace {
constexpr std::uint8_t CMD_ATTITUDE = 0x0D;

std::int16_t le16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0]) |
                                     (static_cast<std::uint16_t>(p[1]) << 8));
}
}  // namespace

std::uint16_t siyi_crc16(const std::uint8_t* p, std::size_t n) {
    std::uint16_t c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= static_cast<std::uint16_t>(p[i]) << 8;
        for (int b = 0; b < 8; ++b)
            c = (c & 0x8000) ? static_cast<std::uint16_t>((c << 1) ^ 0x1021)
                             : static_cast<std::uint16_t>(c << 1);
    }
    return c;
}

std::size_t siyi_attitude_request(std::uint8_t* out, std::uint16_t seq) {
    // 55 66 | CTRL | LEN(2) | SEQ(2) | CMD | CRC16(2), усе little-endian.
    out[0] = 0x55;
    out[1] = 0x66;
    out[2] = 0x01;  // потрібна відповідь
    out[3] = 0x00;
    out[4] = 0x00;
    out[5] = static_cast<std::uint8_t>(seq & 0xFF);
    out[6] = static_cast<std::uint8_t>(seq >> 8);
    out[7] = CMD_ATTITUDE;
    const std::uint16_t c = siyi_crc16(out, 8);
    out[8] = static_cast<std::uint8_t>(c & 0xFF);
    out[9] = static_cast<std::uint8_t>(c >> 8);
    return 10;
}

bool siyi_parse_attitude(const std::uint8_t* p, std::size_t n, double* yaw, double* pitch,
                         double* roll) {
    if (n < 10 || p[0] != 0x55 || p[1] != 0x66) return false;
    const std::size_t len = static_cast<std::size_t>(p[3]) | (static_cast<std::size_t>(p[4]) << 8);
    if (n < 8 + len + 2) return false;
    const std::uint16_t want = static_cast<std::uint16_t>(p[8 + len] | (p[9 + len] << 8));
    if (siyi_crc16(p, 8 + len) != want) return false;
    if (p[7] != CMD_ATTITUDE || len < 6) return false;
    *yaw = le16(p + 8) / 10.0;
    *pitch = le16(p + 10) / 10.0;
    *roll = le16(p + 12) / 10.0;
    return true;
}

const char* siyi_state_name(SiyiState s) {
    switch (s) {
        case SiyiState::Off: return "off";
        case SiyiState::Ok: return "ok";
        case SiyiState::Lost: return "no reply";
        default: return "unknown";
    }
}

SiyiReader::SiyiReader(std::string ip, int port, double rate_hz) : ip_(std::move(ip)), port_(port) {
    if (rate_hz <= 0.0) rate_hz = 2.0;
    if (rate_hz > 5.0) rate_hz = 5.0;  // не навантажувати гімбал
    period_ = 1.0 / rate_hz;
    if (ip_.empty()) return;
    enabled_ = true;
    state_ = SiyiState::Unknown;
    net::init();
    sock_ = net::udp_socket();  // довільний вільний порт, 37260 на камері не займаємо
    if (sock_ >= 0) net::set_nonblocking(sock_);
    const sockaddr_in a = net::make_addr(ip_.c_str(), port_);
    std::memcpy(addr_, &a, sizeof(a));
}

SiyiReader::~SiyiReader() {
    if (sock_ >= 0) net::close_socket(sock_);
}

void SiyiReader::feed(const std::uint8_t* p, std::size_t n, double now) {
    double y, pi, r;
    if (!siyi_parse_attitude(p, n, &y, &pi, &r)) return;  // чужі відповіді (зум R2D2) — пропускаємо
    if (have_) motion_ = std::fabs(angle_diff_deg(y, yaw_)) + std::fabs(pi - pitch_);
    yaw_ = y;
    pitch_ = pi;
    roll_ = r;
    have_ = true;
    last_reply_ = now;
    ++replies_;
}

void SiyiReader::update_state(double now) {
    if (!enabled_) {
        state_ = SiyiState::Off;
    } else if (now - last_reply_ <= LOST_S) {
        state_ = SiyiState::Ok;
    } else if (replies_ > 0) {
        state_ = SiyiState::Lost;  // відповідав, а тепер мовчить
    } else {
        state_ = SiyiState::Unknown;  // ще жодного разу не відповів — не тривожимо
    }
}

bool SiyiReader::poll(double now) {
    const SiyiState before = state_;
    if (enabled_ && sock_ >= 0) {
        if (started_ < 0) {
            started_ = now;
            next_ = now;
        }
        if (now >= next_) {
            std::uint8_t req[16];
            const std::size_t n = siyi_attitude_request(req, seq_++);
            sockaddr_in to;
            std::memcpy(&to, addr_, sizeof(to));
            if (net::send_to(sock_, req, static_cast<int>(n), to)) ++requests_;
            next_ += period_;
            if (next_ < now) next_ = now + period_;
        }
        std::uint8_t buf[512];
        for (int i = 0; i < 64; ++i) {  // за раз — не більше 64 пакетів
            sockaddr_in from;
            const int r = net::recv_from(sock_, buf, sizeof(buf), from);
            if (r <= 0) break;
            feed(buf, static_cast<std::size_t>(r), now);
        }
    }
    update_state(now);
    return state_ != before;
}

double angle_diff_deg(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

bool FcLinkCheck::update(double now, bool have_fc, double fc_yaw, double fc_pitch, bool have_siyi,
                         double siyi_yaw, double siyi_pitch, bool still) {
    const bool before = lost_;
    if (!have_fc || !have_siyi) {
        mismatch_since_ = -1.0;
        match_since_ = -1.0;
        lost_ = false;
        return lost_ != before;
    }
    yaw_diff_ = angle_diff_deg(fc_yaw, yaw_sign_ * siyi_yaw);
    pitch_diff_ = fc_pitch - siyi_pitch;
    const bool mismatch = std::fabs(yaw_diff_) > MISMATCH_DEG || std::fabs(pitch_diff_) > MISMATCH_DEG;
    if (!mismatch) {
        mismatch_since_ = -1.0;
        // 0.3.7: «знову ok» лише коли кути збігаються на НЕРУХОМОМУ гімбалі
        // CLEAR_S — на ходу гімбал може випадково проїхати через застиглий кут
        // контролера (тест 2026-11-11, 13:56:11).
        if (!lost_) {
            match_since_ = -1.0;
        } else if (still) {
            if (match_since_ < 0) match_since_ = now;
            if (now - match_since_ >= CLEAR_S) {
                lost_ = false;
                match_since_ = -1.0;
            }
        } else {
            match_since_ = -1.0;
        }
    } else {
        match_since_ = -1.0;
        if (still) {
            if (mismatch_since_ < 0) mismatch_since_ = now;
            if (now - mismatch_since_ >= HOLD_S) lost_ = true;
        }
    }
    // Гімбал рухається й кути розходяться — чекаємо, поки зупиниться (lost_ не змінюємо).
    return lost_ != before;
}

}  // namespace zr10
