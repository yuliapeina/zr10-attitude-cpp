// attitude.hpp — перетворення кватерніона гімбала в кути і визначення frame.
// Переклад attitude.py з Python-версії плагіна. Поведінка та сама.
#pragma once

namespace zr10 {

// Число π (те саме значення, що math.pi у Python). Своя константа, бо M_PI
// є не в усіх компіляторах (наприклад, у Windows-збірці).
constexpr double PI = 3.14159265358979323846;

// Прапорці MAVLink GIMBAL_DEVICE_FLAGS (ті самі значення, що в attitude.py).
constexpr int YAW_IN_VEHICLE_FRAME = 32;
constexpr int YAW_IN_EARTH_FRAME = 64;

// Кути гімбала в градусах.
struct Euler {
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

// Перетворює кватерніон MAVLink q = [w, x, y, z] у roll, pitch, yaw (градуси).
// Повертає false, якщо кватерніон некоректний (нескінченні значення або
// нульова довжина). У Python у цьому випадку виникала помилка ValueError;
// тут програма не падає, а просто отримує false.
bool quat_to_euler_deg(const float q[4], Euler& out);
bool quat_to_euler_deg(double w, double x, double y, double z, Euler& out);

// Напрямок камери в градусах (-180…180, 0 = вперед по корпусу, + = вправо).
// ВІДМІННІСТЬ ВІД PYTHON-ВЕРСІЇ (рішення 2026-10-22).
// Звичайний yaw із quat_to_euler_deg() має «сліпу зону» (gimbal lock): коли
// камера дивиться рівно вниз (pitch -90°), він стає 0 незалежно від того,
// куди повернутий гімбал. Ця функція рахує напрямок інакше: куди дивиться
// об'єктив, а коли він дивиться вниз — куди спрямований «верх» кадру. Поза
// сліпою зоною (і при roll = 0) результат такий самий, як звичайний yaw.
// Повертає false для некоректного кватерніона (як quat_to_euler_deg).
bool camera_heading_deg(double w, double x, double y, double z, double& out);
bool camera_heading_deg(const float q[4], double& out);

// Повертає frame, оголошений прапорцями повідомлення:
// "VEHICLE", "EARTH", "INVALID" (обидва прапорці) або "UNSPECIFIED".
const char* yaw_frame(int flags);

// true лише тоді, коли yaw явно відносно корпусу апарата (frame VEHICLE).
bool is_vehicle_yaw(int flags);

}  // namespace zr10
