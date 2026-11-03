// attitude.cpp — див. пояснення в attitude.hpp.
#include "attitude.hpp"

#include <cmath>

namespace zr10 {

namespace {

// Так само, як math.degrees() у Python: x * (180 / pi).
double to_degrees(double radians) {
    return radians * (180.0 / PI);
}

}  // namespace

bool quat_to_euler_deg(double w, double x, double y, double z, Euler& out) {
    if (!std::isfinite(w) || !std::isfinite(x) ||
        !std::isfinite(y) || !std::isfinite(z)) {
        return false;  // кватерніон містить нескінченне значення
    }

    const double norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (!std::isfinite(norm) || norm == 0.0) {
        return false;  // нульова або некоректна довжина
    }

    // Нормалізація: ділимо кожну складову на довжину.
    w /= norm;
    x /= norm;
    y /= norm;
    z /= norm;

    const double roll = std::atan2(
        2.0 * (w * x + y * z),
        1.0 - 2.0 * (x * x + y * y));

    double pitch_argument = 2.0 * (w * y - z * x);
    // Обмежуємо діапазон -1..1, щоб asin не отримав зайве через округлення.
    if (pitch_argument > 1.0) pitch_argument = 1.0;
    if (pitch_argument < -1.0) pitch_argument = -1.0;
    const double pitch = std::asin(pitch_argument);

    const double yaw = std::atan2(
        2.0 * (w * z + x * y),
        1.0 - 2.0 * (y * y + z * z));

    out.roll = to_degrees(roll);
    out.pitch = to_degrees(pitch);
    out.yaw = to_degrees(yaw);
    return true;
}

bool quat_to_euler_deg(const float q[4], Euler& out) {
    // MAVLink передає кватерніон як float; Python теж перетворював на double.
    return quat_to_euler_deg(
        static_cast<double>(q[0]), static_cast<double>(q[1]),
        static_cast<double>(q[2]), static_cast<double>(q[3]), out);
}

bool camera_heading_deg(double w, double x, double y, double z, double& out) {
    if (!std::isfinite(w) || !std::isfinite(x) ||
        !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }
    const double norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (!std::isfinite(norm) || norm == 0.0) {
        return false;
    }
    w /= norm;
    x /= norm;
    y /= norm;
    z /= norm;

    // f — куди дивиться об'єктив (вісь X камери) у системі апарата
    // (x вперед, y вправо, z вниз).
    const double fx = 1.0 - 2.0 * (y * y + z * z);
    const double fy = 2.0 * (x * y + w * z);
    const double fz = 2.0 * (x * z - w * y);  // > 0, коли камера нахилена вниз
    // u — куди спрямований «верх» кадру (вісь -Z камери), горизонтальна частина.
    const double ux = -2.0 * (x * z + w * y);
    const double uy = -2.0 * (y * z - w * x);

    // Горизонтальний напрямок = напрямок об'єктива + «верх» кадру з вагою fz.
    // Камера дивиться вперед (fz ≈ 0) — важить лише об'єктив; дивиться рівно
    // вниз (горизонтальна частина об'єктива ≈ 0) — важить лише «верх» кадру.
    // При roll = 0 обидві частини спрямовані однаково, тож результат дорівнює
    // справжньому yaw на всьому діапазоні pitch, зокрема на -90°.
    const double hx = fx + fz * ux;
    const double hy = fy + fz * uy;
    if (std::hypot(hx, hy) < 1e-9) {
        // Теоретично недосяжно для реальних положень гімбала; про всяк випадок
        // беремо звичайний yaw.
        out = to_degrees(std::atan2(fy, fx));
        return true;
    }
    out = to_degrees(std::atan2(hy, hx));
    return true;
}

bool camera_heading_deg(const float q[4], double& out) {
    return camera_heading_deg(
        static_cast<double>(q[0]), static_cast<double>(q[1]),
        static_cast<double>(q[2]), static_cast<double>(q[3]), out);
}

const char* yaw_frame(int flags) {
    const bool vehicle = (flags & YAW_IN_VEHICLE_FRAME) != 0;
    const bool earth = (flags & YAW_IN_EARTH_FRAME) != 0;
    if (vehicle && earth) return "INVALID";
    if (vehicle) return "VEHICLE";
    if (earth) return "EARTH";
    return "UNSPECIFIED";
}

bool is_vehicle_yaw(int flags) {
    const bool vehicle = (flags & YAW_IN_VEHICLE_FRAME) != 0;
    const bool earth = (flags & YAW_IN_EARTH_FRAME) != 0;
    return vehicle && !earth;
}

}  // namespace zr10
