# ESP32 File Hub

ESP32 File Hub is a PlatformIO/Arduino firmware project for the ESP32-2432S028
("Cheap Yellow Display"). It serves a browser-based file manager from the
device, stores files on a MicroSD card, and provides a compact touchscreen
status UI.

## Features

- CYD ILI9341 display and XPT2046 touch interface; Home, Files, Transfer, and
  Settings screens plus a touchscreen photo gallery, JPEG/PNG/BMP viewer,
  cached JPEG previews, album creation, file/folder browsing, and long-press
  delete confirmation.
- MicroSD file listing, uploads, downloads, folder creation, rename, delete,
  copy/move between internal and optional external cards, capacity reporting,
  and periodic detection/remount after card insertion or removal.
- Photo albums are directories under `/Albums` on the selected card; the
  touchscreen viewer supports JPG/JPEG, PNG, and uncompressed 24-bit BMP.
- Simultaneous provisioning access point and WiFi station connection, with
  reconnect attempts and a browser settings page.
- Responsive dark dashboard, login/session timeout, browser upload progress,
  transfer reporting, and authenticated firmware OTA.
- Pressure-gated, five-sample touch filtering with stored four-corner
  calibration. Gallery downloads include streaming progress and attachment
  filenames.
- AsyncWebServer request handling and a low-priority FreeRTOS UI/touch task.

## Project layout

```text
platformio.ini
include/
  cyd_config.h
  display_manager.h
  file_manager.h
  ota_manager.h
  touch_manager.h
  transfer_state.h
  web_server.h
  wifi_manager.h
src/
  display_manager.cpp
  file_manager.cpp
  main.cpp
  ota_manager.cpp
  touch_manager.cpp
  transfer_state.cpp
  web_server.cpp
  wifi_manager.cpp
```

The HTML, CSS, and JavaScript dashboard are embedded in `src/web_server.cpp`;
no separate filesystem image or external web host is required.

## Pin connections

The pinout below matches the commonly sold ESP32-2432S028 CYD revision with
the onboard SD socket. The TFT uses HSPI; touch and SD share VSPI but have
different GPIO routes. The firmware switches VSPI routing under a mutex before
touch reads and SD operations, following the CYD touch/SD wiring.
The onboard parts are already wired—these are reference pins, not a request to
jumper the modules together.

| Function | ESP32 GPIO | Notes |
|---|---:|---|
| TFT SCLK / MOSI / MISO | 14 / 13 / 12 | HSPI (TFT_eSPI `USE_HSPI_PORT`) |
| TFT CS / DC / RESET | 15 / 2 / tied to board reset | TFT reset is configured `-1` |
| TFT backlight | 21 | PWM brightness |
| Touch SCLK / MISO / MOSI | 25 / 39 / 32 | XPT2046, VSPI |
| Touch CS / IRQ | 33 / 36 | XPT2046, VSPI |
| SD SCLK / MISO / MOSI / CS | 18 / 19 / 23 / 5 | VSPI (pin route switches from touch) |
| Optional external SPI SD CS | 22 | Shares the onboard SD SCLK/MISO/MOSI lines |
| Power | board 5 V input | Follow the board vendor's input rating |

```text
                  ESP32-2432S028 CYD (onboard wiring)
        +----------------------------------------------+
        |                                              |
        | GPIO 14/12/13 + CS 15/DC 2 --> ILI9341 TFT   |
        | GPIO 25/39/32 + CS 33/IRQ 36 -> XPT2046      |
        | GPIO 21 ----------------------> TFT backlight |
        | GPIO 18/19/23 + CS 5 ---------> MicroSD slot  |
        | GPIO 18/19/23 + CS 22 --------> optional SD slot |
        |                                              |
        +----------------------------------------------+

  The TFT uses HSPI. Touch and MicroSD use VSPI with separate GPIO routing;
  firmware serializes access and switches the VSPI pins between peripherals.
  The optional card shares the onboard SD SPI signals and uses a separate
  chip-select. No external jumpers are needed for onboard parts.
```

**CYD revisions vary.** Check your board schematic/silkscreen before connecting
external hardware. Some CYD variants use a different backlight or SD wiring.
Do not connect a 5 V logic signal to an ESP32 GPIO.

### Optional external storage

The CYD's onboard MicroSD remains the internal volume. For expansion, connect a
second **3.3 V SPI MicroSD module** in parallel with the onboard SD SPI signals:

| External SD module | CYD GPIO |
|---|---:|
| SCK / CLK | 18 |
| MISO / DO | 19 |
| MOSI / DI | 23 |
| CS | 22 |
| VCC / GND | regulated 3.3 V / GND |

Use a module/card socket with 3.3 V logic compatibility and common ground. Keep
the CS line inactive-high during boot. The external slot is optional; the
firmware probes it, reports insertion/removal, and shows its capacity when
mounted. The classic ESP32-2432S028 has no USB host controller, so a USB thumb
drive cannot be used as this expansion without additional host hardware and
different firmware.

## Build and flash

1. Install Visual Studio Code with PlatformIO IDE, or install PlatformIO Core.
2. Open this folder as a PlatformIO project. PlatformIO resolves the libraries
   listed in `platformio.ini` on the first build.
3. Insert a FAT32-formatted onboard MicroSD card. Optionally connect the
   external SPI SD module above and insert a second FAT32 card.
4. Connect the CYD USB port to the computer; select the detected serial port.
5. Build and upload:

   ```powershell
   pio run -e cyd
   pio run -e cyd -t upload
   pio device monitor -b 115200
   ```

6. Connect to the device WiFi **ESP32-FileHub** using password **FileHub123**.
   The captive portal should open the dashboard/login page automatically.
   If your phone does not show a sign-in prompt, open `http://192.168.4.1`.
7. Sign in with username **admin** and password **admin12345**. Optionally
   enter home WiFi credentials under Settings; the setup AP remains available
   alongside station mode. The TFT shows the current reachable IP and can
   display the dashboard or WiFi QR code.
8. Upload a board-compatible `.bin` under **Firmware OTA**. Keep the device
   powered until it restarts. For recovery, use USB serial upload.

The default AP and dashboard credentials are defined in `include/cyd_config.h`.
Change them in firmware before deployment on a shared or untrusted network.
The firmware resets these two credentials to their configured defaults at
startup; saved station WiFi credentials are retained.

To clear saved station credentials, erase the ESP32 flash/NVS from PlatformIO
(`pio run -e cyd -t erase`) and flash again. The SD card contents are not
erased by this operation.

## Browser API and security notes

All management APIs except `/api/login` and the minimal `/api/status` endpoint
require an authenticated session. Login issues an HttpOnly, SameSite=Strict
cookie with a 15-minute idle timeout. Logout invalidates the device's active
session. File paths are normalized and reject traversal segments; uploaded
folder paths are created as needed. Empty folders can be deleted; non-empty
folders must first be cleared.

This firmware serves **plain HTTP** and stores network/login values in ESP32
NVS. It is intended for a trusted local network or an isolated device AP—not
direct exposure to the public Internet. Use a trusted network, avoid sensitive
transfers on untrusted WiFi, and use an encrypted tunnel/reverse proxy if remote
access is required. There is one active login session; a later successful
login replaces it.

OTA checks the firmware image using the ESP32 Update library and the selected
partition layout. The board needs an OTA-capable partition table and enough
space for the new image. A failed/interrupted update may require USB recovery.
Never remove power during an update.

## Storage behavior and limits

- The SD library reports card capacity and used bytes. A missing card is
  retried automatically; either card can be inserted or removed while the
  device is running.
- Internal storage appears at `/` in the browser. The optional volume is
  `/external`; use the volume selector to browse/upload/download it. Browser
  Copy and Move actions can transfer individual files between card volumes.
- In the browser, open **Photos** to browse albums and JPG/JPEG/PNG/BMP files
  on either SD card, then choose **Download photo**. The Files page can download
  other file types as well. Downloads use the authenticated
  `/download?file=<path>` streaming endpoint; the older `/api/download?path=`
  endpoint remains available.
- The touchscreen Files page has Up, previous/next page, volume toggle, and
  per-entry Delete controls. Deletion requires confirmation; folders must be
  empty.
- Touch **Gallery** to browse `/Albums`. Create albums with the on-screen
  keyboard, tap a JPG/JPEG/PNG/BMP to view it, use Prev/Next to navigate, and
  hold a Delete target for 0.8 seconds before confirming removal. The Gallery
  **SCAN** button rescans the current folder and invalidates cached JPEG
  previews. JPEG, PNG, and uncompressed 24-bit BMP images are rendered; the
  PNG decoder does not support interlaced images and limits source width to
  320 pixels. Unsupported or corrupt images show an error instead of a blank
  viewer. Gallery counts and serial logs cover the currently scanned folder.
- The browser lists up to 150 entries per folder. Folder creation supports
  nested paths; rename can move a file to a new folder path. Touchscreen lists
  show five entries per page.
- Standard Arduino `SD`/FAT support does not expose reliable creation or
  modification timestamps. The API identifies this limitation rather than
  reporting fabricated dates.
- Browser upload and download progress is sent back to the device for the TFT
  and dashboard. Downloads are streamed by ESPAsyncWebServer; very large files
  and several concurrent transfers are constrained by card and network speed.
- To recalibrate touch, open **Settings** and tap **CALIBRATE**, then tap the
  four crosshairs in order. Calibration bounds are saved in NVS and reloaded
  at startup. Touch requires stable pressure and five consistent samples over
  at least 60 ms; movement over five screen pixels cancels a pending delete
  hold.
- For image diagnostics, open **Settings → IMAGE TEST**. The test page reports
  SD mount state, free heap, largest heap block, and the next image from the
  currently selected gallery folder. Tap **LOAD IMAGE** to run the same
  validated loader used by the gallery; failure messages and decoder return
  codes are also printed to Serial at 115200 baud. The TFT uses HSPI and the
  SD card uses VSPI; file decoding holds the SD lock while drawing, and the
  JPEG output callback continues decoding blocks that overlap the footer
  instead of aborting the decoder.
- To enter home WiFi credentials without a computer, open **Settings** on the
  touchscreen, tap **SET WIFI**, enter the network name using the on-screen
  keyboard and tap **NEXT**, then enter the password and tap **SAVE**. Use
  **123/ABC** for numbers/symbols and **SHIFT** to change letter case. The
  password is masked and must be re-entered when updating the network.

## Troubleshooting

| Symptom | Check |
|---|---|
| SD unavailable | Format FAT32, reinsert the card, check the board revision and SD GPIO assignments; view `[SD]` serial messages. External storage must use GPIO 22 CS and share GPIO 18/19/23. |
| No WiFi AP | Open the serial monitor at 115200 and verify the AP credentials and startup messages. Stay near the board and use `http://192.168.4.1`. |
| Login rejected | Use the first-boot credentials from serial. Erase NVS and reflash to generate a new login if the saved password is lost. |
| TFT blank | Check USB/power, confirm ILI9341 and pin defines in `platformio.ini`, and verify that the board is an ESP32-2432S028 revision. |
| Touch is inaccurate | Check the GPIO 25/39/32/33/36 touch routing, then use Settings → CALIBRATE; different panel revisions can have different raw limits. |
| OTA fails | Build for ESP32 with the same partition layout; ensure the image fits the OTA slot and use a `.bin` firmware image. Recover by USB if necessary. |
| Compile error in a dependency | Update PlatformIO and resolve dependencies from `platformio.ini`; the ESPAsyncWebServer and AsyncTCP libraries must be the ESP32Async versions. |

## Memory and reliability notes

- The UI runs in its own modest-priority FreeRTOS task; the HTTP server is
  asynchronous and there is no framebuffer allocation. TFT redraws are direct,
  so the UI uses less RAM than double-buffering a 320x240 RGB565 frame
  (about 150 KiB).
- JSON listings are bounded to 150 directory entries and use a bounded
  ArduinoJson document. Lower this limit in `src/file_manager.cpp` on
  low-memory builds.
- Do not add large in-memory file buffers: upload data is written in server
  chunks, and downloads use the filesystem response.
- Avoid running more than one bulk transfer at a time. Use a quality SD card,
  short reliable USB supply, and a watchdog-friendly power source.

## License

No license was supplied with the original repository. Add a license before
redistributing this project.
