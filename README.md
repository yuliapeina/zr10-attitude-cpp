# zr10-attitude 0.3.8 — плагін R2D2 для гімбала SIYI ZR10

Нативний плагін для камери SSC30KQ (OpenIPC, R2D2, armv7l / SigmaStar I6E, без Docker).
Малює OSD на UDP `127.0.0.1:14777` — плашку в правому нижньому куті відео ZR10:
білий літачок, синє віяло напрямку камери, рядки `ZR10 Pitch` і `ZR10 Yaw`.
Якщо гімбал не відповідає 3 с — `--` і віяло зникає.

**Кути** — напряму з гімбала: двічі на секунду запит SIYI «поточні кути» (0x0D)
на `SIYI_IP:37260`. Інших команд гімбалу плагін не надсилає (ні руху, ні зуму).

**MAVLink** (`udpout:127.0.0.1:14550`, HEARTBEAT 255/0 раз на секунду) — лише для
журналу: плагін порівнює кути з повідомлення 285 польотного контролера з кутами
гімбала і пише `FC link lost`, коли контролер втратив гімбал по UART.
Команд контролеру не надсилає, файлів не пише.

## Структура

| Шлях | Що |
|---|---|
| `CMakeLists.txt` | збирає одну програму `zr10-attitude` |
| `src/*.cpp` | `main.cpp` (головний цикл), `siyi.cpp` (гімбал напряму), `mav.cpp` (MAVLink), `osd.cpp` (OSD JSON по UDP), `draw.cpp` (плашка), `config.cpp` (config.json), `attitude.cpp` (кути з кватерніона) |
| `include/*.hpp` | заголовки; `net.hpp` — UDP-сокети, `log.hpp` — журнал |
| `config.json` | налаштування за замовчуванням |

## Налаштування (config.json)

| Ключ | За замовчуванням | Що задає |
|---|---|---|
| `CONNECT` | `udpout:127.0.0.1:14550` | MAVLink від контролера (лише журнал) |
| `GIMBAL_ID` | 1 | id гімбала в повідомленні 285 |
| `CAM` | усі камери | номер камери R2D2 для плашки (0 — ZR10) |
| `SIYI_IP` | `192.168.144.25` | адреса ZR10; `""` — не питати гімбал |

## Залежності

Лише заголовки MAVLink **c_library_v2**, commit
`6a7760569ce6d46c56f93163703543d4345e940a` (діалект `ardupilotmega`).
CMake завантажує їх сам (FetchContent) або бере з `-DMAVLINK_DIR=<шлях>`.

## Збірка

```sh
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=<toolchain armv7 hard-float>
cmake --build build
```
Результат — статичний ELF `build/zr10-attitude` (ARMv7, hard-float, без динамічних залежностей).
Наша збірка того самого коду: zig 0.17.0 c++ `-target arm-linux-musleabihf -mcpu=cortex_a7 -static -Os -s`.

## Запуск

```sh
zr10-attitude [шлях/до/config.json]
```
Без аргументу шукає `config.json` у робочій теці, поруч із програмою, потім `/app/config.json`.
SIGTERM / SIGINT — прибирає свої елементи OSD і виходить з кодом 0.
