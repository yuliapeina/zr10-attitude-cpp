// draw.cpp — див. пояснення в draw.hpp.
#include "draw.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include "attitude.hpp"

namespace zr10 {

namespace {

// Порядок малювання в R2D2 — за абеткою id: bg (плашка) → pitch → yaw →
// yaw_plane.image → yaw_sector.NN (віяло поверх літачка).
const char* const PANEL_ID = "zr10_attitude.bg";
const char* const PITCH_ID = "zr10_attitude.pitch";
const char* const YAW_ID = "zr10_attitude.yaw";
const char* const PLANE_ID = "zr10_attitude.yaw_plane.image";

std::string sector_id(int i) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "zr10_attitude.yaw_sector.%02d", i);
    return buf;
}

void clear_sector(Osd& osd) {
    for (int i = 0; i < SECTOR_LINES; ++i) osd.clear(sector_id(i));
}

// Літачок — картинка-квадрат (як R2Draw.square): STYLE_CAM_PROPORTION робить
// ширину = висоті на екрані; центр — (plane_x, plane_y), верх картинки = ніс.
void draw_plane(Osd& osd, const PanelLayout& L) {
    const double side = 0.055 * PLANE_SCALE * 2.0 / PLANE_IMAGE_ASPECT;
    Osd::RectFields f;
    f.image = "airplane";
    f.style = Osd::STYLE_CAM_PROPORTION;
    osd.rect(PLANE_ID, L.plane_x - side / 2.0, L.plane_y - side / 2.0, side, side, f);
}

// Промінь з носа під кутом angle_deg (0 = вгору, + = вправо). Рендерер R2D2
// ставить ПОЧАТОК лінії правильно (x·ширина, y·висота), а кінець зміщує на
// (width·ширина, height·ширина) — тож усі промені починаються в носі, а
// зміщення — в частках ширини: кут і довжина правильні за будь-яких пропорцій.
void draw_ray(Osd& osd, const std::string& id, const PanelLayout& L, double angle_deg) {
    const double a = angle_deg * (PI / 180.0);
    double dx = SECTOR_LENGTH * std::sin(a);
    const double dy = -SECTOR_LENGTH * std::cos(a);  // вгору — від'ємне y
    if (std::fabs(dx) < MIN_LINE_DX) dx = dx < 0.0 ? -MIN_LINE_DX : MIN_LINE_DX;
    osd.line(id, L.nose_x, L.nose_y, L.nose_x + dx, L.nose_y + dy, SECTOR_COLOR, SECTOR_LINE_WIDTH);
}

// Зафарбоване віяло — SECTOR_LINES променів від yaw−22° до yaw+22°.
void draw_sector(Osd& osd, const PanelLayout& L, double yaw_deg) {
    for (int i = 0; i < SECTOR_LINES; ++i) {
        const double t = static_cast<double>(i) / (SECTOR_LINES - 1);
        draw_ray(osd, sector_id(i), L, yaw_deg - SECTOR_HALF_ANGLE_DEG + t * 2.0 * SECTOR_HALF_ANGLE_DEG);
    }
}

void draw_text(Osd& osd, const char* id, const PanelLayout& L, double y, const char* value) {
    osd.text(id, L.text_x, y, value, L.text_size, Osd::ALIGN_LEFT, Osd::rgba(0, 0, 0, 0), std::nullopt,
             0.30, L.row_pitch, 0.01, TEXT_COLOR);
}

}  // namespace

PanelLayout panel_layout() {
    PanelLayout L;
    L.panel_w = PANEL_W;
    L.row_pitch = ROW_PITCH;
    L.text_size = TEXT_SIZE;
    L.panel_h = 3.0 * L.row_pitch + 0.004;  // висота — як була з трьома рядками
    L.panel_x = 1.0 - PANEL_MARGIN_X - L.panel_w;
    L.panel_y = 1.0 - PANEL_MARGIN_BOTTOM - L.panel_h;
    // Два рядки по центру плашки по висоті.
    L.text_y = L.panel_y + 0.002 + L.row_pitch / 2.0;
    L.plane_x = L.panel_x + 0.04;
    // Літачок трохи нижче середини: над носом лишається місце для віяла вгору.
    L.plane_y = L.panel_y + L.panel_h / 2.0 + 0.0295;
    L.nose_x = L.plane_x;
    L.nose_y = L.plane_y - 0.055 * PLANE_SCALE;
    L.text_x = L.plane_x + 0.053;
    return L;
}

void draw_panel(Osd& osd, const ShownAngles* a) {
    const PanelLayout L = panel_layout();
    char pitch[64], yaw[64];
    if (a != nullptr) {
        // «+ 0.0» прибирає від'ємний нуль: інакше було б «Yaw -0.0».
        std::snprintf(pitch, sizeof pitch, "ZR10 Pitch %+.1f deg", a->pitch + 0.0);
        std::snprintf(yaw, sizeof yaw, "ZR10 Yaw %+.1f deg", a->yaw + 0.0);
    } else {
        std::snprintf(pitch, sizeof pitch, "ZR10 Pitch --");
        std::snprintf(yaw, sizeof yaw, "ZR10 Yaw --");
    }

    Osd::Batch batch(osd);
    {
        // ui-tile: ialign 1 — розтягується лише середина, скруглені кути цілі.
        Osd::RectFields f;
        f.image = "ui-tile";
        f.ialign = 1;
        osd.rect(PANEL_ID, L.panel_x, L.panel_y, L.panel_w, L.panel_h, f);
    }
    draw_plane(osd, L);
    if (a != nullptr) draw_sector(osd, L, a->yaw);
    else clear_sector(osd);  // немає кутів — віяла немає (а не «вперед»)
    draw_text(osd, PITCH_ID, L, L.text_y, pitch);
    draw_text(osd, YAW_ID, L, L.text_y + L.row_pitch, yaw);
}

void clear_legacy_elements(Osd& osd) {
    static const char* const legacy[] = {
        "zr10_attitude.status",        "zr10_attitude.live_dot",
        "zr10_attitude.pitch_marker",  "zr10_attitude.yaw_marker",
        "zr10_attitude.status_marker", "zr10_attitude.yaw_direction",
        "zr10_attitude.yaw_plane.fuselage", "zr10_attitude.yaw_plane.wings",
        "zr10_attitude.yaw_plane.tail", "zr10_attitude.yaw_triangle.left",
        "zr10_attitude.yaw_triangle.right", "zr10_attitude.yaw_triangle.base",
    };
    Osd::Batch batch(osd);
    for (const char* id : legacy) osd.clear(id);
}

void clear_plugin_elements(Osd& osd) {
    Osd::Batch batch(osd);
    osd.clear(PANEL_ID);
    osd.clear(PITCH_ID);
    osd.clear(YAW_ID);
    osd.clear(PLANE_ID);
    clear_sector(osd);
}

}  // namespace zr10
