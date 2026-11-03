// overlay.hpp — геометрія індикатора напрямку: літачок і трикутник камери.
// Переклад overlay.py з Python-версії плагіна. Чиста математика, без OSD.
//
// Координати екрана — частки 0.0…1.0: x вправо, y вниз.
// yaw = 0 — трикутник дивиться вгору; додатний yaw — вправо.
#pragma once

namespace zr10 {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Segment {
    Point start;
    Point end;
};

// Три відрізки нерухомого літачка (вид зверху): фюзеляж, крила, хвіст.
// out[0] — фюзеляж, out[1] — крила, out[2] — хвіст.
void plane_segments(double center_x, double center_y, double scale,
                    Segment out[3]);

// Вершина і дві кінцеві точки сектора камери, повернутого на yaw_deg.
// out[0] — вершина (origin), out[1] — лівий край, out[2] — правий край.
// Значення за замовчуванням такі самі, як у Python.
void yaw_triangle_points(double origin_x, double origin_y, double yaw_deg,
                         Point out[3], double length = 0.14,
                         double half_angle_deg = 22.0);

}  // namespace zr10
