// config.hpp — читання config.json плагіна.
// Поведінка як у plugin.py: файл не знайдено або він пошкоджений → усі
// параметри за замовчуванням; некоректне значення одного параметра → для
// нього значення за замовчуванням. Правила перетворення ті самі, що в
// функціях as_float / as_int / as_bool у plugin.py.
#pragma once

#include <cstdint>
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

// Усі параметри плагіна після читання config.json (0.3.8, 2026-11-11 —
// лишилося тільки те, що справді використовується; інші ключі ігноруються).
struct PluginSettings {
    // CONNECT: MAVLink від польотного контролера — лише для журналу (порівняння
    // кутів контролера з кутами гімбала, «FC link lost»). Порожній — за замовч.
    std::string connect = "udpout:127.0.0.1:14550";
    long long gimbal_id = 1;  // GIMBAL_ID: id гімбала в повідомленні 285
    // CAM (0.3.4): номер камери в R2D2 (як R2D2_shm<N>), на якій малювати;
    // -1 — усі камери явно. NO_CAM (немає ключа) — без поля "cam" (усі камери).
    static constexpr int NO_CAM = -1000;
    int cam = NO_CAM;
    // SIYI_IP (0.3.5): адреса ZR10; кути на плашці — напряму з гімбала
    // (протокол SIYI, порт 37260, лише запит 0x0D). "" — кутів не буде («--»).
    std::string siyi_ip = "192.168.144.25";
};

PluginSettings settings_from_config(const Config& cfg);

}  // namespace zr10
