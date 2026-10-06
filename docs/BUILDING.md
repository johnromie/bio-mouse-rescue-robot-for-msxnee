# Building & VS Code Setup

Both firmwares **compile cleanly** and were verified with a real build, not just
inspected.

| Target | Board / core | Result |
|---|---|---|
| `firmware/arduino/BioMouse_Arduino` | `arduino:avr:uno` (1.8.6) | ✅ 19,678 B flash (61%), 1,301 B RAM (63%) |
| `firmware/esp32cam/BioMouse_ESP32CAM` | `esp32:esp32:esp32cam` (3.1.3) | ✅ 1,039,240 B flash (33%) |

---

## 1. The red squiggles are an editor problem, not a code problem

If VS Code showed errors like:

```
cannot open source file "Arduino.h"
cannot open source file "Wire.h"
cannot open source file "WebServer.h"
```

…those are **C/C++ IntelliSense includePath** errors. The C++ extension did not
know where the Arduino core headers live, so it flagged every `#include`. The
firmware itself was fine — and in fact turned out to contain several genuine
compile errors that only a real build would reveal (see section 4).

`.vscode/c_cpp_properties.json` and `.vscode/settings.json` have now been added
and every path in them was verified to exist on this machine.

### Switching between the two boards

This project targets **two different boards**, which need different include
paths. Press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> →
`C/C++: Select IntelliSense Configuration`:

- Editing `firmware/arduino/**` → choose **Arduino-UNO**
- Editing `firmware/esp32cam/**` → choose **ESP32-CAM**

### If paths break after reinstalling the IDE

Run this to find the real locations and update the JSON:

```powershell
$ESP="C:\Users\Asus\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.1.3"
$LIB="C:\Users\Asus\AppData\Local\Arduino15\packages\esp32\tools\esp32-arduino-libs"
Get-ChildItem $LIB -Directory            # -> the idf-release_v5.x-... folder
Get-ChildItem "$ESP\cores" -Directory
Get-ChildItem "$env:USERPROFILE\Documents\Arduino\libraries" -Directory
```

---

## 2. Building from the command line (recommended)

`arduino-cli` ships inside the Arduino IDE. No separate install needed:

```powershell
$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

# Arduino UNO
& $cli compile --fqbn arduino:avr:uno "C:\Users\Asus\Desktop\robot\firmware\arduino\BioMouse_Arduino"

# ESP32-CAM
& $cli compile --fqbn esp32:esp32:esp32cam "C:\Users\Asus\Desktop\robot\firmware\esp32cam\BioMouse_ESP32CAM"
```

To install the thermal library:

```powershell
& $cli lib install "Adafruit AMG88xx Library"
```

---

## 3. Required libraries

| Library | Why |
|---|---|
| **Adafruit AMG88xx Library** | Drives the AMG8833 thermal array |
| **Adafruit BusIO** | Dependency, pulled in automatically |

> ### ⚠️ There is no "Adafruit AMG8833" library
>
> Searching the Arduino library index (44,541 libraries) returns only
> **Adafruit AMG88xx Library**. The AMG8833 is a model of the AMG88xx family and
> is driven by that one library. Asking the Library Manager for "AMG8833 IR
> Sensor" will not find anything.
>
> The class is `Adafruit_AMG88xx`, and the I2C address is **`0x69`**, not `0x33`.

---

## 4. Real compile errors this uncovered (all now fixed)

Building for real found bugs that inspection had missed:

### ESP32-CAM — API changes in core 3.x

| Problem | Fix |
|---|---|
| `sram32_gb_iram0_dflt`, `esp_camera_de_mem` removed in core 3.x | Removed those lines; the driver now manages its own memory |
| `esp_camera_fb_get(&fb)` returned `esp_err_t` | Now takes **no arguments** and returns the pointer directly |
| `esp_camera_sensor_set(s, size)` removed | Use `s->set_framesize(s, size)` |
| `framesize_t` unknown in `config.h` | Added `#include <esp_camera.h>` (which also pulls in `sensor.h`) |
| `esp_camera_sensor.h` does not exist | `esp_camera.h` already includes `sensor.h` |
| `WebServer::hasClient()` removed | Use `server.client().connected()` |
| `server.send(code, type, buf, len)` overload removed | Use `setContentLength()` + `sendContent(ptr, len)` |

### Arduino UNO — wrong library and I2C address

| Problem | Fix |
|---|---|
| `#include <Adafruit_AMG8833.h>` — file does not exist | `Adafruit_AMG88xx.h` |
| `Adafruit_AMG8833` class | `Adafruit_AMG88xx` |
| `readPixel(x, y)` does not exist | `readPixels(buf, 64)`, then index `buf[(row*8)+col]` |
| I2C address `0x33` | `0x69` |
| `F"% ("` — unterminated macro in the boot banner | `F("% (")` |

### RAM note
The UNO has only 2 KB of RAM and the sketch now uses 63%. That is workable but
not generous — avoid adding large buffers. If you add features and hit
"Low memory available", move rarely-used code out of globals into locals.

---

## 5. Quick checklist before flashing

- [ ] `arduino-cli compile --fqbn arduino:avr:uno ...` reports no errors
- [ ] `arduino-cli compile --fqbn esp32:esp32:esp32cam ...` reports no errors
- [ ] `config.h` pins match your wiring (see `PIN_CONFIGURATION.md`)
- [ ] **ENA/ENB on PWM pins** and L298N jumpers removed
- [ ] ESP32 3.3 V logic not wired straight to a 5 V Arduino TX pin
- [ ] Wheels lifted for the first motor test