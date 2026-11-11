// osd.cpp — див. пояснення в osd.hpp.
#include "osd.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "net.hpp"

namespace zr10 {

static_assert(sizeof(sockaddr_in) <= 16, "sockaddr_in має вміщатися в addr_");

Osd::Osd(const char* ip, int port) {
    const sockaddr_in addr = net::make_addr(ip, port);
    std::memcpy(addr_, &addr, sizeof(addr));
    sock_ = net::udp_socket();
    // Якщо сокет не створився — не падаємо: send() рахуватиме помилки.
}

Osd::Osd(std::function<void(const std::string&)> sink) : sink_(std::move(sink)) {}

Osd::~Osd() { close(); }

void Osd::set_only_changes(bool enabled, double resend_s) {
    only_changes_ = enabled;
    resend_s_ = resend_s > 0.0 ? resend_s : 5.0;
    last_sent_.clear();
}

double Osd::now() const {
    if (clock_) return clock_();
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void Osd::close() {
    if (sock_ >= 0) {
        net::close_socket(sock_);
        sock_ = -1;
    }
}

// ---------------------------------------------------------------- JSON ----

void Osd::append_number(std::string& out, double value) {
    // Нескінченність/NaN у JSON недопустимі; R2D2 міг би не розібрати пакет.
    // У плагіні таких значень бути не може, але про всяк випадок шлемо 0.0.
    if (!std::isfinite(value)) value = 0.0;

    if (value == 0.0) {
        out += std::signbit(value) ? "-0.0" : "0.0";
        return;
    }

    // Найкоротший запис, який точно повертається в те саме число
    // (так робить Python repr). Шукаємо мінімальну кількість цифр.
    char buf[40];
    int digits = 1;
    for (; digits <= 17; ++digits) {
        std::snprintf(buf, sizeof(buf), "%.*e", digits - 1, value);
        if (std::strtod(buf, nullptr) == value) break;
    }
    if (digits > 17) {
        digits = 17;
        std::snprintf(buf, sizeof(buf), "%.16e", value);
    }

    // buf має вигляд "-d.ddddde+XX". Дістаємо знак, цифри і порядок.
    const char* p = buf;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    std::string mantissa;
    for (; *p && *p != 'e'; ++p) {
        if (*p != '.') mantissa += *p;
    }
    const int exponent = (*p == 'e') ? std::atoi(p + 1) : 0;

    if (negative) out += '-';

    // Правило Python repr: звичайний запис, якщо -4 <= порядок < 16,
    // інакше експоненційний (1e-05, 1.5e+16).
    if (exponent >= -4 && exponent < 16) {
        const int n = static_cast<int>(mantissa.size());
        if (exponent >= 0) {
            const int int_digits = exponent + 1;
            if (n <= int_digits) {
                out += mantissa;
                out.append(static_cast<size_t>(int_digits - n), '0');
                out += ".0";
            } else {
                out.append(mantissa, 0, static_cast<size_t>(int_digits));
                out += '.';
                out.append(mantissa, static_cast<size_t>(int_digits), std::string::npos);
            }
        } else {
            out += "0.";
            out.append(static_cast<size_t>(-exponent - 1), '0');
            out += mantissa;
        }
    } else {
        out += mantissa[0];
        if (mantissa.size() > 1) {
            out += '.';
            out.append(mantissa, 1, std::string::npos);
        }
        char exp_buf[16];
        std::snprintf(exp_buf, sizeof(exp_buf), "e%c%02d", exponent < 0 ? '-' : '+',
                      std::abs(exponent));
        out += exp_buf;
    }
}

namespace {

void append_hex4(std::string& out, unsigned value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "\\u%04x", value & 0xFFFFu);
    out += buf;
}

}  // namespace

void Osd::append_string(std::string& out, const std::string& value) {
    // Як json.dumps з ensure_ascii=True: лапки, \\, керуючі символи та всі
    // не-ASCII символи (кирилиця тощо) екрануються як \uXXXX.
    out += '"';
    const auto* s = reinterpret_cast<const unsigned char*>(value.data());
    const size_t n = value.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = s[i];
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) append_hex4(out, c);
                    else out += static_cast<char>(c);
            }
            ++i;
            continue;
        }
        // Розбір UTF-8. Некоректний байт замінюємо на U+FFFD.
        unsigned cp = 0xFFFD;
        size_t len = 1;
        if ((c & 0xE0) == 0xC0 && i + 1 < n && (s[i + 1] & 0xC0) == 0x80) {
            cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n && (s[i + 1] & 0xC0) == 0x80 &&
                   (s[i + 2] & 0xC0) == 0x80) {
            cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n && (s[i + 1] & 0xC0) == 0x80 &&
                   (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) {
            cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) |
                 ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu);
            len = 4;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            append_hex4(out, 0xD800 + (cp >> 10));
            append_hex4(out, 0xDC00 + (cp & 0x3FF));
        } else {
            append_hex4(out, cp);
        }
        i += len;
    }
    out += '"';
}

// ------------------------------------------------------------- batching ----

void Osd::begin_batch() { ++batch_depth_; }

void Osd::end_batch() {
    if (batch_depth_ == 0) return;
    if (--batch_depth_ == 0) flush();
}

void Osd::flush() {
    if (pending_.empty()) return;
    std::string payload = rects_prefix();
    for (size_t i = 0; i < pending_.size(); ++i) {
        if (i) payload += ", ";
        payload += pending_[i];
    }
    payload += "]}";
    pending_.clear();
    send(payload);
}

void Osd::emit(const std::string& id, std::string element_json, bool is_clear) {
    if (only_changes_) {
        if (is_clear) {
            // Після прибирання елемента наступне малювання має піти обов'язково.
            last_sent_.erase(id);
        } else {
            const double t = now();
            auto it = last_sent_.find(id);
            if (it != last_sent_.end() && it->second.json == element_json &&
                t - it->second.at < resend_s_) {
                ++skipped_;
                return;  // нічого не змінилось і ще рано повторювати
            }
            SentElement& entry = last_sent_[id];
            entry.json = element_json;
            entry.at = t;
        }
    }
    if (batch_depth_ > 0) {
        pending_.push_back(std::move(element_json));
    } else {
        send(rects_prefix() + element_json + "]}");
    }
}

std::string Osd::rects_prefix() const {
    if (cam_ == NO_CAM) return "{\"rects\": [";
    return "{\"cam\": " + std::to_string(cam_) + ", \"rects\": [";
}

std::string Osd::ping(double timeout_s) {
    if (sink_ || sock_ < 0) return std::string();
    net::set_nonblocking(sock_);
    sockaddr_in to;
    std::memcpy(&to, addr_, sizeof(to));
    static const char kPing[] = "{\"ping\":true}";
    if (!net::send_to(sock_, kPing, static_cast<int>(sizeof(kPing) - 1), to)) return std::string();
    const double deadline = now() + timeout_s;
    char buf[4096];
    while (now() < deadline) {
        sockaddr_in from;
        const int n = net::recv_from(sock_, buf, sizeof(buf) - 1, from);
        if (n > 0) {
            buf[n] = 0;
            if (std::strstr(buf, "pong") != nullptr) return std::string(buf, static_cast<size_t>(n));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    return std::string();
}

void Osd::send(const std::string& payload) {
    if (sink_) {
        sink_(payload);
        ++sent_;
        return;
    }
    if (sock_ < 0) {
        ++errors_;
        return;
    }
    sockaddr_in to;
    std::memcpy(&to, addr_, sizeof(to));
    if (net::send_to(sock_, payload.data(), static_cast<int>(payload.size()), to)) ++sent_;
    else ++errors_;
}

// ----------------------------------------------------------- primitives ----

void Osd::rect(const std::string& id, double x, double y, double width,
               double height, const RectFields& f) {
    // Порядок ключів той самий, що в R2Draw.rect().
    std::string j;
    j.reserve(200);
    j += "{\"id\": ";
    append_string(j, id);
    j += ", \"position\": {\"x\": ";
    append_number(j, x);
    j += ", \"y\": ";
    append_number(j, y);
    j += ", \"width\": ";
    append_number(j, width);
    j += ", \"height\": ";
    append_number(j, height);
    j += '}';
    if (f.border) {
        j += ", \"border\": ";
        j += std::to_string(*f.border);
    }
    if (f.border_width) {
        j += ", \"width\": ";
        append_number(j, *f.border_width);
    }
    if (f.fill) {
        j += ", \"fill\": ";
        j += std::to_string(*f.fill);
    }
    if (f.text) {
        j += ", \"text\": ";
        append_string(j, *f.text);
    }
    if (f.size) {
        j += ", \"size\": ";
        append_number(j, *f.size);
    }
    if (f.align) {
        j += ", \"align\": ";
        j += std::to_string(*f.align);
    }
    if (f.color) {
        j += ", \"color\": ";
        j += std::to_string(*f.color);
    }
    if (f.outline) {
        j += ", \"outline\": ";
        j += std::to_string(*f.outline);
    }
    if (f.image) {
        j += ", \"image\": ";
        append_string(j, *f.image);
    }
    if (f.style) {
        j += ", \"style\": ";
        j += std::to_string(*f.style);
    }
    if (f.ialign) {
        j += ", \"ialign\": ";
        j += std::to_string(*f.ialign);
    }
    j += '}';
    emit(id, std::move(j), false);
}

void Osd::text(const std::string& id, double x, double y, const std::string& value,
               double size, int align, std::optional<std::uint32_t> border,
               std::optional<std::uint32_t> fill, double width, double height,
               double border_width, std::optional<std::uint32_t> color,
               std::optional<std::uint32_t> outline) {
    RectFields f;
    f.color = color;
    f.outline = outline;
    f.border = border;
    f.fill = fill;
    f.text = value;
    f.size = size;
    f.align = align;
    f.border_width = border_width;
    rect(id, x, y, width, height, f);
}

void Osd::line(const std::string& id, double x1, double y1, double x2, double y2,
               std::uint32_t fill, double width, int extra_style) {
    RectFields f;
    f.fill = fill;
    f.border_width = width;
    f.style = STYLE_LINE | extra_style;
    rect(id, x1, y1, x2 - x1, y2 - y1, f);
}

void Osd::sline(const std::string& id, double x1, double y1, double x2, double y2,
                std::uint32_t color, double thickness) {
    // Той самий розрахунок, що R2Draw.sline() у r2draw.py.
    double x = x1 < x2 ? x1 : x2;
    double y = y1 < y2 ? y1 : y2;
    double w = std::fabs(x2 - x1);
    double h = std::fabs(y2 - y1);
    if (w < thickness) {
        x = (x1 + x2) / 2.0 - thickness / 2.0;
        w = thickness;
    }
    if (h < thickness) {
        y = (y1 + y2) / 2.0 - thickness / 2.0;
        h = thickness;
    }
    RectFields f;
    f.fill = color;
    f.border = color;
    f.border_width = thickness;
    rect(id, x, y, w, h, f);
}

void Osd::image(const std::string& id, double x, double y, double width,
                double height, const std::string& image, std::optional<int> style) {
    RectFields f;
    f.image = image;
    f.style = style;
    rect(id, x, y, width, height, f);
}

void Osd::clear(const std::string& id) {
    std::string j = "{\"id\": ";
    append_string(j, id);
    j += ", \"clear\": true}";
    emit(id, std::move(j), true);
}

void Osd::clear_all() {
    // Як у Python: загальне очищення завжди йде окремим повідомленням,
    // навіть усередині пакета.
    send("{\"clear\": true}");
    last_sent_.clear();  // на екрані нічого немає — наступне малювання піде повністю
}

}  // namespace zr10
