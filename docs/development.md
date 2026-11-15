# Build, use and develop PACE

For first installation, follow **Minimal tools → Without a connected board → First build and flash → Device setup**. Use Demo and disposable credentials: the default build stores data unencrypted. See [security profiles](ota-security.md#device-data-profiles) before household use and [verification](verification.md) for current test evidence.

## Minimal tools

**Recommended IDE: Visual Studio Code with Espressif's official ESP-IDF extension** (`espressif.esp-idf-extension`) for building, flashing, monitoring and debugging. No additional framework or C++ language-server extension is needed.

Install these once on your development computer:

| Tool | Needed for |
| --- | --- |
| ESP-IDF **v5.5.6**, installed with Espressif Installation Manager (EIM) | ESP32 compiler, SDK Python environment, flashing and debugging tools |
| Git, native GCC or Clang with C++17, OpenSSL development headers/libraries, Node.js 24 LTS or newer supported LTS | Checkout, tests on your computer, adapter tests |
| clang-format (contributors) | C/C++ formatting |
| USB data cable (board stage only) | Flashing and serial monitoring |

Install Git, the native compiler and Node with your OS package manager. The native compiler runs host tests; the SDK compiler builds for the ESP32.

### openSUSE Tumbleweed prerequisites

Run this once in a normal terminal, before installing the SDK:

```sh
sudo zypper install git gcc-c++ cmake ninja python3 python3-pip \
  flex bison gperf ccache libffi-devel libopenssl-devel dfu-util \
  libusb-1_0-0 curl unzip nodejs26 npm26
```

`flex`, `bison` and `gperf` are SDK build prerequisites; `libffi-devel` and `libopenssl-devel` support Python/native dependencies; `dfu-util` and `libusb` support USB tools. `ccache` speeds repeated builds. Zypper resolves their supporting packages automatically. On other systems, use Espressif's [platform prerequisites](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/linux-macos-setup.html#step-1-install-prerequisites) and install a native compiler and Node LTS separately.

For C/C++ contributions on openSUSE, `sudo zypper install clang` supplies `clang-format`. CAD packages and Outlook's MSAL dependency are optional; install them only through the [enclosure](../hardware/enclosure.md#edit-or-regenerate-the-models) or [Outlook](adapters.md#outlook--microsoft-365) instructions.

### SDK and VS Code

1. Install VS Code, then install the **ESP-IDF** extension published by **Espressif Systems**.
2. Run **ESP-IDF: Open ESP-IDF Installation Manager** from the Command Palette. Install **v5.5.6** with the **ESP32-S3** target and **IDE** feature outside this repository. See the [official installation instructions](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html).
3. Open `firmware/` as the ESP-IDF project, then run **ESP-IDF: Select Current ESP-IDF Version → v5.5.6** in that window. Open the repository root in a separate window for host tests and other files.
4. Press **Ctrl+Shift+P**, run **ESP-IDF: Open ESP-IDF Terminal**, then run `idf.py --version`. It must report v5.5.6, matching [`.esp-idf-version`](../.esp-idf-version).

EIM installs the SDK-selected Xtensa/RISC-V/ULP compilers, GDB, OpenOCD, Ninja, ROM symbols, Espressif Clang/Clang libraries for the IDE, and an isolated Python environment with the SDK's required packages. The first firmware build downloads pinned LVGL; host tests use pinned cJSON. No global `pip install` or separate Arduino/PlatformIO installation is needed.

### Installer download or selection fails

- **HTTP 504 downloading EIM:** download the Linux x64 installer from the [official download page](https://dl.espressif.com/dl/eim/), choosing **Download** (Espressif's mirror) instead of **GitHub**. Extract it and run the installer. Do not change the project's SDK version to work around a download error.
- **Missing prerequisites:** install the named packages with your OS package manager, then rerun EIM.
- **No ESP-IDF setup selected:** finish installation, open `firmware/`, then run **ESP-IDF: Select Current ESP-IDF Version → v5.5.6**. Close the failed terminal and open a new ESP-IDF terminal. **ESP-IDF: Doctor Command** checks the configuration.

For a CLI-only Linux installer, extract its ZIP with `unzip`, open a terminal in the extracted directory, and install it persistently:

```sh
install -Dm755 ./eim "$HOME/.local/bin/eim"
"$HOME/.local/bin/eim" install --path "$HOME/.espressif" \
  --idf-versions v5.5.6 --target esp32s3 --idf-features ide \
  --mirror https://dl.espressif.com/github_assets --skip-components-download true
```

EIM registers the installation for VS Code; project builds download required components. Keep SDKs outside the project. Do not commit generated EIM configuration or machine-specific editor/tool paths.

To use this EIM installation from a normal terminal instead of VS Code:

```sh
. "$HOME/.espressif/tools/activate_idf_v5.5.6.sh"
idf.py --version
```

Run activation in each new terminal. For a manual SDK checkout without EIM, use `. /path/to/esp-idf/export.sh` instead.

Commands use Bash on Linux/macOS. On Windows, run host tests in WSL2 with Git, GCC/Clang, Python 3, CMake ≥3.20, Ninja and Node installed there; use a native ESP-IDF terminal for device builds/flashing. MSVC host tests are unsupported.

### Linux USB permissions

On openSUSE, grant your account serial-port access once:

```sh
sudo usermod -a -G dialout "$USER"
```

**Sign out of the desktop and sign back in**, then reopen VS Code. A new terminal alone does not update an existing desktop session's groups. Do not run VS Code or flashing commands as root.

For optional USB/JTAG debugging, if EIM reports that it could not copy OpenOCD rules, run these in an activated ESP-IDF terminal:

```sh
sudo install -m 0644 "$OPENOCD_SCRIPTS/../contrib/60-openocd.rules" /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

Unplug/replug the board after changing rules. These permissions do not validate the board's native USB/JTAG routing; first bring-up uses USB TO UART.

## Without a connected board

In an activated ESP-IDF terminal (or the WSL2 host environment above), go to the repository root containing `README.md`, `tools/` and `firmware/`, then run:

```sh
python3 tools/check_project.py
python3 -m unittest discover -s tools -p 'test_*.py'
cmake -S . -B build-host -G Ninja -DPACE_SANITIZERS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
PACE_FEED_CHECK="$PWD/build-host/pace_feed_check" node tests/adapters.test.mjs
PACE_FEED_CHECK="$PWD/build-host/pace_feed_check" node tests/setup.test.mjs
```

Every command must succeed; CTest and Node must report zero failures. `PACE_FEED_CHECK` enables provider-to-C++ integration, so keep it in the command. Node tests need no `npm install`.

The first CMake configure downloads the pinned cJSON dependency. For an offline build, append `-DCJSON_SOURCE_DIR=/path/to/esp-idf/components/json/cJSON` to that command. Sanitizers detect memory and undefined-behavior errors; disable them with `-DPACE_SANITIZERS=OFF` only on an unsupported GCC/Clang platform.

These tests exercise domain, feed, HTTP, open/Google route parsing, bounded geometry/maps, map ownership/cache/error paths, secondary-calendar priority/failure handling, visible-only scheduling, brightness persistence, OTA, upload queues and setup-page behavior with synthetic data and simulated SDK calls. They do not test real peripherals/accounts. To check a custom adapter's synthetic JSON, run `build-host/pace_feed_check /path/to/feed.json`.

For an actual LVGL render/interaction check after a firmware build has downloaded pinned LVGL, reconfigure the host build:

```sh
cmake -S . -B build-host -DPACE_LVGL_SOURCE_DIR="$PWD/firmware/managed_components/lvgl__lvgl"
cmake --build build-host
ctest --test-dir build-host --output-on-failure
mkdir -p /tmp/pace-preview
build-host/pace_ui_tests /tmp/pace-preview
```

The optional test writes PPM screenshots and exercises real controls, route previews, software brightness, German fonts and rotation math. Open the images in an image viewer. This is not a physical touch/readability test. To regenerate the font subsets, install Pillow **13.0.0** in a tooling environment, then run `python3 tools/generate_fonts.py` with `clang-format` available; the TTF and license come from pinned LVGL.

## First build and flash

### Build without hardware

After the host checks pass, use an activated ESP-IDF terminal, starting at the repository root:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py size
```

Expect **Project build complete** and `firmware/build/pace.bin` plus `pace.elf`. The application must fit a 3 MiB slot. **Stop here if no board is connected.**

Run `set-target` only on the first build or to reset local build settings. Later builds need only `idf.py build`. Defaults select the reference board, 8 MiB flash and octal PSRAM. Change build options with `idf.py menuconfig`; existing `sdkconfig` values override defaults. Export intentional defaults with `idf.py save-defconfig` and review them for private paths/URLs.

### Flash the display

1. Follow [wiring step 1](../hardware/wiring.md#assembly-order): check the revision and connect the display alone. Leave the sensor disconnected.
2. Find the serial port in the extension, Windows Device Manager or Linux `/dev/serial/by-id/`. From `firmware/`, replace `PORT` with that port:

   ```sh
   idf.py -p PORT flash monitor
   ```

3. Expect the PACE board identity in the log and the setup screen. Exit the monitor with **Ctrl+T**, release, then **X** (or **Ctrl+]**). Exiting the monitor does not stop PACE. Fix serial permissions if needed; do not run as root.
4. Unplug USB, then complete [wiring steps 2–3](../hardware/wiring.md#assembly-order) to attach the sensor. Reconnect power and continue below.

## Device setup

1. Scan the on-screen Wi-Fi QR code or join **PACE-…** with the shown password. Stay connected if your phone reports no internet.
2. Open **http://192.168.4.1** and wait for saved settings to load. The page is available only through the setup network. If loading fails, reconnect and select **Reload saved settings**; saving stays disabled to protect your existing configuration.
3. Enter your home/test **2.4 GHz Wi-Fi name and password**, not the PACE setup network credentials. Choose **Demo** for the first test.
4. Confirm **Time zone** (e.g. `Europe/Berlin`) and **Date-label locale** (e.g. `de-DE`), then select time format and °C/°F. Demo uses UTC regardless of the zone. Choose **Language / Sprache** for English or German controls. German umlauts and ß are supported; unsupported scripts are reported.
5. Optionally set category colors, night hours and outdoor weather. Weather needs a label and [coordinates](#coordinate-format); a name alone does not select the location.
6. Select **Preview settings**, then **Save and restart**. Reconnect your phone to its usual network. PACE connects, obtains time and loads the agenda.
7. Check Demo events, Inside temperature and touch. An Inside reading alone does not confirm Wi-Fi: it works offline. If the screen says **Wi-Fi offline** or **Time unknown**, resolve that first using [debugging](#debugging). Then follow [Google or Outlook setup](adapters.md), reopen Settings and enter the endpoint/secret. Authorization is checked after saving; failures appear on screen.
8. Run steps 1–6 of the [board checklist](verification.md#board-workflow) on the bench. Then [assemble the enclosure](../hardware/enclosure.md#assemble) and repeat temperature/touch checks in its final position. The remaining checklist covers security, updates and release qualification.

Hold **Settings for two seconds** to reopen setup. Saved values load automatically; change only what you need. Saved passwords, keys and private URLs stay hidden. **Leave a saved private field blank to keep it**, or enter its replacement. Changing the Wi-Fi name or calendar adapter requires its credential again.

To clear a saved value, use **Use an open network**, **Remove the saved HTTPS update source**, or **Remove the saved Google Routes API key**, as appropriate. Factory reset is separate: open diagnostics, type **ERASE**, and confirm.

Setup closes after **15 minutes without activity**. Typing and tapping keep it open; an idle tab does not. Home-Wi-Fi reconnect attempts resume after setup closes.

Use **Calendar / Kalender** for the agenda, **Day / Tag** and **Month / Monat**. Tap a day or an appointment count for the daily list; tap an appointment for location and details. **Today / Heute** returns the selected date to today. Arrows stay within loaded dates; `--` means not loaded, not empty. **Back / Zurück** closes details; 60 seconds idle returns to the agenda. **Sync now / Synchronisieren** refreshes immediately or queues one refresh while offline. A touch during night blanking only wakes the screen. Cold boot needs network time; cached, stale and expired data are labeled.

**Optional second calendar:** configure its ID/label in your adapter using [these steps](adapters.md#optional-second-calendar). Choose its accent with **Second calendar highlight** in setup. The week badge and month asterisk open the day list; main appointments remain first and control the Next banner. Updating the firmware alone does not add a second source to an older bridge.

**Traffic / Verkehr** is optional; configure the provider and journeys using [Traffic setup](adapters.md#traffic). The overview fits four travel times; tap a card for endpoints, a geographic preview, route summary and advisories. Scroll for the full details; **All routes / Alle Wege** returns to the overview. Calendar remains the startup/idle view. Wi-Fi uses a check/cross icon; offline and other faults also have text. Car previews show a simplified route; transit previews connect journey points and are labelled schematic. Open routes use a small OpenStreetMap street background; live vehicle tracking and navigation are outside scope. Tiles load when a route is opened; an unavailable map does not hide the travel time. Open routing refreshes on reopening the view; see the linked provider guide for coverage and service rules.

**Brightness:** tap **Settings / Einstellungen** briefly, move the slider, then **Save / Speichern**. The 20–100% preference survives restart; **Cancel / Abbrechen** restores the saved value. A two-second hold still opens Wi-Fi/calendar setup. The reference LCD-7 uses software dimming because its backlight connection is on/off; it does not save backlight power. Setup and update screens temporarily use full brightness. If saving fails, the footer reports it and the previous saved level remains.

The GUI and touch default to 180° rotation for the current STL enclosure. For bench use in the panel's native orientation, open `idf.py menuconfig → PACE` and disable **Rotate display and touch by 180 degrees**. Follow the enclosure's mounting orientation; this setting changes no CAD or wiring.

### Coordinate format

Use decimal degrees with a **decimal point (`.`)**. Latitude comes first (−90 to 90); longitude comes second (−180 to 180). Negative latitude means south; negative longitude means west.

| Field | Example |
| --- | --- |
| Route start or destination: two numbers separated by one comma | `48.1234, 8.5678` |
| Weather latitude: one number | `48.1234` |
| Weather longitude: one number | `8.5678` |

Replace these examples with your location. In OpenStreetMap, right-click a place and choose **Show address** to find its coordinates. Do not use a decimal comma, degree symbols or street names in coordinate fields.

## Firmware updates for users and developers

**OTA (over the air)** means installing firmware through Wi-Fi. PACE's simplest method is uploading a signed file through its local setup page. No hosting account, cloud deployment or paid tool is needed. The device checks the signature using the key trusted by its installed firmware, then writes the new application to the spare slot. See [security and rollback](ota-security.md#signed-application-updates).

### Prepare your terminal

1. Open an **ESP-IDF terminal** with the version in [`.esp-idf-version`](../.esp-idf-version). For activation instructions, see [SDK and VS Code](#sdk-and-vs-code). `(venv)` alone does not establish that the right SDK is active.
2. Change to the repository root containing `tools/` and `firmware/`. Run `idf.py --version`; it must report **ESP-IDF v5.5.6**.
3. Select the board's chip in this terminal. In Bash/zsh (Linux/macOS), run `export IDF_TARGET=esp32s3`. In PowerShell (Windows), run `$env:IDF_TARGET="esp32s3"`. Repeat this in each new terminal used for direct USB flashing.

The preparation helper also selects ESP32-S3 for its child build and checks the SDK version. It does not change your terminal settings or reset an existing build configuration. Required display and network features are selected through native Kconfig, including PNG decoding and TLS 1.3 when upgrading an older saved configuration. The commands below work in Bash/zsh and PowerShell; replace example paths/ports with your own. An absolute key path may be used instead of `$HOME`.

### Choose first installation or OTA

Hold **Settings / Einstellungen** on PACE for two seconds, join its displayed Wi-Fi with the displayed password, and open `http://192.168.4.1`. Stay connected if your phone/laptop reports no internet. Under **Diagnostics and recovery**, check whether firmware uploads are enabled:

- **Disabled, or the installed firmware has no upload control:** complete the one-time preparation below. Unsigned firmware cannot enable signed OTA through an upload.
- **Enabled:** skip one-time preparation and use **Everyday or development update** with the original signing key.

### One-time preparation

A signing key is a private file identifying the firmware you trust. Keep it outside the repository and back it up securely. For a first signed installation, run:

```sh
python tools/prepare_ota.py --key "$HOME/.pace-signing/pace.pem" --create-key --version 0.1.0 --output build-ota
```

`--create-key` creates the file only if missing. An existing key is reused; it is not replaced. The helper builds and verifies the signed image, then prints **Verified update:** and its file path. Stop if preparation fails. This profile does not enable Secure Boot, flash encryption or eFuse programming; it retains the development data-storage profile.

Connect a USB data cable to the board's **USB TO UART** connector. Use one USB power source and close other serial monitors. Find the port in VS Code, `/dev/serial/by-id/` on Linux, `/dev/cu.*` on macOS, or Windows Device Manager. Replace `PORT` in this command:

```sh
idf.py -C firmware -B "$PWD/build-ota/build" -D IDF_TARGET=esp32s3 -p PORT flash monitor
```

This uses the **signed build directory**. Flashing the ordinary `firmware/build` directory may install an unsigned build instead. A normal flash with the same partition layout does not intentionally erase Wi-Fi/settings/cache; do not run `erase-flash` for this step.

After boot, keep PACE powered for at least **30 seconds** so its health check can establish the fallback image. Exit the serial monitor with **Ctrl+T**, release, then **X** (or **Ctrl+]**). Open setup again and confirm uploads are enabled. First installation is complete; future versions can use Wi-Fi.

### Everyday or development update

1. In the prepared terminal, build with the **same key** and a version different from the installed version:

   ```sh
   python tools/prepare_ota.py --key "$HOME/.pace-signing/pace.pem" --version 0.1.1 --output build-ota
   ```

2. Wait for **Verified update:**. The upload file is `build-ota/update-0.1.1/pace.bin`. Keep it on the computer doing the upload, or copy just that file to your phone. Never transfer the signing key to the phone/device.
3. Hold **Settings** for two seconds. Join the displayed PACE Wi-Fi; the password changes each setup session. Open **http://192.168.4.1** and expand **Diagnostics and recovery**.
4. Under **Signed firmware file (.bin)**, choose that `pace.bin`, click **Upload and install**, and confirm. Use this file-upload control; the separate **Install signed update** control requires an optional HTTPS source. Do not submit **Save and restart**: uploading retains current settings.
5. Keep the browser page open and USB power connected through upload, verification and restart. The setup Wi-Fi may disappear on restart. Leave PACE running for at least **30 seconds**. Reopen setup, reconnect using the newly displayed password, and refresh diagnostics to check the installed version.

Use a new version, such as `0.1.2`, for each subsequent changed build. An existing versioned package is not overwritten; if its preparation completed successfully, upload that file without rebuilding. After a failed build that produced no package, retry the same command. Preparing a file never flashes or publishes it automatically.

If you used USB to install the newest version directly, that installation is already complete. Do not upload the same version again; use a different version for the next update.

### Target mismatch or other preparation errors

| Message / symptom | Action |
| --- | --- |
| `sdkconfig` is `esp32s3`, but environment `IDF_TARGET` is `esp32` | Set the terminal target to `esp32s3` as above and retry. The updated helper handles this for its build. Do **not** follow the generic suggestion to run `set-target esp32`: that is the wrong chip for PACE. No erase or key replacement is needed. |
| Output directory already contains an `esp32` build | Keep it intact. Use a fresh directory, e.g. `--output build-ota-s3`, with the same signing key. Use that directory in subsequent upload/USB paths. |
| SDK missing or version mismatch | Activate the pinned SDK in a new ESP-IDF terminal. No global Python/package installation is needed. |
| Key not found | For an existing signed device, restore its original key or correct the path. `--create-key` is for a first signed USB installation; a new key cannot update a device trusting another key. |
| Package already exists | Use the existing successfully prepared file, or choose a new version. Do not overwrite a previously shared version with different firmware. |
| Other build/signing error | Read the first tool error above the helper's final message. Correct it and retry; no device changes happen during preparation. |

### Optional HTTPS updates

Add `--image-url https://your-host.example/pace.bin` when preparing a version to generate a matching `manifest.json`. Put both files on your chosen HTTPS host only when you intend to publish them. In setup, save its manifest URL under **Firmware updates**. The manifest and binary must use the same exact HTTPS origin (scheme, hostname and port), with no redirects. A saved URL takes precedence over an optional PACE build-menu default. Tap the display's **Update** button or the setup page's **Install signed update**, then confirm. Wi-Fi and trustworthy time are required for HTTPS; local signed uploads need neither home Wi-Fi nor an update host.

### Debugging an update

- Keep the exact build's `build-ota/build/pace.elf` and `pace.map` alongside your private development notes. `idf.py -C firmware -B "$PWD/build-ota/build" -D IDF_TARGET=esp32s3 -p PORT monitor` uses that ELF to decode crashes. Save it before rebuilding a different candidate; do not attach device dumps or private logs publicly.
- Setup diagnostics expose the installed version and last OTA result name. Serial logs report that result without URLs, credentials or appointments. `ESP_ERR_INVALID_VERSION` means the version is already installed; change the version. `ESP_ERR_INVALID_STATE` often means the initial 30-second health check has not completed. CRC/size, TLS or signature errors require correcting the file/source, not bypassing validation.
- Upload only the signed application `pace.bin`, not `factory.bin`, the bootloader or partition table. For failed boot confirmation or an interrupted transfer, follow [rollback and USB recovery](ota-security.md#boot-health-and-rollback) and the [OTA board test](verification.md#board-workflow).
- Test with synthetic appointments and a disposable **separate** test key. Host tests simulate SDK rejection/rollback calls; only the board test establishes actual flash and bootloader recovery.

## Typical workflow

Make a focused change, add regression tests for changed behavior/error paths, run the [host checks](#without-a-connected-board), then [build the firmware](#build-without-hardware). Review `git diff --check` and `git diff`; inspect staged changes with `git diff --cached` before committing.

### Before publishing

Review `git status --short`, `git diff` and `git diff --cached`; only add intended public source files. Keep `local/`, credentials, route/contact settings, `.env`, device dumps, signing keys and build directories private. Share allowlisted firmware packages, not a ZIP of the entire working directory. Source archives must also exclude `.git`: Git history contains author names/emails and can retain deleted secrets. A clean current tree alone does not establish a private history; rotate any exposed credentials before publishing.

### With a connected board

Flash and monitor after the host checks/build pass. Keep that build's `firmware/build/pace.elf` to decode crashes. Run affected cases from the [board checklist](verification.md#board-workflow), changing one fault at a time. Record versions, board revision, steps and measured results there.

CI repeats automated checks and the ESP32-S3 build; it does not flash hardware or publish firmware.

## Debugging

| Symptom | First steps |
| --- | --- |
| No serial port | Check USB TO UART, data cable, selector and USB-bridge driver. Close another serial monitor. |
| Flash cannot connect | Hold BOOT, tap RESET, release BOOT, retry flash. See [recovery](ota-security.md#usb-recovery). |
| Blank/unstable display | Verify exact LCD-7 model/revision and power. Check PSRAM/RGB errors. Do not try another board's pin map. |
| Sensor unavailable | Unplug power; check H2=3.3 V and all four signals. CRC/disconnect errors must leave the agenda visible. |
| Wi-Fi unavailable | Check 2.4 GHz SSID/password and WPA2 support. Hold Settings to correct them. If it persists, capture 60–90 seconds after restart in the serial monitor: `network` messages report connection-request errors and SDK disconnection reason codes without credentials. `network=0 time=0` alone does not identify the cause. |
| Setup says “Open setup from the device Wi-Fi network” | Stay on PACE-… despite the no-internet warning, then open `http://192.168.4.1` directly in your browser. Temporarily disable a phone VPN if enabled. Older firmware incorrectly rejected IPv4-mapped IPv6 connections; rebuild and flash the current firmware if the message persists. |
| Time unknown / TLS unavailable | Allow DNS, outbound HTTPS and NTP (UDP 123). Do not disable certificate validation. |
| Traffic unavailable | For open routing, check coordinate order, HTTPS server/contact settings and coverage; car estimates have no live congestion. For Google, check key/API restrictions, billing, quota and addresses. See [provider setup](adapters.md#traffic). |
| Calendar denied | Check endpoint, pairing secret, provider authorization and account restrictions. |
| Stale or expired calendar | Inspect network/time/feed state. The last successful cache is retained. |
| Missing characters | Latin-1 fonts cover German but cannot display every script. Do not alter UTF-8 data to hide this limitation. |
| Reset/crash | Keep the matching ELF and private serial capture; `idf.py monitor` decodes backtraces. Share only redacted diagnostics. |

For host breakpoints, run `gdb build-host/pace_tests`. Start board debugging with the serial monitor. JTAG is optional: verify native USB routing, then use the extension's ESP32-S3 OpenOCD/GDB configuration. Keep one USB power source.

Logs report free heap, largest block and network-task stack margin. If these shrink repeatedly, use SDK [heap debugging](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/system/heap_debug.html). Redact private data before sharing logs.

## Learn by doing

| Topic | Small experiment | Observe |
| --- | --- | --- |
| C/C++ boundaries | Trace a sensor frame from `board.c` to `decode_sht40` | Driver bytes become a portable value |
| I²C and CRC | Corrupt a synthetic sensor byte in a host test | CRC rejects it; the last reading ages out |
| RTOS tasks and queues | Follow `available` and `completed` snapshot queues | A snapshot has one owner at a time |
| DMA and memory | Compare idle/display/sync heap and power on the board | PSRAM capacity differs from internal DMA memory |
| State machines and time | Run DST, freshness and night-schedule tests | UTC intervals avoid fixed-offset mistakes |
| Network/security | Reject an invalid certificate or wrong feed secret | No new agenda replaces the last valid one |
| Persistence | Perform the documented cache power-cut test | Reboot shows a complete validated snapshot or unavailable state |
| Bootloader/OTA | Use signed test images and interrupt a candidate boot | Previous valid application returns |

Do experiments with synthetic data on a recoverable development board. Do not burn eFuses as a learning exercise.

## Local install package

After a normal development build, run this from the repository root in the activated ESP-IDF terminal:

```sh
python3 tools/package_firmware.py
python3 -m http.server 8080 --bind 127.0.0.1 --directory build-package
```

This optional browser installer replaces the initial `idf.py flash` step. Open `http://localhost:8080` in a Web Serial-capable desktop browser, connect USB TO UART and select **Connect and install**. It needs internet to load ESP Web Tools. **Installation resets Wi-Fi/settings even without full erase**; choose erase to clear the calendar cache too.

The package includes build-derived offsets, hashes and dependency notices, excluding private data. Use a fresh `--output` directory for another build. Creating a package does not publish it.

The packager rejects builds that can provision hardware security on first boot. Those require the separately reviewed [security procedure](ota-security.md#device-data-profiles).

## Contributing

Read the relevant requirement, [component map](architecture.md#components) and [CONTRIBUTING.md](../CONTRIBUTING.md). Follow the workflow above. Describe behavior, test results and untested cases in the pull request. Format authored C/C++ with `.clang-format`. For dependency changes, update pins/lockfiles together, review upstream notes and rerun affected tests/builds. Record evidence only in [verification](verification.md).

## Small glossary

- **Firmware / MCU:** the device program / its microcontroller.
- **GPIO / I²C:** programmable pin / shared two-wire peripheral bus.
- **RTOS / task:** real-time operating system / independently scheduled work.
- **DMA / PSRAM:** hardware memory transfer / external working RAM.
- **NVS / OTA:** persistent key-value storage / updating firmware over the network.
- **CRC / TLS:** accidental-corruption check / authenticated encrypted network connection.
- **Host test / HIL:** test on your computer / test using real connected hardware.

## Publish documentation and a release

Documentation stays in the Markdown files linked from README. GitHub Pages renders those same files with Jekyll; do not maintain a separate HTML copy. In repository **Settings → Pages**, select **GitHub Actions**. The `documentation` workflow builds and publishes on pushes to `main`; its deployment link opens the site. The layout and navigation live in `_layouts/default.html`, with build settings in `_config.yml`.

For a release:

1. Update the default version in `firmware/CMakeLists.txt`, run the checks above and record actual results in [verification](verification.md).
2. Build with the retained signing key and that version using [OTA preparation](#one-time-preparation). Test the signed image on the reference board.
3. Package only the approved build outputs with `python tools/package_firmware.py --build build-ota/build --output build-package-VERSION`. Keep the key, SDK configuration, logs, ELF/debug files, local notes and device data private.
4. Review staged source and package contents, commit, then create and push a matching Git tag. Create the GitHub release from that tag, attach the signed `pace.bin`, a factory-install package with notices and checksums, and describe remaining limitations. Publish only after the CI checks pass.

The factory package is for the documented board only. Its initial installation resets settings. An OTA upload preserves settings and must be signed by the key already trusted by the installed firmware. A firmware signature authenticates an update; it does not encrypt stored user data.
