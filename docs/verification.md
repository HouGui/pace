# Verification and implementation status

Reviewed **2026-11-15** for **0.1.0**. Reference hardware: Waveshare ESP32-S3-Touch-LCD-7 Rev 1.2 and Adafruit SHT40 (#4885). Automated checks and limited device observations are listed separately. This initial release is development firmware; thermal accuracy and full hardware qualification remain unverified.

## Implemented

| Area | Present software | Evidence / remaining check |
| --- | --- | --- |
| Calendar | Feed v2 with optional second calendar, month coverage/location, 10-minute and coalesced manual sync, agenda/day/month/details, all-day/overlap, Now/Next, stale/offline state | Host + provider-to-C++ tests; secondary-calendar priority/failure tests; live-device validation pending |
| Traffic/maps | Open OSRM car and MOTIS/Transitous transit; optional legacy Google; four journeys, details, bounded OpenStreetMap street tiles/cache and route overlay | Synthetic parser/worker/setup/LVGL tests; live public OSRM/PNG compatibility below; device car-route check below; live transit, sustained memory/latency and journey accuracy pending |
| Time | UTC trust, civil days and offset segments, header GMT offset, compact week times, DST-aware clock/night policy | Host tests include gaps/folds, fractional offsets and 23/25/24.5-hour days |
| Indoor temperature | SHT40 transaction, CRC, conversion, sampling and age/error display | Conversion/CRC tests; sensor accuracy/disconnect on board pending |
| Outdoor temperature | Independent Open-Meteo HTTPS adapter, location label, units, source age and expiry | Live public-coordinate response accepted by actual host parser; device HTTPS/display validation pending |
| Board/UI | Native RGB/GT911/CH422G, English/German UI and Latin-1 fonts, temperature cards, 180° display/touch orientation, setup QR, night wake and saved software brightness | Cross-build + actual LVGL render/interaction and brightness persistence/failure tests; physical alignment/readability/tearing pending |
| Setup/storage | Temporary WPA2 portal, CSRF/Origin/interface checks, bounded settings, account-bound NVS cache, optional encrypted partitions, confirmed reset | Saved-settings/authorization/timeout handler tests; phone checks below; encrypted-storage and power-cut validation pending |
| Providers | Google Apps Script and optional Outlook/MSAL service | Synthetic provider tests; live account authorization/deployment pending |
| Updates | Signature-gated HTTPS and local signed uploads, bounded streaming, progress/result diagnostics, build/package helper, candidate health and rollback calls | Transaction/queue tests, SDK RSA checks, owner-reported upload and UART flash/boot below; first-boot rollback and power cuts pending |
| Developer tooling | CMake/CTest, sanitizers, Node tests, CI definition, local installer packaging | Checks and build results below |

## Local evidence

Reproduce the software checks with the [host checks](development.md#without-a-connected-board) and [firmware build](development.md#build-without-hardware). Fixtures are synthetic unless a public-service check is identified below.

### Current software and device checks — 2026-11-15

| Check | Evidence |
| --- | --- |
| Host regression | **14 CTest suites pass** with address, undefined-behavior and leak sanitizers. Covers calendar/feed, HTTP, route parsing, map bounds/cache/ownership, brightness, OTA and setup. The actual setup-handler suite performs 735 checks, including secret retention, failed reads/writes, authorization and the 15-minute boundary. |
| Adapter and setup integration | **23 adapter and 11 setup Node tests pass** through the actual C++ parser. Covers both calendar providers, optional secondary sources, saved-setting edits, coordinates and error paths. |
| Signed firmware | ESP-IDF v5.5.6 / ESP32-S3, **1,773,568 bytes**, SDK RSA signature verification passed; fits the 3 MiB application slot. |
| Privacy | Gitleaks 8.30.1 found no secrets in the public candidate files. Additional review checked personal identifiers, paths, image assets and firmware artifacts. Device data and the signing key are excluded. This is a bounded review, not a guarantee against every possible disclosure. |
| Tooling | **21 Python tests pass**, plus project consistency, local links, formatting and whitespace checks. OTA tests include inherited wrong-target environments and preserved existing configurations. |
| Display | Real LVGL render/interaction tests at 800 × 480 pass, including map attribution, primary/secondary calendar priority, brightness and decoded-map reuse/invalidation. The map fixture is synthetic. Physical latency is not established by host tests. |
| Device setup | The owner confirmed that reopening phone setup, saving and retaining settings worked. UART recorded reconnection with saved Wi-Fi and 5,416 bytes of HTTP-task stack headroom. The full idle period and interrupted writes still need hardware tests. |
| Device routing | Signed **0.1.0** installed over UART; all written-region hashes verified. UART confirmed the version, Wi-Fi reconnection and a valid car route (HTTP 200), with at least 6,980 bytes of network-stack headroom and no reset during the five-minute capture. Settings were retained. The device owner confirmed journey time and map. |
| Map responsiveness | The renderer pre-renders route lines into the existing image and reuses the last decoded map when reopened. Automated render/cache tests pass. The device owner reported a small improvement and acceptable interaction; the ≤200 ms target has not been measured. |
| Live public compatibility | Public OSRM example coordinates passed the C++ route parser. A public OSM tile passed bounded PNG/LVGL decoding. No live Transitous journey was tested; public-transport coverage remains location/provider dependent. |

Compiler checks limit stack frames in setup, HTTP and routing code. Measured diagnostics/settings frames are 544/144 bytes. TLS 1.3 worked against the public OSRM host while TLS 1.2 was rejected; certificate and hostname verification stay enabled. A separate SDK signing test accepted the matching key and rejected a wrong key and corrupted image. The encrypted-storage profile has cross-built, but runtime encryption/provisioning has not been qualified.

No eFuses or hardware security provisioning were performed. Private captures, settings, build configuration and signing keys are excluded from public source and release packages.

## Remaining release gates

- Run the board workflow below, including 72-hour soak, final mounted sensor accuracy and touch/readability checks.
- Provision/test encrypted credentials **and cache**, signing-key custody and signed USB recovery before real household use. The default profile is unencrypted.
- Test both provider setups with real disposable accounts and non-authors. Google tenant restrictions and Microsoft hosting/consent cannot be validated by mocks.
- English/German controls and Latin-1 glyphs are covered by software checks. Other scripts remain unsupported; on-device language/readability checks remain required.
- Wi-Fi names and weather coordinates are entered manually. Network scanning/city lookup and a pre-save live calendar preview are not implemented.
- Measure GUI overflow/contrast at one metre and RGB stability during Wi-Fi/flash activity. The current single framebuffer can tear during redraw.
- Recheck parts availability and delivered cost for each build. Publication does not establish hardware acceptance.

## Board workflow

Follow [assembly](../hardware/wiring.md#assembly-order) and [build/flash](development.md#first-build-and-flash). Use synthetic events and a disposable adapter secret. Save dated results under `tests/hil/reports/` only after running them.

Begin with steps 1–6 on the bench. Security/update cases need their configured test profiles. Assemble the enclosure before step 11 and repeat sensor/touch checks in the final position; all applicable steps are release gates.

| Step | Procedure | Pass condition / requirements |
| --- | --- | --- |
| 1. Identity and power | Record module/PCB/panel, supply/cable, firmware and SDK. Boot through USB TO UART. | Correct board, no repeated reset; CON-02/03/04, QR-10 |
| 2. Display/touch | Configure Demo; test 180° orientation and all four touch corners, Agenda/Day/Month, appointment locations, paging, Today and Settings in both languages. | Stable color display, accessible full list, ≤200 ms feedback; FR-01/12/13/14, QR-01 |
| 3. Indoor probe | Compare at two stabilized 15–30 °C points. Power off before disconnecting/reconnecting the probe; boot after each change. Repeat in enclosure. | Correct error/age, automatic sampling after reconnection; installed ≤0.5 °C target; FR-04 |
| 4. Calendar | Add/edit/delete recurring, overlapping, all-day and midnight events; test both providers and an optional birthday calendar, including source failure and disabling it. | 10-minute polling plus bounded completion per FR-02; immediate manual refresh, offline coalescing, current/next month, location and main-calendar priority; FR-01/02/03/09/14/17 |
| 5. Offline/cache | Disconnect Wi-Fi, restart offline, exhaust coverage; cut power repeatedly during cache writes. | Complete previous/new snapshot or clear unavailable state; never false current; FR-05, QR-02/03 |
| 6. Weather | Enable a named location with its map coordinates; compare source time, °C/°F and model label; block weather host while calendar syncs. | Independent status, stale/expired estimates, correct attribution; FR-11 |
| 7. Setup security | Try home-LAN access, wrong Host/Origin/token and oversized requests. Reopen setup, edit one field, save/reboot and verify other values remain. Test explicit secret clearing, changed network/adapter credentials, failed settings reads, interaction renewal, 15-minute idle expiry and reset. | Unauthorized writes rejected; unique password; confirmed reset; FR-06/08, QR-04/07 |
| 8. TLS/input faults | Expired/untrusted certificate, DNS failure, slow/truncated/chunked/oversized feed. | Bounded recovery, valid cache retained, no secret logs; QR-02/03/04 |
| 9. Night mode | Test start/end, DST change, first-touch wake, 120-second reblank, setup/update override. | Correct blanking, no accidental tap action; FR-03/14, QR-11 |
| 10. OTA | Bootstrap signed USB image, then test local file upload and optional HTTPS. Try signed/unsigned/wrong-key/wrong-target/same-version/oversized files; interrupt upload/download; reset pending candidate. Confirm diagnostics and reuse the matching ELF for a development crash test. | Rejection before boot selection or return to prior valid app; FR-07, QR-04 |
| 10a. Traffic/maps | Configure open car/transit with public test coordinates and a valid contact; test coverage, cancelled transit, unavailable tiles, rapid navigation and Wi-Fi/setup/update interruption. Also test legacy Google with a restricted test key. | Main calendar remains usable; visible map attribution, correct A/B placement and car alignment, clearly approximate transit line, bounded cache/requests, expired results hidden; no route/contact/key logs; FR-15 |
| 10b. Brightness | Tap Settings, drag/save/cancel at both bounds, restart, test failed persistence, idle timeout, night wake and setup/OTA overrides. | Saved preference survives restart, Cancel restores it, failure is visible, underlying controls do not receive modal touches; FR-16 |
| 10c. Responsiveness/resources | Measure touch feedback while decoding worst-case allowed tiles, syncing dense dual calendars and writing flash; log internal/PSRAM heap and stack margins through repeated navigation. | ≤200 ms touch feedback, no unbounded growth or watchdog reset, calendar controls available during map/network failures; FR-14, QR-02/03 |
| 11. Soak and mount | Run 72 h with repeated syncs/touch; measure heap/stack/power peaks, thermal bias and enclosure fit/load. | No unrecovered hang; targets in requirements; FR-10, QR-01/02/03/09/10/11 |
| 12. Usability | Three non-authors perform setup, bridge creation and Wi-Fi changes. | Timing/error-recovery targets in QR-07; document difficulties |

Report template: date; firmware commit/version; SDK and dependency pins; board/panel/sensor revision; instruments and supply; synthetic fixture; exact steps; expected/actual result; measured values; pass/fail; follow-up issue. Never attach real appointment data, private URLs, credentials, dumps or signing keys.

## Mechanical CAD checks

Enclosure A was generated with CadQuery 2.8.0 and checked with Trimesh 5.1.1 on 2026-10-23. These checks cover geometry only; no print, slicer profile, mounted load, actual cable fit or thermal result is claimed.

| Check                                                                  | Result                                                                     |
| ---------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| CAD solids; STL closure, winding and connected parts                   | PASS; one solid per assembly part                                          |
| 200 × 200 mm bed with 3 mm brim                                        | PASS; largest footprint 194 × 153 mm                                       |
| Cover/plate and LCD screw-head clearance during the 12 mm slide        | PASS at 1 mm intervals; zero model intersection                            |
| Cover and wall plate against Waveshare display STEP                    | PASS; zero model intersection                                              |
| Sensor cradle against Adafruit 4885 STEP                               | PASS; zero model intersection                                              |
| USB body/cable design envelopes                                        | PASS against cover and supplier model; actual cable fit remains unmeasured |
| Nominal wall-to-glass depth                                            | 37.5 mm in CAD; physical depth not measured                                |
| Thread engagement, print fit, load retention and installed temperature | NOT TESTED in that historical run                                    |

Reference files are external, not vendored:

- [Waveshare mechanical ZIP](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7.zip): `ESP32-S3-touch-lcd-7.stp`, SHA-256 `a3c83e9fcc35ae0eb7ce2416aa9f702815f84830d920b7d9b2c035267498320c`.
- [Adafruit 4885 CAD](https://github.com/adafruit/Adafruit_CAD_Parts/tree/main/4885%20SHT40%20Sensor): upstream file is named `4485 SHT40.step`, SHA-256 `60957c18a5050994c901ba2509be9483036ecc48616285985a944a6e4c374d57`.

To reproduce the reference-model checks, follow the [CAD setup](../hardware/enclosure.md#edit-or-regenerate-the-models), download those models and run:

```sh
.venv/bin/python hardware/enclosure.py \
  --reference-step '/path/to/ESP32-S3-touch-lcd-7.stp' \
  --sensor-step '/path/to/4485 SHT40.step'
```

The command prints a JSON report with part dimensions, STL hashes, source-model hashes and checked intersections; append `> /tmp/pace-validation.json` to save it. Without the two optional model arguments, only the local part/mesh/mount checks run. The [first-build procedure](../hardware/enclosure.md#fit-boundaries-and-first-build-checks) remains required before claiming FR-10 or installed sensor accuracy.
