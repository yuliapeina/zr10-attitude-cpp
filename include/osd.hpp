// osd.hpp — клієнт OSD R2D2: малювання поверх відео.
// Переклад api/r2draw.py. Надсилає JSON по UDP на 127.0.0.1:14777.
// Формат повідомлень — той самий, що в r2draw.py (див. опис протоколу там):
//   {"rects": [ {"id": ..., "position": {"x":..,"y":..,"width":..,"height":..}, ...} ]}
// Уся геометрія — частки екрана 0.0…1.0.
//
// Помилки мережі не зупиняють програму: повідомлення просто не дійде,
// а лічильник send_errors() збільшиться.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zr10 {

class Osd {
public:
    // Вирівнювання тексту (ті самі значення, що ALIGN_* у r2draw.py).
    static constexpr int ALIGN_CENTER = 0x00000000;
    static constexpr int ALIGN_LEFT = 0x00000001;
    static constexpr int ALIGN_RIGHT = 0x00000002;
    static constexpr int ALIGN_TOP = 0x00000004;
    static constexpr int ALIGN_BOTTOM = 0x00000008;

    // Стилі (STYLE_* у r2draw.py).
    static constexpr int STYLE_CAM_PROPORTION = 1;
    static constexpr int STYLE_LINE = 2;
    static constexpr int STYLE_ROTATE_IMAGE = 4;
    static constexpr int STYLE_CENTER_ORIGIN = 8;

    // Колір r,g,b,a (0–255) → одне число 0xRRGGBBAA, як R2Draw.rgba().
    static constexpr std::uint32_t rgba(int r, int g, int b, int a = 255) {
        return (static_cast<std::uint32_t>(r & 0xFF) << 24) |
               (static_cast<std::uint32_t>(g & 0xFF) << 16) |
               (static_cast<std::uint32_t>(b & 0xFF) << 8) |
               static_cast<std::uint32_t>(a & 0xFF);
    }

    // Необов'язкові поля прямокутника. Поле без значення в JSON не потрапляє
    // (як None у Python).
    struct RectFields {
        std::optional<std::uint32_t> border;
        std::optional<double> border_width;  // у JSON це ключ "width"
        std::optional<std::uint32_t> fill;
        std::optional<std::string> text;
        std::optional<double> size;
        std::optional<int> align;
        // Колір тексту (поле "color" нового r2draw.py, 2026-10-29). Старий
        // застосунок оператора його ігнорує — текст лишається білим.
        std::optional<std::uint32_t> color;
        // Обведення тексту (поле "outline", перевірено osd_featuretest
        // 2026-10-29): колір контуру навколо літер.
        std::optional<std::uint32_t> outline;
        std::optional<std::string> image;
        std::optional<int> style;
        // Як розтягувати картинку (поле "ialign" нового r2draw.py): 1 — кінці
        // зберігають пропорції, тягнеться лише середина (скруглені кути цілі).
        std::optional<int> ialign;
    };

    // Звичайний режим: UDP на ip:port.
    explicit Osd(const char* ip = "127.0.0.1", int port = 14777);

    // Режим для тестів: замість мережі кожне готове повідомлення передається
    // у функцію sink.
    explicit Osd(std::function<void(const std::string&)> sink);

    ~Osd();
    Osd(const Osd&) = delete;
    Osd& operator=(const Osd&) = delete;

    void close();

    // ---- оптимізація: надсилати лише змінене (ВІДМІННІСТЬ ВІД PYTHON) ----
    // Увімкнено: елемент, ідентичний уже надісланому (той самий id і JSON),
    // не надсилається повторно. Але не рідше ніж раз на resend_s секунд
    // елемент надсилається знову — на випадок, якщо R2D2 перезапустився або
    // загубив пакет. Пакет, у якому нічого не змінилось, не надсилається зовсім.
    // Вимкнено (за замовчуванням у класі) — усе як у r2draw.py.
    void set_only_changes(bool enabled, double resend_s = 5.0);

    // 0.3.4: камера, на якій малювати (номер як у R2D2_shm<N>; -1 — усі).
    // Додається як "cam" поруч із "rects" у КОЖНОМУ повідомленні, як
    // R2Draw(cam=N) у r2draw.py. NO_CAM — поля немає (малюється на всіх
    // камерах, як до 0.3.4).
    static constexpr int NO_CAM = -1000;
    void set_cam(int cam) { cam_ = cam; }
    int cam() const { return cam_; }

    // 0.3.4: спитати модуль OSD {"ping":true} і повернути відповідь (pong з
    // переліком камер "cams") або "" за timeout_s. Лише для журналу.
    std::string ping(double timeout_s);
    bool only_changes() const { return only_changes_; }

    // Джерело часу в секундах (для тестів). За замовчуванням — монотонний годинник.
    void set_clock(std::function<double()> clock) { clock_ = std::move(clock); }

    unsigned long skipped_count() const { return skipped_; }

    // ---- пакети (batch) ----
    // Між begin_batch() і end_batch() елементи збираються і йдуть одним
    // UDP-повідомленням. Вкладені пакети дозволені: відправка відбувається
    // лише при закритті зовнішнього.
    void begin_batch();
    void end_batch();
    void flush();

    // Зручний спосіб: Osd::Batch b(osd); … — пакет закривається сам.
    class Batch {
    public:
        explicit Batch(Osd& osd) : osd_(osd) { osd_.begin_batch(); }
        ~Batch() { osd_.end_batch(); }
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
    private:
        Osd& osd_;
    };

    // ---- примітиви малювання (як у r2draw.py) ----
    void rect(const std::string& id, double x, double y, double width,
              double height, const RectFields& fields = {});

    // Текст у точці (x, y). Колір тексту — border. fill за замовчуванням
    // відсутній. Параметри за замовчуванням ті самі, що в R2Draw.text().
    void text(const std::string& id, double x, double y, const std::string& text,
              double size = 0.04, int align = ALIGN_CENTER,
              std::optional<std::uint32_t> border = 0xFFFFFFFFu,
              std::optional<std::uint32_t> fill = std::nullopt,
              double width = 0.2, double height = 0.08,
              double border_width = 0.01,
              std::optional<std::uint32_t> color = std::nullopt,
              std::optional<std::uint32_t> outline = std::nullopt);

    // Лінія від (x1, y1) до (x2, y2) (STYLE_LINE). extra_style додається до
    // STYLE_LINE (для діагностики, напр. STYLE_CAM_PROPORTION).
    void line(const std::string& id, double x1, double y1, double x2, double y2,
              std::uint32_t fill = 0xFFFFFFFFu, double width = 0.005, int extra_style = 0);

    // Горизонтальна або вертикальна лінія як тонкий заповнений прямокутник
    // (як R2Draw.sline). Рендерер R2D2 не малює STYLE_LINE з нульовою шириною
    // (вертикальні лінії), а прямокутники — малює.
    void sline(const std::string& id, double x1, double y1, double x2, double y2,
               std::uint32_t color = 0xFFFFFFFFu, double thickness = 0.004);

    // Картинка з ресурсів застосунку.
    void image(const std::string& id, double x, double y, double width,
               double height, const std::string& image,
               std::optional<int> style = std::nullopt);

    // Прибрати один свій елемент.
    void clear(const std::string& id);
    // Прибрати ВСЕ на екрані (зокрема чуже) — як osd.clear() без id.
    void clear_all();

    unsigned long sent_count() const { return sent_; }
    unsigned long send_errors() const { return errors_; }

    // Перетворення числа у JSON так само, як Python json.dumps (repr float).
    // Відкрито для тестів.
    static void append_number(std::string& out, double value);
    static void append_string(std::string& out, const std::string& value);

private:
    void emit(const std::string& id, std::string element_json, bool is_clear);
    double now() const;
    void send(const std::string& payload);

    int sock_ = -1;
    int cam_ = NO_CAM;
    std::string rects_prefix() const;
    std::function<void(const std::string&)> sink_;
    unsigned char addr_[16] = {};  // sockaddr_in, щоб не тягнути заголовки сюди
    int batch_depth_ = 0;
    std::vector<std::string> pending_;
    unsigned long sent_ = 0;
    unsigned long errors_ = 0;

    struct SentElement {
        std::string json;
        double at = 0.0;
    };
    bool only_changes_ = false;
    double resend_s_ = 5.0;
    std::unordered_map<std::string, SentElement> last_sent_;
    std::function<double()> clock_;
    unsigned long skipped_ = 0;
};

}  // namespace zr10
