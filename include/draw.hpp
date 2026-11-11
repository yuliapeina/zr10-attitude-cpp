// draw.hpp — малювання плашки ZR10 на OSD застосунку оператора (0.3.8).
//
// Вигляд (узгоджено 2026-11-11, з 0.3.8 інших варіантів немає):
//   * плашка — картинка ui-tile (чорна, скруглені кути) у правому нижньому
//     куті екрана, ширина 0.24 ширини екрана;
//   * ліворуч — білий літачок (картинка airplane) і синє зафарбоване віяло з
//     його носа: напрямок камери відносно корпусу (0 — вгору, + — вправо);
//   * праворуч — два білі рядки «ZR10 Pitch …» і «ZR10 Yaw …» по центру
//     плашки по висоті;
//   * немає кутів (гімбал не відповідає) — «--» і віяла немає.
//
// Координати — частки екрана 0.0…1.0: x вправо, y вниз.
#pragma once

#include "osd.hpp"

namespace zr10 {

// Кути, які показує плашка (градуси). yaw: 0 — вперед по корпусу, + — вправо.
struct ShownAngles {
    double pitch = 0.0;
    double yaw = 0.0;
};

// Розміщення елементів плашки (усе — частки екрана).
struct PanelLayout {
    double panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    double row_pitch = 0;   // відстань між рядками
    double text_size = 0;   // кегль тексту
    double text_x = 0;      // лівий край тексту
    double text_y = 0;      // верх першого рядка
    double plane_x = 0, plane_y = 0;  // центр літачка
    double nose_x = 0, nose_y = 0;    // ніс літачка — вершина віяла
};
PanelLayout panel_layout();

// Намалювати плашку одним пакетом. angles == nullptr — «--», без віяла.
void draw_panel(Osd& osd, const ShownAngles* angles);

// Прибрати залишки елементів старих версій (рядок статусу, крапка,
// квадратики, літачок лініями, інші стилі віяла) — на старті.
void clear_legacy_elements(Osd& osd);

// Прибрати всі свої елементи (на виході).
void clear_plugin_elements(Osd& osd);

// --- розміри (підібрані на камері 2026-10-28…11-11) ---
constexpr double PANEL_W = 0.24;                  // ширина плашки
constexpr double PANEL_MARGIN_X = 0.01;           // відступ від правого краю
constexpr double PANEL_MARGIN_BOTTOM = PANEL_MARGIN_X * 16.0 / 9.0;  // такий самий на екрані 16:9
constexpr double ROW_PITCH = 0.05 * 0.76;         // відстань між рядками
constexpr double TEXT_SIZE = 0.03 * 0.85;         // кегль
constexpr double PLANE_SCALE = 0.5;               // літачок −50% від plugin.py
constexpr double PLANE_IMAGE_ASPECT = 16.0 / 9.0;
constexpr int SECTOR_LINES = 11;                  // віяло — 11 променів
constexpr double SECTOR_LENGTH = 0.028 * 0.9;     // довжина (частки ширини)
constexpr double SECTOR_HALF_ANGLE_DEG = 22.0;    // півкут віяла
constexpr double SECTOR_LINE_WIDTH = 0.0035;
constexpr std::uint32_t SECTOR_COLOR = Osd::rgba(30, 144, 255);  // синій (#1E90FF)
constexpr std::uint32_t TEXT_COLOR = Osd::rgba(255, 255, 255);   // білий
constexpr double MIN_LINE_DX = 0.0001;  // лінію без горизонтального зміщення R2D2 не малює

}  // namespace zr10
