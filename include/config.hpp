// config.hpp — читання config.json плагіна.
// Поведінка як у plugin.py: файл не знайдено або він пошкоджений → усі
// параметри за замовчуванням; некоректне значення одного параметра → для
// нього значення за замовчуванням. Правила перетворення ті самі, що в
// функціях as_float / as_int / as_bool у plugin.py.
#pragma once

#include <map>
#include <string>

namespace zr10 {

// Значення з JSON (лише те, що потрібно для перетворень як у Python).
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    bool num_is_int = false;  // число записане без крапки й експоненти
    std::string str;          // для String
    bool non_empty = false;   // для Array/Object: чи є елементи (bool() у Python)
};

// Розібрати текст JSON. true — коректний JSON-об'єкт (тоді out містить його
// ключі верхнього рівня); false — некоректний або не об'єкт.
bool parse_json_object(const std::string& text, std::map<std::string, JsonValue>& out);

class Config {
public:
    // Прочитати файл. Повертає false, якщо файлу немає або він некоректний
    // (тоді всі значення будуть за замовчуванням, як у Python).
    bool load_file(const std::string& path);
    bool load_text(const std::string& text);

    // Як as_float / as_int / as_bool / str(cfg.get(...)) у plugin.py.
    double get_float(const std::string& key, double fallback) const;
    long long get_int(const std::string& key, long long fallback) const;
    bool get_bool(const std::string& key, bool fallback) const;
    std::string get_string(const std::string& key, const std::string& fallback) const;

    bool has(const std::string& key) const { return values_.count(key) != 0; }

private:
    std::map<std::string, JsonValue> values_;
};

// Усі параметри плагіна після читання config.json.
struct PluginSettings {
    // Ті самі параметри, що в plugin.py:
    std::string connect = "udpout:127.0.0.1:14550";  // CONNECT
    long long gimbal_id = 1;                          // GIMBAL_ID
    bool draw = true;                                 // DRAW
    double x = 0.03, y = 0.04, dy = 0.05, size = 0.03;  // X, Y, DY, SIZE
    double yaw_widget_x = 0.16, yaw_widget_y = 0.43;  // YAW_WIDGET_X/Y
    double yaw_widget_scale = 1.0;                    // YAW_WIDGET_SCALE (≥ 0.5)
    double request_rate_hz = 0.0;                     // REQUEST_RATE_HZ (≥ 0)

    // Нові параметри C++ версії (рішення 2026-10-22):
    bool osd_only_changes = true;   // OSD_ONLY_CHANGES: false = як у Python
    double osd_resend_s = 5.0;      // OSD_RESEND_S: повтор незмінних елементів
    double log_interval_s = 1.0;    // LOG_INTERVAL_S: 0 = рядок на кожне 285
    // Лише для перевірки: поводитися точно як Python-версія (старий yaw без
    // виправлення -90°, без округлення, без оптимізації OSD, лог на кожне 285).
    bool python_compat = false;     // PYTHON_COMPAT
    // YAW_STYLE: "fan" — віяло (за замовч., 2026-10-29), "line" — одна лінія,
    // "arrow" — стрілка, що обертається навколо вістря, "arrow_base" — стрілка
    // з нерухомою основою (вістря рухається), "triangle" — замкнений трикутник,
    // "sector" — зафарбоване віяло з ліній.
    std::string yaw_style = "fan";
    // Плашка під текстом і літачком (2026-10-28):
    bool panel = true;          // PANEL
    long long panel_alpha = 115;  // PANEL_ALPHA 0…255 (115 ≈ 45% непрозорості)
    double panel_w = 0.24;      // PANEL_W, частки ширини екрана (підібрано на камері)
    // PANEL_POS: "bottom_right" (за замовч.), "bottom_left", "top_right",
    // "top_left" або "xy" — тоді місце задають X і Y.
    std::string panel_pos = "bottom_right";
    // PLANE_IMAGE (2026-10-29): назва картинки літачка з ресурсів застосунку
    // оператора (без .png); "" — малювати літачок лініями, як раніше.
    std::string plane_image = "airplane";
    // PANEL_IMAGE (2026-11-03): картинка плашки (ui-tile — чорна зі скругленими
    // кутами); "" — прямокутник з прозорістю PANEL_ALPHA.
    std::string panel_image = "ui-tile";
};

PluginSettings settings_from_config(const Config& cfg);

}  // namespace zr10
