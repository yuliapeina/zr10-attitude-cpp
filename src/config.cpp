// config.cpp — див. пояснення в config.hpp.
#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace zr10 {

namespace {

// ------------------------------------------------ невеликий розбірник JSON ----
// Приймає те саме, що json.load у Python, зокрема NaN / Infinity / -Infinity.

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    bool parse_document(std::map<std::string, JsonValue>& out) {
        skip_ws();
        if (!peek('{')) return false;
        if (!parse_object(&out, 0)) return false;
        skip_ws();
        return pos_ == s_.size();
    }

private:
    static constexpr int MAX_DEPTH = 64;

    bool peek(char c) const { return pos_ < s_.size() && s_[pos_] == c; }

    void skip_ws() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(const char* word) {
        const size_t n = std::char_traits<char>::length(word);
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    // top != nullptr — зберегти ключі верхнього рівня.
    bool parse_object(std::map<std::string, JsonValue>* top, int depth, bool* non_empty = nullptr) {
        if (depth > MAX_DEPTH) return false;
        ++pos_;  // '{'
        skip_ws();
        if (peek('}')) {
            ++pos_;
            if (non_empty) *non_empty = false;
            return true;
        }
        if (non_empty) *non_empty = true;
        while (true) {
            skip_ws();
            std::string key;
            if (!peek('"') || !parse_string(key)) return false;
            skip_ws();
            if (!peek(':')) return false;
            ++pos_;
            JsonValue v;
            if (!parse_value(v, depth + 1)) return false;
            if (top) (*top)[key] = v;  // як у Python: повторений ключ — останнє значення
            skip_ws();
            if (peek(',')) { ++pos_; continue; }
            if (peek('}')) { ++pos_; return true; }
            return false;
        }
    }

    bool parse_array(int depth, bool& non_empty) {
        if (depth > MAX_DEPTH) return false;
        ++pos_;  // '['
        skip_ws();
        if (peek(']')) { ++pos_; non_empty = false; return true; }
        non_empty = true;
        while (true) {
            JsonValue v;
            if (!parse_value(v, depth + 1)) return false;
            skip_ws();
            if (peek(',')) { ++pos_; continue; }
            if (peek(']')) { ++pos_; return true; }
            return false;
        }
    }

    bool parse_value(JsonValue& v, int depth) {
        skip_ws();
        if (pos_ >= s_.size()) return false;
        const char c = s_[pos_];
        if (c == '{') { v.type = JsonValue::Object; return parse_object(nullptr, depth, &v.non_empty); }
        if (c == '[') { v.type = JsonValue::Array; return parse_array(depth, v.non_empty); }
        if (c == '"') { v.type = JsonValue::String; return parse_string(v.str); }
        if (literal("true")) { v.type = JsonValue::Bool; v.b = true; return true; }
        if (literal("false")) { v.type = JsonValue::Bool; v.b = false; return true; }
        if (literal("null")) { v.type = JsonValue::Null; return true; }
        if (literal("NaN")) { v.type = JsonValue::Number; v.num = std::nan(""); return true; }
        if (literal("Infinity")) {
            v.type = JsonValue::Number; v.num = std::numeric_limits<double>::infinity(); return true;
        }
        if (literal("-Infinity")) {
            v.type = JsonValue::Number; v.num = -std::numeric_limits<double>::infinity(); return true;
        }
        return parse_number(v);
    }

    bool parse_number(JsonValue& v) {
        // Граматика числа JSON: -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
        const size_t start = pos_;
        if (peek('-')) ++pos_;
        if (peek('0')) {
            ++pos_;
        } else if (pos_ < s_.size() && s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        } else {
            return false;
        }
        bool is_int = true;
        if (peek('.')) {
            is_int = false;
            ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return false;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (peek('e') || peek('E')) {
            is_int = false;
            ++pos_;
            if (peek('+') || peek('-')) ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return false;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        v.type = JsonValue::Number;
        v.num_is_int = is_int;
        v.num = std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr);
        return true;
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& value) {
        if (pos_ + 4 > s_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // '"'
        while (pos_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;  // керуючі символи в рядку JSON заборонені
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ >= s_.size()) return false;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(pos_, 2, "\\u") == 0) {
                        const size_t save = pos_;
                        pos_ += 2;
                        unsigned low;
                        if (hex4(low) && low >= 0xDC00 && low < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save;
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    const std::string& s_;
    size_t pos_ = 0;
};

std::string strip_lower(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string r = s.substr(a, b - a);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return r;
}

// Python дозволяє «_» між цифрами: "1_000" == 1000. Прибрати такі «_»;
// false — «_» стоїть не між двома цифрами.
bool remove_digit_underscores(std::string& t) {
    std::string r;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '_') { r += t[i]; continue; }
        const bool ok = i > 0 && i + 1 < t.size() &&
                        std::isdigit(static_cast<unsigned char>(t[i - 1])) &&
                        std::isdigit(static_cast<unsigned char>(t[i + 1]));
        if (!ok) return false;
    }
    t = r;
    return true;
}

// float("...") у Python: пробіли по краях дозволені, решта має бути числом.
bool python_float_from_string(const std::string& s, double& out) {
    std::string t = strip_lower(s);
    if (t.empty() || !remove_digit_underscores(t)) return false;
    // strtod приймає також шістнадцяткові числа ("0x10") і "nan(...)", а Python — ні.
    if (t.find('x') != std::string::npos || t.find('(') != std::string::npos) return false;
    // Python: "inf", "infinity", "nan" (зі знаком); strtod — так само.
    for (char ch : t) {
        if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == 'e' ||
              ch == '+' || ch == '-' || ch == 'i' || ch == 'n' || ch == 'f' || ch == 't' ||
              ch == 'y' || ch == 'a'))
            return false;
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

// int("...") у Python: лише ціле число (з пробілами по краях, знаком).
bool python_int_from_string(const std::string& s, long long& out) {
    std::string t = strip_lower(s);
    if (t.empty() || !remove_digit_underscores(t)) return false;
    const size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
    if (i >= t.size()) return false;
    for (size_t k = i; k < t.size(); ++k) {
        if (!std::isdigit(static_cast<unsigned char>(t[k]))) return false;
    }
    char* end = nullptr;
    out = std::strtoll(t.c_str(), &end, 10);  // завеликі числа обмежуються, не ламаються
    return *end == '\0';
}

// Python відмовляється читати файл з некоректним UTF-8 (і тоді — усе за
// замовчуванням). Перевіряємо так само.
bool valid_utf8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        size_t len;
        unsigned cp;
        if (c < 0x80) { ++i; continue; }
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07u; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + k] & 0x3Fu);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
            return false;
        i += len;
    }
    return true;
}

}  // namespace

bool parse_json_object(const std::string& text, std::map<std::string, JsonValue>& out) {
    out.clear();
    Parser p(text);
    if (!p.parse_document(out)) {
        out.clear();
        return false;
    }
    return true;
}

bool Config::load_text(const std::string& text) {
    // Python: json.load читає UTF-8; некоректне кодування або позначка BOM на
    // початку — помилка, і тоді всі параметри за замовчуванням.
    if (!valid_utf8(text)) {
        values_.clear();
        return false;
    }
    return parse_json_object(text, values_);
}

bool Config::load_file(const std::string& path) {
    values_.clear();
    // Прості функції C замість потоків C++: програма на камері менша.
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
        if (text.size() > 1024 * 1024) {  // config.json не буває мегабайтним
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return load_text(text);
}

double Config::get_float(const std::string& key, double fallback) const {
    // as_float: float(value), NaN → за замовчуванням, помилка → за замовчуванням.
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    const JsonValue& v = it->second;
    double r;
    switch (v.type) {
        case JsonValue::Number: r = v.num; break;
        case JsonValue::Bool: r = v.b ? 1.0 : 0.0; break;
        case JsonValue::String:
            if (!python_float_from_string(v.str, r)) return fallback;
            break;
        default: return fallback;  // None, список, словник → TypeError
    }
    return std::isnan(r) ? fallback : r;
}

long long Config::get_int(const std::string& key, long long fallback) const {
    // as_int: int(value); число з крапкою обрізається до цілого.
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    const JsonValue& v = it->second;
    switch (v.type) {
        case JsonValue::Number:
            if (!std::isfinite(v.num)) return fallback;
            // Завеликі числа обмежуємо (у Python вони просто ніколи не збігуться
            // з номером гімбала — тут так само).
            if (v.num > 9e18) return 9000000000000000000LL;
            if (v.num < -9e18) return -9000000000000000000LL;
            return static_cast<long long>(std::trunc(v.num));
        case JsonValue::Bool: return v.b ? 1 : 0;
        case JsonValue::String: {
            long long r;
            return python_int_from_string(v.str, r) ? r : fallback;
        }
        default: return fallback;
    }
}

bool Config::get_bool(const std::string& key, bool fallback) const {
    // as_bool: справжнє true/false; рядок "1/true/yes/on"; інакше bool(value).
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    const JsonValue& v = it->second;
    switch (v.type) {
        case JsonValue::Bool: return v.b;
        case JsonValue::String: {
            const std::string t = strip_lower(v.str);
            return t == "1" || t == "true" || t == "yes" || t == "on";
        }
        case JsonValue::Number: return v.num != 0.0;  // NaN теж True, як у Python
        case JsonValue::Null: return false;
        default: return v.non_empty;
    }
}

std::string Config::get_string(const std::string& key, const std::string& fallback) const {
    // str(cfg.get(key, default)). Для рядка — сам рядок; інше значення
    // підключенням бути не може, тому беремо значення за замовчуванням.
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    return it->second.type == JsonValue::String ? it->second.str : fallback;
}

PluginSettings settings_from_config(const Config& cfg) {
    PluginSettings s;
    s.connect = cfg.get_string("CONNECT", s.connect);
    s.gimbal_id = cfg.get_int("GIMBAL_ID", 1);
    s.draw = cfg.get_bool("DRAW", true);
    s.x = cfg.get_float("X", 0.03);
    s.y = cfg.get_float("Y", 0.04);
    s.dy = cfg.get_float("DY", 0.05);
    s.size = cfg.get_float("SIZE", 0.03);
    s.yaw_widget_x = cfg.get_float("YAW_WIDGET_X", 0.16);
    s.yaw_widget_y = cfg.get_float("YAW_WIDGET_Y", 0.43);
    s.yaw_widget_scale = std::max(0.5, cfg.get_float("YAW_WIDGET_SCALE", 1.0));
    s.request_rate_hz = std::max(0.0, cfg.get_float("REQUEST_RATE_HZ", 0.0));

    s.python_compat = cfg.get_bool("PYTHON_COMPAT", false);
    {
        std::string style = strip_lower(cfg.get_string("YAW_STYLE", "sector"));
        s.yaw_style = (style == "line" || style == "arrow" || style == "arrow_base" ||
                       style == "triangle" || style == "fan")
                          ? style
                          : "sector";  // 0.3.1: немає або невідоме → "sector"
    }
    s.panel = cfg.get_bool("PANEL", true);
    s.panel_alpha = std::min(255LL, std::max(0LL, cfg.get_int("PANEL_ALPHA", 115)));
    s.panel_w = cfg.get_float("PANEL_W", 0.24);
    {
        const std::string pos = strip_lower(cfg.get_string("PANEL_POS", "bottom_right"));
        s.panel_pos = (pos == "bottom_left" || pos == "top_right" || pos == "top_left" ||
                       pos == "xy")
                          ? pos
                          : "bottom_right";  // невідоме значення → правий нижній кут
    }
    if (!(s.panel_w >= 0.05 && s.panel_w <= 1.0)) s.panel_w = 0.24;
    s.plane_image = cfg.get_string("PLANE_IMAGE", "airplane");
    {
        // Лише безпечні символи: назва йде в JSON як є.
        std::string clean;
        for (char ch : s.plane_image)
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')
                clean += ch;
        if (clean.size() > 4 && clean.compare(clean.size() - 4, 4, ".png") == 0)
            clean.erase(clean.size() - 4);  // "airplane.png" → "airplane"
        s.plane_image = clean;
    }
    {
        std::string raw = cfg.get_string("PANEL_IMAGE", "ui-tile"), clean;
        for (char ch : raw)
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')
                clean += ch;
        if (clean.size() > 4 && clean.compare(clean.size() - 4, 4, ".png") == 0)
            clean.erase(clean.size() - 4);
        s.panel_image = clean;
    }
    s.osd_only_changes = cfg.get_bool("OSD_ONLY_CHANGES", true);
    s.osd_resend_s = cfg.get_float("OSD_RESEND_S", 5.0);
    if (!(s.osd_resend_s >= 0.5)) s.osd_resend_s = 5.0;  // захист від 0 і від'ємних
    s.log_interval_s = std::max(0.0, cfg.get_float("LOG_INTERVAL_S", 1.0));
    if (s.python_compat) {
        s.osd_only_changes = false;
        s.log_interval_s = 0.0;
    }
    return s;
}

}  // namespace zr10
