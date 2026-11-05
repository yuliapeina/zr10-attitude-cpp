# zr10-attitude 0.3.1 — плагін R2D2 для гімбала SIYI ZR10

Нативний плагін для камери SSC30KQ (OpenIPC, R2D2, armv7l / SigmaStar I6E, без Docker).
Читає MAVLink 285 `GIMBAL_DEVICE_ATTITUDE_STATUS` з `udpout:127.0.0.1:14550`
(як r2mav: HEARTBEAT 255/0 раз на секунду) і малює OSD на UDP `127.0.0.1:14777`:
плашку, рядки `ZR10 Pitch`, `ZR10 Yaw`, статус і віяло напрямку камери.
На гімбал нічого не надсилає, файлів не пише.

## Структура

| Шлях | Що |
|---|---|
| `CMakeLists.txt` | збирає одну програму `zr10-attitude` |
| `src/*.cpp` | код: `main.cpp` (головний цикл), `mav.cpp` (MAVLink), `osd.cpp` (OSD JSON по UDP), `draw.cpp` (малювання), `config.cpp` (config.json), `attitude.cpp`, `overlay.cpp` (кути й геометрія) |
| `include/*.hpp` | заголовки; `net.hpp` — UDP-сокети, `log.hpp` — журнал |
| `config.json` | налаштування за замовчуванням |

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
Без аргументу шукає `config.json` поруч із програмою, потім `/app/config.json`.
SIGTERM / SIGINT — прибирає свої елементи OSD і виходить з кодом 0.

Без config.json (або без ключа `YAW_STYLE`) напрямок малюється зафарбованим віялом
`sector` (з 0.3.1; раніше було `fan`). Інші стилі: `fan`, `line`, `arrow`,
`arrow_base`, `triangle`.
