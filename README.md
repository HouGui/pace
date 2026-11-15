# PACE

A USB-powered, 7-inch calendar display with category colors, an optional second calendar (such as birthdays), week/day/month views, English/German controls, touch navigation, and indoor/outdoor temperature. Optional Traffic / Verkehr shows named car and public-transport journeys through open routing services, with small OpenStreetMap street maps. Tap Settings for screen brightness; Calendar remains the home screen. Built with ESP32-S3, ESP-IDF and LVGL.

**Development firmware:** software and automated tests are available. Hardware validation is incomplete. Read the [verified status and remaining release gates](docs/verification.md) before using real account data.

Read the [online documentation](https://HouGui.github.io/pace/) or start with the [development guide](docs/development.md): install tools, test without hardware, build, flash, configure and test the board. It links to wiring and enclosure instructions at the relevant steps.

## Guides

| Need | Guide |
| --- | --- |
| Build, configure, test, debug, learn | [Development and usage](docs/development.md) |
| Parts, electrical connections, enclosure assembly | [BOM](hardware/BOM.md) / [wiring](hardware/wiring.md) / [enclosure](hardware/enclosure.md) |
| Behavior and acceptance criteria | [Requirements](docs/requirements.md) |
| Components, ownership and design decisions | [Architecture](docs/architecture.md) |
| Provider setup / custom adapter format | [Adapters](docs/adapters.md) / [feed contract](docs/interfaces/calendar-feed.md) |
| Signed updates, private data and USB recovery | [Security](docs/ota-security.md) |
| Test results and hardware checklist | [Verification](docs/verification.md) |

Contributors: see [contributing](docs/development.md#contributing) and the repository rules in [CONTRIBUTING.md](CONTRIBUTING.md).

## License and third-party components

Original code, CAD and diagrams use the [MIT license](LICENSE). Dependencies are downloaded by their build tools; no third-party runtime source is copied into PACE. The generated Latin font subsets derive from the pinned LVGL Montserrat asset; their [font license](firmware/components/pace_ui/fonts/OFL.txt) is included. Keep their notices when distributing binaries.

| Component | Pin / role | License and notices |
| --- | --- | --- |
| [ESP-IDF](https://github.com/espressif/esp-idf/tree/v5.5.6) | v5.5.6; SDK, FreeRTOS, network, TLS, NVS, OTA | Apache-2.0 and bundled component licenses; preserve SDK notices |
| [cJSON](https://github.com/DaveGamble/cJSON/tree/b2890c8d76bbb64e710585ebc0a917196b9c67e7) | SDK commit `b2890c8d76bbb64e710585ebc0a917196b9c67e7`; also used by host tests | MIT; unchanged external source |
| [LVGL](https://github.com/lvgl/lvgl/tree/v9.4.0) | 9.4.0; GUI, QR encoding and bundled LodePNG decoding | MIT plus bundled notices in `COPYRIGHTS.md`; LodePNG: zlib license; Montserrat font: SIL OFL-1.1; Font Awesome glyphs: SIL OFL-1.1; retain upstream font notices |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools/releases/tag/10.4.0) | 10.4.0; optional browser installer loaded from pinned CDN URL | Apache-2.0; browser/network required |
| [MSAL Node](https://github.com/AzureAD/microsoft-authentication-library-for-js) | 7.2.1; optional Outlook adapter only | MIT; transitive versions/integrities in `adapters/service/package-lock.json` |
| [Waveshare reference](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7/tree/bc51a84b30c0cea018fee6e0d7427ec10102060e) | Reviewed pin/timing/reset reference; no example code imported | Repository Apache-2.0; RGB example carries CC0-1.0 notice |
| [OSRM](https://project-osrm.org/) / [OpenStreetMap](https://www.openstreetmap.org/copyright) | Optional open car routing; no firmware dependency | OSRM engine: BSD-2-Clause; map data: ODbL; [service rules and attribution](docs/adapters.md#community-service-rules-and-privacy) |
| [MOTIS](https://github.com/motis-project/motis) / [Transitous](https://transitous.org/api/) | Optional open transit routing; no firmware dependency | MOTIS engine: MIT; community service/data have separate conditions, including non-commercial use; [details](docs/adapters.md#community-service-rules-and-privacy) |
| [Google Routes](https://developers.google.com/maps/documentation/routes/policies) | Optional traffic service; owner API key/billing | Separate provider terms; Google Maps attribution; see [setup and privacy](docs/adapters.md#traffic) |
| [Open-Meteo](https://open-meteo.com/en/terms) | Optional weather service | Separate service/data terms; free endpoint is non-commercial; show “Weather: Open-Meteo” |

Dependency lockfiles belong in source control. Hardware correctness, provider availability and service rights do not follow from a source-code license.
