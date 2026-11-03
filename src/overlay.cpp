// overlay.cpp — див. пояснення в overlay.hpp.
#include "overlay.hpp"

#include "attitude.hpp"  // PI

#include <cmath>

namespace zr10 {

namespace {

// Так само, як math.radians() і math.degrees() у Python.
double to_radians(double degrees) { return degrees * (PI / 180.0); }
double to_degrees(double radians) { return radians * (180.0 / PI); }

}  // namespace

void plane_segments(double center_x, double center_y, double scale,
                    Segment out[3]) {
    const double fuselage = 0.055 * scale;
    const double wings = 0.055 * scale;
    const double tail_y = 0.035 * scale;
    const double tail_half = 0.025 * scale;

    // Фюзеляж: вертикальна лінія через центр.
    out[0] = {{center_x, center_y - fuselage}, {center_x, center_y + fuselage}};
    // Крила: горизонтальна лінія через центр.
    out[1] = {{center_x - wings, center_y}, {center_x + wings, center_y}};
    // Хвіст: коротка горизонтальна лінія нижче центру.
    out[2] = {{center_x - tail_half, center_y + tail_y},
              {center_x + tail_half, center_y + tail_y}};
}

void yaw_triangle_points(double origin_x, double origin_y, double yaw_deg,
                         Point out[3], double length, double half_angle_deg) {
    // Порядок обчислень повторює Python, щоб результат збігався до останнього
    // знака: спочатку радіани половини кута, потім назад у градуси.
    const double half_angle = to_radians(half_angle_deg);
    const double half_deg = to_degrees(half_angle);

    auto endpoint = [&](double angle_deg) {
        const double r = to_radians(angle_deg);
        return Point{origin_x + length * std::sin(r),
                     origin_y - length * std::cos(r)};
    };

    out[0] = {origin_x, origin_y};
    out[1] = endpoint(yaw_deg - half_deg);
    out[2] = endpoint(yaw_deg + half_deg);
}

}  // namespace zr10
