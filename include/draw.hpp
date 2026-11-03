// draw.hpp — малювання кадру плагіна на OSD: тексти, квадратики-статуси,
// літачок і трикутник напрямку камери.
// Переклад функцій draw_sample() і draw_yaw_widget() з plugin.py.
// Елементи, їхні id, порядок, координати, кольори і тексти — ті самі.
#pragma once

#include <cstdint>
#include <string>

#include "osd.hpp"

namespace zr10 {

enum class YawStyle { Line, Fan, Arrow, ArrowBase, Triangle, Sector, Python };

// Де стоїть плашка (PANEL_POS у config.json, рішення 2026-10-28): у кутку
// екрана або, для XY, за параметрами X і Y (як було спочатку).
enum class PanelPos { BottomRight, BottomLeft, TopRight, TopLeft, XY };

// Параметри розташування з config.json (ті самі ключі й значення за
// замовчуванням, що в plugin.py).
struct DrawSettings {
    double x = 0.03;               // X
    double y = 0.04;               // Y
    double dy = 0.05;              // DY
    double size = 0.03;            // SIZE
    double yaw_widget_x = 0.16;    // YAW_WIDGET_X
    double yaw_widget_y = 0.43;    // YAW_WIDGET_Y
    double yaw_widget_scale = 1.0; // YAW_WIDGET_SCALE (не менше 0.5)

    // Вигляд індикатора напрямку (YAW_STYLE у config.json, рішення 2026-10-23):
    //   Line   — одна лінія напрямку з носа літачка ("line", за замовчуванням);
    //   Fan    — «віяло»: два бокові промені yaw ± 22° з носа ("fan");
    //   Arrow  — лише стрілка, без літачка ("arrow", 2026-10-29): вістря
    //            нерухоме в центрі області ліворуч від тексту, стовбур і два
    //            вуса однакової довжини виходять з вістря назад. Стрілка
    //            обертається навколо вістря.
    //   ArrowBase — та сама стрілка, але нерухома ОСНОВА стовбура в центрі
    //            області, а рухається вістря ("arrow_base", 2026-10-29).
    //            Вістря рахується один раз для екрана 16:9, і з нього
    //            починаються всі три лінії (стовбур теж, назад до основи) —
    //            стрілка завжди ціла; на інших пропорціях трохи зміщується
    //            лише основа.
    //   Triangle — замкнений трикутник ("triangle", 2026-10-29): як віяло —
    //            нерухома вершина в центрі області, дві сторони під yaw ± 22° —
    //            плюс третя сторона між їхніми кінцями. Вершини рахуються один
    //            раз (для 16:9) і зберігаються в змінних. Нерухома вершина
    //            завжди точна; на пропорціях, відмінних від 16:9, можливі малі
    //            розриви в двох інших.
    //   Sector — зафарбоване віяло ("sector", 2026-10-29): білий літачок і
    //            SECTOR_LINES ліній однакової довжини з його носа (нерухома
    //            вершина), рівномірно від yaw − 22° до yaw + 22°. Усі лінії
    //            починаються в одній точці — форма правильна за будь-яких пропорцій.
    //   Python — як у plugin.py: замкнений трикутник і літачок лініями
    //            (лише PYTHON_COMPAT; на справжньому R2D2 малюється спотворено).
    // У Line і Fan літачок — прямокутники sline, а всі лінії починаються в
    // носі: однаково правильно в будь-якому режимі екрана R2D2.
    YawStyle yaw_style = YawStyle::Fan;  // 2026-10-29: віяло за замовчуванням

    // Плашка (рішення 2026-10-28): напівпрозорий прямокутник під текстом і
    // літачком (у Line і Fan). Літачок — ліворуч, текст — праворуч.
    // У Line і Fan розташування задають X і Y (лівий верхній кут першого рядка
    // тексту, як у plugin.py); YAW_WIDGET_X/Y використовуються лише в Python.
    bool panel = true;          // PANEL
    int panel_alpha = 115;      // PANEL_ALPHA: 0 — прозора, 255 — суцільна чорна
    double panel_w = 0.24;      // PANEL_W: ширина плашки, частки ширини екрана
    PanelPos panel_pos = PanelPos::BottomRight;  // PANEL_POS (2026-10-28)
    // PANEL_IMAGE (2026-11-03): картинка плашки; "" — прямокутник з PANEL_ALPHA.
    std::string panel_image = "ui-tile";
    // PLANE_IMAGE (2026-10-29): картинка літачка з ресурсів застосунку
    // оператора замість ліній; порожньо — лінії.
    std::string plane_image = "airplane";
};

// Розташування елементів на плашці (Line і Fan).
struct PanelLayout {
    double panel_x, panel_y, panel_w, panel_h;  // плашка
    double text_y;                               // верх першого рядка тексту
    double plane_x, plane_y;                     // центр літачка
    double nose_x, nose_y;                       // ніс — звідси виходять лінії
    double marks_x;                              // квадратики статусу (текст правіше)
    double icon_x, icon_y;                       // центр області ліворуч від тексту
    double row_pitch;                            // крок рядків тексту
    double text_size;                            // розмір тексту (однаковий для 3 рядків)
};
PanelLayout panel_layout(const DrawSettings& s);

// Розміри (рішення 2026-10-28, макет «ліворуч, на плашці»):
constexpr double PANEL_PLANE_SCALE = 0.5;      // літачок −50% від plugin.py
// Для картинки літачка: ширина квадрата в частках ширини = висота / (16/9).
constexpr double PLANE_IMAGE_ASPECT = 16.0 / 9.0;
// Товщина літачка (2026-10-28, тест osd_widthtest на камері): товщину ліній
// (крила, хвіст) R2D2 ігнорує й малює своєю, а фюзеляж-прямокутник шириною
// 0.003 виходить такої самої товщини — і видимий у режимі «дві камери»
// (0.002 там зникав). Тож літачок однаковий, як лінія напрямку.
constexpr double PLANE_BODY_THICKNESS = 0.003;
constexpr double PLANE_WINGS_THICKNESS = 0.001;  // R2D2 ігнорує; лишається в JSON
// 2026-10-28 (після камери): усі рядки розміром як нижній (SIZE × 0.85), а
// проміжок між рядками −50%: крок 0.0255 + (0.05 − 0.0255) / 2 ≈ 0.038 = DY × 0.76.
constexpr double PANEL_TEXT_SIZE_FACTOR = 0.85;
// Відступи плашки від країв екрана, коли вона в кутку (PANEL_POS). Знизу
// більше: там R2D2 показує власний рядок стану.
constexpr double PANEL_MARGIN_X = 0.01;
constexpr double PANEL_MARGIN_TOP = 0.038;
// 2026-11-03: знизу такий самий відступ, як справа (0.01 ширини = 0.01·16/9 висоти).
constexpr double PANEL_MARGIN_BOTTOM = PANEL_MARGIN_X * 16.0 / 9.0;
constexpr double PANEL_ROW_PITCH_FACTOR = 0.76;

// Довжина ліній напрямку — у частках ШИРИНИ екрана (рендерер R2D2 рахує
// зміщення кінця лінії по обох осях від ширини).
// 2026-10-28: −50% і ще −30% (0.08 → 0.028), товщина −50%.
constexpr double FAN_LENGTH = 0.028;
constexpr double FAN_WIDTH = 0.003;
constexpr double FAN_HALF_ANGLE_DEG = 22.0;  // як у plugin.py
constexpr double DIRECTION_LENGTH = 0.028;
constexpr double DIRECTION_WIDTH = 0.0035;
// Стрілка (YAW_STYLE "arrow"): довжина стовбура, вусів (удвічі коротші) —
// частки ширини — і кут вусів від стовбура. На екрані 16:9 стрілка вміщується
// в коло радіусом ARROW_LENGTH навколо центру області (по висоті ≈ 0.044).
constexpr double ARROW_LENGTH = 0.025;
constexpr double ARROW_HEAD_LENGTH = ARROW_LENGTH * 0.5;  // вуса на 50% коротші (2026-10-29)
constexpr double ARROW_HEAD_ANGLE_DEG = 35.0;
constexpr double ARROW_ASSUMED_ASPECT = 16.0 / 9.0;  // для ArrowBase і Triangle
// Трикутник (YAW_STYLE "triangle"): довжина бічних сторін (частки ширини) і
// половина кута між ними. Нерухома вершина — центр області.
constexpr double TRI_LENGTH = 0.028;
constexpr double TRI_HALF_ANGLE_DEG = 22.0;  // як у віяла й plugin.py
// Зафарбоване віяло (YAW_STYLE "sector"): кількість ліній (разом із двома
// крайніми) і їхня довжина. 11 ліній по 4.4° — на повному екрані сусідні
// кінці ~4 px один від одного, лінії R2D2 товщиною 3–4 px майже зливаються.
constexpr int SECTOR_LINES = 11;
constexpr double SECTOR_LENGTH = 0.028 * 0.9;  // 2026-11-03: −10%
// Лінію з рівно нульовим горизонтальним зміщенням рендерер не малює
// (перевірено osd_linetest 2026-10-23), тож менше за це значення не буває.
constexpr double MIN_LINE_DX = 0.0001;

// Останні дані гімбала (як словник sample у plugin.py).
struct GimbalSample {
    int id = 0;
    int flags = 0;
    int failure = 0;
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;  // кут, на який повертати трикутник і показувати в тексті
};

// Намалювати кадр. sample == nullptr означає «ще не було даних» (WAIT MAV).
// stale = дані застаріли (NO DATA). Усе йде одним пакетом (batch).
void draw_sample(Osd& osd, const GimbalSample* sample, bool stale,
                 const DrawSettings& settings);

// Прибрати елементи, які вибраний вигляд не малює (наприклад, промені віяла,
// коли вибрано одну лінію), — якщо лишилися з попереднього запуску.
void clear_unused_elements(Osd& osd, const DrawSettings& settings);

// Прибрати всі елементи плагіна з екрана (як блок finally у plugin.py).
// Для Python — рівно той самий список, що в plugin.py.
void clear_plugin_elements(Osd& osd, YawStyle style = YawStyle::Python);

}  // namespace zr10
