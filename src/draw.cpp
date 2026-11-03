// draw.cpp — див. пояснення в draw.hpp.
#include "draw.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include "attitude.hpp"
#include "overlay.hpp"

namespace zr10 {

namespace {

// Ті самі id, що OSD_IDS, MARK_IDS і YAW_WIDGET_IDS у plugin.py.
const char* const OSD_IDS[3] = {
    "zr10_attitude.pitch",
    "zr10_attitude.yaw",
    "zr10_attitude.status",
};
const char* const MARK_IDS[3] = {
    "zr10_attitude.pitch_marker",
    "zr10_attitude.yaw_marker",
    "zr10_attitude.status_marker",
};
const char* const YAW_WIDGET_IDS[6] = {
    "zr10_attitude.yaw_plane.fuselage",
    "zr10_attitude.yaw_plane.wings",
    "zr10_attitude.yaw_plane.tail",
    "zr10_attitude.yaw_triangle.left",
    "zr10_attitude.yaw_triangle.base",
    "zr10_attitude.yaw_triangle.right",
};

// Нова лінія напрямку (YAW_STYLE "line"); у plugin.py її немає.
const char* const DIRECTION_ID = "zr10_attitude.yaw_direction";

// Кольори статусу (ті самі, що були в квадратиків).
constexpr std::uint32_t STATUS_OK = Osd::rgba(0, 220, 0);       // зелений
constexpr std::uint32_t STATUS_WARN = Osd::rgba(255, 200, 0);   // помаранчевий
constexpr std::uint32_t STATUS_FAIL = Osd::rgba(230, 40, 40);   // червоний

constexpr double MARK_SIZE = 0.012;
constexpr double MARK_GAP = 0.008;

const char* const PANEL_ID = "zr10_attitude.bg";
// Картинка літачка (PLANE_IMAGE). За абеткою перед yaw_sector — віяло зверху.
const char* const PLANE_IMAGE_ID = "zr10_attitude.yaw_plane.image";

// Лінії зафарбованого віяла (YAW_STYLE "sector").
std::string sector_id(int i) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "zr10_attitude.yaw_sector.%02d", i);
    return buf;
}
void clear_sector(Osd& osd) {
    for (int i = 0; i < SECTOR_LINES; ++i) osd.clear(sector_id(i));
}  // «bg» — за абеткою перед текстом

// Літачок. Фюзеляж (вертикальний) — прямокутник sline: вертикальну лінію
// R2D2 не малює. Крила й хвіст (горизонтальні) — лінії: прямокутник висотою
// 1–2 пікселі R2D2 округлює по-різному (на камері хвіст виходив товщим за
// крила, 2026-10-28), а в лінії товщина рахується від ширини екрана — так
// само, як ширина фюзеляжу-sline. Тож усі три частини однакової товщини.
void draw_plane_slines(Osd& osd, const PanelLayout& L, const DrawSettings& s) {
    if (!s.plane_image.empty()) {
        // Картинка-квадрат (як R2Draw.square): висота 0.055 висоти екрана, як
        // фюзеляж літачка з ліній, центр там само — верх картинки = ніс.
        // STYLE_CAM_PROPORTION робить ширину = висоті на екрані; застосунок
        // масштабує висоту відносно центру, тож передаємо лівий верхній кут
        // квадрата w = h без CENTER_ORIGIN.
        const double side = 0.055 * PANEL_PLANE_SCALE * 2.0 * s.yaw_widget_scale / PLANE_IMAGE_ASPECT;
        Osd::RectFields f;
        f.image = s.plane_image;
        f.style = Osd::STYLE_CAM_PROPORTION;
        osd.rect(PLANE_IMAGE_ID, L.plane_x - side / 2.0, L.plane_y - side / 2.0, side, side, f);
        return;
    }
    const std::uint32_t plane_color = Osd::rgba(255, 255, 255);  // білий (2026-10-29)
    Segment plane[3];
    plane_segments(L.plane_x, L.plane_y, PANEL_PLANE_SCALE * s.yaw_widget_scale, plane);
    osd.sline(YAW_WIDGET_IDS[0], plane[0].start.x, plane[0].start.y, plane[0].end.x,
              plane[0].end.y, plane_color, PLANE_BODY_THICKNESS);
    for (int i = 1; i < 3; ++i) {
        osd.line(YAW_WIDGET_IDS[i], plane[i].start.x, plane[i].start.y, plane[i].end.x,
                 plane[i].end.y, plane_color, PLANE_WINGS_THICKNESS);
    }
}

// Промінь з носа літачка під кутом angle_deg (0 = вгору, + = вправо).
// Рендерер R2D2 ставить ПОЧАТОК лінії правильно (x·ширина, y·висота), а кінець
// зміщує на (width·ширина, height·ширина). Тому всі промені починаються в
// носі, а зміщення задаються в частках ширини: кут і довжина правильні за
// будь-яких пропорцій екрана (повний екран, дві камери, інший пристрій).
void draw_ray(Osd& osd, const char* id, double angle_deg, double length, double width,
              std::uint32_t color, const PanelLayout& L, const DrawSettings& s) {
    const double a = angle_deg * (PI / 180.0);
    double dx = length * s.yaw_widget_scale * std::sin(a);
    const double dy = -length * s.yaw_widget_scale * std::cos(a);  // вгору — від'ємне y
    // Лінію з нульовим горизонтальним зміщенням R2D2 не малює.
    if (std::fabs(dx) < MIN_LINE_DX) dx = dx < 0.0 ? -MIN_LINE_DX : MIN_LINE_DX;
    osd.line(id, L.nose_x, L.nose_y, L.nose_x + dx, L.nose_y + dy, color, width);
}

// Лінія з точки (ox, oy) під кутом angle_deg (0 = вгору), довжина в частках
// ширини — так само, як draw_ray, але з довільної точки.
void draw_ray_from(Osd& osd, const char* id, double ox, double oy, double angle_deg,
                   double length, double width, std::uint32_t color, const DrawSettings& s) {
    const double a = angle_deg * (PI / 180.0);
    double dx = length * s.yaw_widget_scale * std::sin(a);
    const double dy = -length * s.yaw_widget_scale * std::cos(a);
    if (std::fabs(dx) < MIN_LINE_DX) dx = dx < 0.0 ? -MIN_LINE_DX : MIN_LINE_DX;
    osd.line(id, ox, oy, ox + dx, oy + dy, color, width);
}

void draw_panel(Osd& osd, const PanelLayout& L, const DrawSettings& s) {
    if (!s.panel_image.empty()) {
        // 2026-11-03: картинка ui-tile — чорна, зі скругленими кутами (з
        // ресурсів застосунку оператора). ialign 1: розтягується лише середина,
        // кути не спотворюються. Непрозора: прозорість картинок застосунок
        // малює неправильно (osd_paneltest).
        Osd::RectFields f;
        f.image = s.panel_image;
        f.ialign = 1;
        osd.rect(PANEL_ID, L.panel_x, L.panel_y, L.panel_w, L.panel_h, f);
        return;
    }
    Osd::RectFields f;
    f.fill = Osd::rgba(0, 0, 0, s.panel_alpha);
    f.border = Osd::rgba(0, 0, 0, 0);  // без рамки
    f.border_width = 0.001;            // рендерер малює елемент лише з width > 0
    osd.rect(PANEL_ID, L.panel_x, L.panel_y, L.panel_w, L.panel_h, f);
}

// «Віяло»: два бокові промені yaw ± 22° (YAW_STYLE "fan").
void draw_yaw_fan(Osd& osd, double yaw_deg, std::uint32_t color, const PanelLayout& L,
                  const DrawSettings& s) {
    draw_plane_slines(osd, L, s);
    draw_ray(osd, YAW_WIDGET_IDS[3], yaw_deg - FAN_HALF_ANGLE_DEG, FAN_LENGTH, FAN_WIDTH, color, L, s);
    draw_ray(osd, YAW_WIDGET_IDS[5], yaw_deg + FAN_HALF_ANGLE_DEG, FAN_LENGTH, FAN_WIDTH, color, L, s);
}

// Стрілка «↑» (YAW_STYLE "arrow"). Усі три лінії починаються у вістрі — єдиній
// точці, яку R2D2 ставить правильно, — і йдуть назад: стовбур під кутом
// yaw + 180°, вуса — yaw + 180° ± 35°. Тож форма стрілки правильна в будь-якому
// режимі екрана, а обертається вона навколо вістря.
void draw_yaw_arrow(Osd& osd, double yaw_deg, std::uint32_t color, const PanelLayout& L,
                    const DrawSettings& s) {
    // Без літачка (2026-10-29): вістря — у центрі області ліворуч від тексту.
    const double tip_x = L.icon_x;
    const double tip_y = L.icon_y;
    const double back = yaw_deg + 180.0;
    draw_ray_from(osd, DIRECTION_ID, tip_x, tip_y, back, ARROW_LENGTH, DIRECTION_WIDTH, color, s);
    draw_ray_from(osd, YAW_WIDGET_IDS[3], tip_x, tip_y, back - ARROW_HEAD_ANGLE_DEG,
                  ARROW_HEAD_LENGTH, DIRECTION_WIDTH, color, s);
    draw_ray_from(osd, YAW_WIDGET_IDS[5], tip_x, tip_y, back + ARROW_HEAD_ANGLE_DEG,
                  ARROW_HEAD_LENGTH, DIRECTION_WIDTH, color, s);
}

// Стрілка з нерухомою основою (YAW_STYLE "arrow_base").
// Положення вістря рахуємо один раз (змінні tip_x, tip_y) — так, щоб основа
// стовбура на екрані 16:9 була в центрі області. Далі ВСІ три лінії
// починаються в цій точці: стовбур іде з вістря назад до основи, вуса — з
// вістря назад ± 35°. R2D2 точно ставить лише початок лінії, тож вістря і
// вуса завжди зійдуться (2026-10-29, ідея: спільна вихідна точка). Якщо
// пропорції кадру не рівно 16:9, трохи зміститься лише основа стовбура.
void draw_yaw_arrow_base(Osd& osd, double yaw_deg, std::uint32_t color, const PanelLayout& L,
                         const DrawSettings& s) {
    const double a = yaw_deg * (PI / 180.0);
    const double len = ARROW_LENGTH * s.yaw_widget_scale;
    const double tip_x = L.icon_x + len * std::sin(a);
    const double tip_y = L.icon_y - len * std::cos(a) * ARROW_ASSUMED_ASPECT;
    const double back = yaw_deg + 180.0;
    draw_ray_from(osd, DIRECTION_ID, tip_x, tip_y, back, ARROW_LENGTH, DIRECTION_WIDTH, color, s);
    draw_ray_from(osd, YAW_WIDGET_IDS[3], tip_x, tip_y, back - ARROW_HEAD_ANGLE_DEG,
                  ARROW_HEAD_LENGTH, DIRECTION_WIDTH, color, s);
    draw_ray_from(osd, YAW_WIDGET_IDS[5], tip_x, tip_y, back + ARROW_HEAD_ANGLE_DEG,
                  ARROW_HEAD_LENGTH, DIRECTION_WIDTH, color, s);
}

// Лінія з точки (x, y) зі зміщенням (du, dv) у частках ШИРИНИ — саме так R2D2
// рахує кінець лінії (x + du·ширина, y + dv·ширина).
void line_from(Osd& osd, const char* id, double x, double y, double du, double dv,
               std::uint32_t color) {
    if (std::fabs(du) < MIN_LINE_DX) du = du < 0.0 ? -MIN_LINE_DX : MIN_LINE_DX;
    osd.line(id, x, y, x + du, y + dv, color, DIRECTION_WIDTH);
}

// Трикутник (YAW_STYLE "triangle"): віяло + третя сторона. Три вершини —
// три змінні: нерухома P (центр області) і кінці сторін A, B під yaw ∓ 22°
// (u, v у частках ширини; на екрані x = P.x + u, y = P.y + v·16/9). Обидві
// бічні сторони виходять з P — вона завжди точна; третя сторона — з A до B.
void draw_yaw_triangle(Osd& osd, double yaw_deg, std::uint32_t color, const PanelLayout& L,
                       const DrawSettings& s) {
    struct V { double u, v; };
    auto dir = [&](double deg) {
        const double a = deg * (PI / 180.0);
        const double len = TRI_LENGTH * s.yaw_widget_scale;
        return V{len * std::sin(a), -len * std::cos(a)};  // 0 = вгору, + = вправо
    };
    const double px = L.icon_x, py = L.icon_y;  // нерухома вершина
    const V a = dir(yaw_deg - TRI_HALF_ANGLE_DEG);
    const V b = dir(yaw_deg + TRI_HALF_ANGLE_DEG);
    const double ax = px + a.u, ay = py + a.v * ARROW_ASSUMED_ASPECT;  // вершина A на екрані
    line_from(osd, YAW_WIDGET_IDS[3], px, py, a.u, a.v, color);
    line_from(osd, YAW_WIDGET_IDS[5], px, py, b.u, b.v, color);
    line_from(osd, YAW_WIDGET_IDS[4], ax, ay, b.u - a.u, b.v - a.v, color);
}

// Зафарбоване віяло (YAW_STYLE "sector"): білий літачок і SECTOR_LINES ліній
// однакової довжини з носа літачка (нерухома вершина), від yaw − 22° до
// yaw + 22° (2026-10-29: літачок повернуто).
void draw_yaw_sector(Osd& osd, double yaw_deg, std::uint32_t color, const PanelLayout& L,
                     const DrawSettings& s) {
    draw_plane_slines(osd, L, s);
    for (int i = 0; i < SECTOR_LINES; ++i) {
        const double t = SECTOR_LINES > 1 ? static_cast<double>(i) / (SECTOR_LINES - 1) : 0.5;
        const double angle = yaw_deg - TRI_HALF_ANGLE_DEG + t * 2.0 * TRI_HALF_ANGLE_DEG;
        draw_ray_from(osd, sector_id(i).c_str(), L.nose_x, L.nose_y, angle, SECTOR_LENGTH,
                      DIRECTION_WIDTH, color, s);
    }
}

// Одна лінія напрямку (YAW_STYLE "line").
void draw_yaw_direction(Osd& osd, double yaw_deg, std::uint32_t color, const PanelLayout& L,
                        const DrawSettings& s) {
    draw_plane_slines(osd, L, s);
    draw_ray(osd, DIRECTION_ID, yaw_deg, DIRECTION_LENGTH, DIRECTION_WIDTH, color, L, s);
}

void draw_yaw_widget(Osd& osd, double yaw_deg, std::uint32_t color,
                     const DrawSettings& s) {
    const std::uint32_t plane_color = Osd::rgba(235, 235, 235);
    Segment plane[3];
    plane_segments(s.yaw_widget_x, s.yaw_widget_y, s.yaw_widget_scale, plane);

    const double nose_x = s.yaw_widget_x;
    const double nose_y = s.yaw_widget_y - 0.055 * s.yaw_widget_scale;
    Point tri[3];
    yaw_triangle_points(nose_x, nose_y, yaw_deg, tri, 0.14 * s.yaw_widget_scale, 22.0);

    for (int i = 0; i < 3; ++i) {
        osd.line(YAW_WIDGET_IDS[i], plane[i].start.x, plane[i].start.y,
                 plane[i].end.x, plane[i].end.y, plane_color, 0.004);
    }
    const Point* ends[3][2] = {{&tri[0], &tri[1]}, {&tri[1], &tri[2]}, {&tri[2], &tri[0]}};
    for (int i = 0; i < 3; ++i) {
        osd.line(YAW_WIDGET_IDS[3 + i], ends[i][0]->x, ends[i][0]->y,
                 ends[i][1]->x, ends[i][1]->y, color, 0.006);
    }
}

}  // namespace

PanelLayout panel_layout(const DrawSettings& s) {
    // Макет «літачок ліворуч, на плашці» (2026-10-28). Відступи підібрані на
    // макеті; X, Y — лівий верхній кут першого рядка, як у plugin.py.
    PanelLayout L;
    L.panel_w = s.panel_w;
    L.row_pitch = s.dy * PANEL_ROW_PITCH_FACTOR;
    L.text_size = s.size * PANEL_TEXT_SIZE_FACTOR;
    L.panel_h = 3.0 * L.row_pitch + 0.004;
    switch (s.panel_pos) {
        case PanelPos::XY:
            L.panel_x = s.x - 0.02;
            L.panel_y = s.y - 0.002;
            break;
        case PanelPos::TopLeft:
        case PanelPos::BottomLeft:
            L.panel_x = PANEL_MARGIN_X;
            break;
        default:
            L.panel_x = 1.0 - PANEL_MARGIN_X - L.panel_w;
            break;
    }
    if (s.panel_pos == PanelPos::TopLeft || s.panel_pos == PanelPos::TopRight)
        L.panel_y = PANEL_MARGIN_TOP;
    else if (s.panel_pos != PanelPos::XY)
        L.panel_y = 1.0 - PANEL_MARGIN_BOTTOM - L.panel_h;
    L.text_y = L.panel_y + 0.002;
    L.plane_x = L.panel_x + 0.04;
    // Літачок трохи нижче середини плашки: над носом лишається місце для лінії
    // вгору (до 0.028 ширини — на екрані 2:1 це 0.056 висоти).
    L.plane_y = L.panel_y + L.panel_h / 2.0 + 0.0295;
    L.nose_x = L.plane_x;
    L.nose_y = L.plane_y - 0.055 * PANEL_PLANE_SCALE * s.yaw_widget_scale;
    L.marks_x = L.plane_x + 0.053;  // початок тексту (квадратиків на плашці вже немає)
    L.icon_x = (L.panel_x + L.marks_x) / 2.0;  // центр області ліворуч від тексту
    L.icon_y = L.panel_y + L.panel_h / 2.0;
    return L;
}

void draw_sample(Osd& osd, const GimbalSample* sample, bool stale,
                 const DrawSettings& s) {
    char pitch_text[64];
    char yaw_text[64];
    char status_text[64];
    std::uint32_t color;

    // Слова: у plugin.py (PYTHON_COMPAT) — великими літерами, на плашці
    // (2026-11-03) — звичайними: «Pitch», «Yaw», «Vehicle ok».
    const bool caps = s.yaw_style == YawStyle::Python;
    const char* w_pitch = caps ? "PITCH" : "Pitch";
    const char* w_yaw = caps ? "YAW" : "Yaw";
    if (sample == nullptr) {
        std::snprintf(pitch_text, sizeof(pitch_text), "ZR10 %s --", w_pitch);
        std::snprintf(yaw_text, sizeof(yaw_text), "ZR10 %s --", w_yaw);
        std::snprintf(status_text, sizeof(status_text), caps ? "ZR10 WAIT MAV" : "ZR10 Wait MAV");
        color = STATUS_WARN;
    } else {
        std::snprintf(pitch_text, sizeof(pitch_text), "ZR10 %s %+.1f deg", w_pitch, sample->pitch);
        std::snprintf(yaw_text, sizeof(yaw_text), "ZR10 %s %+.1f deg", w_yaw, sample->yaw);
        if (stale) {
            std::snprintf(status_text, sizeof(status_text), caps ? "ZR10 NO DATA" : "ZR10 No data");
            color = STATUS_WARN;
        } else if (sample->failure != 0) {
            std::snprintf(status_text, sizeof(status_text), caps ? "ZR10 FAILURE %d" : "ZR10 Failure %d",
                          sample->failure);
            color = STATUS_FAIL;
        } else if (!is_vehicle_yaw(sample->flags)) {
            std::snprintf(status_text, sizeof(status_text), caps ? "ZR10 FRAME %s" : "ZR10 Frame %s",
                          yaw_frame(sample->flags));
            color = STATUS_WARN;
        } else {
            std::snprintf(status_text, sizeof(status_text), caps ? "ZR10 VEHICLE OK" : "ZR10 Vehicle ok");
            color = STATUS_OK;
        }
    }

    struct Row {
        const char* text_id;
        const char* mark_id;
        double row_y;
        const char* value;
        double text_size;
    };
    // Вирази ті самі, що в plugin.py, щоб числа збігались до останнього знака.
    const Row rows[3] = {
        {OSD_IDS[0], MARK_IDS[0], s.y, pitch_text, s.size},
        {OSD_IDS[1], MARK_IDS[1], s.y + s.dy, yaw_text, s.size},
        {OSD_IDS[2], MARK_IDS[2], s.y + 2.0 * s.dy, status_text, s.size * 0.85},
    };

    Osd::Batch batch(osd);
    const double yaw = sample != nullptr ? sample->yaw : 0.0;
    double marks_x = s.x;     // Python: квадратики на X, як у plugin.py
    double row_h = s.dy;      // висота рядка
    Row panel_rows[3];
    const Row* use_rows = rows;
    if (s.yaw_style == YawStyle::Python) {
        draw_yaw_widget(osd, yaw, color, s);
    } else {
        const PanelLayout L = panel_layout(s);
        if (s.panel) draw_panel(osd, L, s);
        if (s.yaw_style == YawStyle::Line) draw_yaw_direction(osd, yaw, color, L, s);
        else if (s.yaw_style == YawStyle::Arrow) draw_yaw_arrow(osd, yaw, color, L, s);
        else if (s.yaw_style == YawStyle::ArrowBase) draw_yaw_arrow_base(osd, yaw, color, L, s);
        else if (s.yaw_style == YawStyle::Triangle) draw_yaw_triangle(osd, yaw, color, L, s);
        else if (s.yaw_style == YawStyle::Sector) draw_yaw_sector(osd, yaw, color, L, s);
        else draw_yaw_fan(osd, yaw, color, L, s);
        marks_x = L.marks_x;
        row_h = L.row_pitch;
        // На плашці: усі рядки однакового розміру, щільніше один до одного.
        for (int i = 0; i < 3; ++i) {
            panel_rows[i] = rows[i];
            panel_rows[i].row_y = L.text_y + i * L.row_pitch;
            panel_rows[i].text_size = L.text_size;
        }
        use_rows = panel_rows;
    }
    if (s.yaw_style != YawStyle::Python) {
        // На плашці (2026-10-29): без квадратиків; перші два рядки білі, рядок
        // статусу — кольором статусу (зелений / помаранчевий / червоний).
        for (int i = 0; i < 3; ++i) {
            const Row& row = use_rows[i];
            osd.text(row.text_id, marks_x, row.row_y, row.value, row.text_size, Osd::ALIGN_LEFT,
                     Osd::rgba(0, 0, 0, 0), std::nullopt, 0.30, row_h, 0.01,
                     i == 2 ? color : Osd::rgba(255, 255, 255));
        }
        return;
    }
    for (int i = 0; i < 3; ++i) {
        const Row& row = use_rows[i];
        Osd::RectFields mark;
        mark.fill = color;
        mark.border = color;
        mark.border_width = 0.001;
        osd.rect(row.mark_id, marks_x, row.row_y + (row_h - MARK_SIZE) / 2.0, MARK_SIZE,
                 MARK_SIZE, mark);
        osd.text(row.text_id, marks_x + MARK_SIZE + MARK_GAP, row.row_y, row.value,
                 row.text_size, Osd::ALIGN_LEFT, Osd::rgba(0, 0, 0, 0), std::nullopt,
                 0.30, row_h, 0.01);
    }
}

void clear_unused_elements(Osd& osd, const DrawSettings& s) {
    const YawStyle style = s.yaw_style;
    if (style != YawStyle::Python && !s.panel) osd.clear(PANEL_ID);
    if (style != YawStyle::Python) {
        for (const char* id : MARK_IDS) osd.clear(id);  // квадратиків на плашці більше немає
    }
    if (style != YawStyle::Sector && style != YawStyle::Python) clear_sector(osd);
    if (style != YawStyle::Python) {
        // Літачок або картинкою, або лініями — інший вигляд прибираємо.
        if (s.plane_image.empty()) {
            osd.clear(PLANE_IMAGE_ID);
        } else {
            osd.clear(YAW_WIDGET_IDS[0]);
            osd.clear(YAW_WIDGET_IDS[1]);
            osd.clear(YAW_WIDGET_IDS[2]);
        }
    }
    const bool no_plane = style == YawStyle::Arrow || style == YawStyle::ArrowBase ||
                          style == YawStyle::Triangle;
    if (no_plane) osd.clear(PLANE_IMAGE_ID);
    if (style == YawStyle::Sector) {
        osd.clear(YAW_WIDGET_IDS[3]);  // сторони й основа трикутника
        osd.clear(YAW_WIDGET_IDS[4]);
        osd.clear(YAW_WIDGET_IDS[5]);
        osd.clear(DIRECTION_ID);
    } else if (style == YawStyle::Line) {
        osd.clear(YAW_WIDGET_IDS[3]);  // промені віяла / сторони трикутника
        osd.clear(YAW_WIDGET_IDS[4]);
        osd.clear(YAW_WIDGET_IDS[5]);
    } else if (style == YawStyle::Triangle) {
        osd.clear(YAW_WIDGET_IDS[0]);  // літачка немає
        osd.clear(YAW_WIDGET_IDS[1]);
        osd.clear(YAW_WIDGET_IDS[2]);
        osd.clear(DIRECTION_ID);
    } else if (style == YawStyle::Arrow || style == YawStyle::ArrowBase) {
        osd.clear(YAW_WIDGET_IDS[0]);  // літачка в стрілки немає
        osd.clear(YAW_WIDGET_IDS[1]);
        osd.clear(YAW_WIDGET_IDS[2]);
        osd.clear(YAW_WIDGET_IDS[4]);  // основа трикутника
    } else if (style == YawStyle::Fan) {
        osd.clear(YAW_WIDGET_IDS[4]);  // основа трикутника
        osd.clear(DIRECTION_ID);
    }
}

void clear_plugin_elements(Osd& osd, YawStyle style) {
    // Той самий порядок, що OSD_IDS + MARK_IDS + YAW_WIDGET_IDS у plugin.py.
    for (const char* id : OSD_IDS) osd.clear(id);
    for (const char* id : MARK_IDS) osd.clear(id);
    for (const char* id : YAW_WIDGET_IDS) osd.clear(id);
    if (style != YawStyle::Python) {
        osd.clear(DIRECTION_ID);
        osd.clear(PANEL_ID);
        osd.clear(PLANE_IMAGE_ID);
        clear_sector(osd);
    }
}

}  // namespace zr10
