# Contributing to PACE

Read [README](README.md) and the [requirements](docs/requirements.md). Keep pull requests focused; explain what changes and how you checked it.

- Use C17/C++17, the pinned ESP-IDF SDK and `.clang-format`. Preserve dependency pins, lockfiles and third-party notices.
- Follow the [architecture](docs/architecture.md): testable domain code, bounded inputs/waits, explicit ownership and one LVGL task. Add dependencies only when needed.
- Add tests for changed behavior and error paths. Run the [documented checks and firmware build](docs/development.md#without-a-connected-board); record actual results and untested hardware in [verification](docs/verification.md).
- Update the document that owns the change instead of duplicating instructions. Keep wiring diagrams/pins and CAD/STL files consistent.
- Use synthetic test data. Review staged files; never commit credentials, private URLs, personal data, signing keys or device dumps. Keep TLS verification enabled.

Releases and deployments are maintainer actions. Follow the [security guide](docs/ota-security.md); eFuse provisioning requires the device owner's explicit approval.
