# Security, updates and recovery

The default build is for development. TLS 1.2/1.3 certificate and hostname verification is enabled; **OTA is disabled unless signed-image verification is compiled in**. Physical security, encrypted storage and recovery must be provisioned/tested before real household use. Evidence belongs in [verification](verification.md).

## Setup protection

Setup opens only on first boot or a deliberate two-second Settings hold. Each session has a random WPA2 password and CSRF token. Requests must arrive on the setup interface with the expected Host; state-changing requests also require the exact Origin, route-specific content type (`application/json`, or `application/octet-stream` for firmware uploads) and CSRF header. The portal is unavailable through the home-LAN interface, does not return stored secrets, loads no third-party scripts, and closes after the idle interval defined in [FR-06](requirements.md#functional-requirements).

Reading saved settings also requires the session CSRF token and returns `Cache-Control: no-store`. Wi-Fi passwords, pairing secrets, API keys and private calendar/update URLs are replaced with `null`, meaning “keep the stored value” in an edit. The server restores those values before validating and saving the complete configuration. Explicit strings replace or clear them. A retained Wi-Fi password cannot be applied to a different SSID, nor a retained pairing secret to a different adapter URL/profile. Failed reads or validation leave stored settings untouched. No browser local storage is used.

WPA2 protects the wireless link. The portal itself uses local HTTP, not publicly trusted HTTPS. Anyone with the setup password can configure the device while it is open; protect physical access. Reset requires typing ERASE and confirming. Invalid settings and failed writes do not erase the old configuration.

Traffic API keys, route coordinates/addresses, server URLs and client contacts are private settings protected by the same storage profile as Wi-Fi/calendar credentials. The protected setup editor returns route coordinates/addresses, server URLs and contacts for editing, but never stored API keys. None of these values are logged. Provider estimates are transient; see [traffic data handling](adapters.md#traffic).

## Device-data profiles

- **Development:** synthetic appointments and disposable credentials. NVS/cache are unencrypted; no Secure Boot or eFuse changes. USB recovery remains straightforward.
- **Household:** enable the SDK's `CONFIG_NVS_ENCRYPTION` with a provisioned key-protection scheme. Firmware initializes both settings and calendar NVS with that scheme; a security error never falls back to plaintext. Provisioning and physical recovery still require owner review and board tests. The `nvs_keys` partition is marked for encryption, but this flag alone does not enable protection.

Review the pinned SDK's [NVS encryption guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/api-reference/storage/nvs_encryption.html) before selecting HMAC key storage or flash encryption. Enabling these options can cause the SDK to program eFuses on first boot. Do not flash such a build until the owner approves that exact provisioning/recovery procedure. Plaintext development data is not automatically migrated into encrypted storage.

Burning eFuses can permanently change recovery access. It requires the owner's explicit authorization and is excluded from build/test scripts. Do not load real calendar/Wi-Fi secrets into an unprotected device and assume `.gitignore` or TLS encrypts its flash.

SNTP is unauthenticated. Firmware checks plausibility and observed jumps, but cannot defeat an attacker controlling time synchronization. Cached time is not trusted after a cold boot.

Secondary calendar IDs and labels are owner configuration in the adapter; names/dates in its events are private calendar data and follow the same encrypted-cache policy as main events. Street-map servers receive tile locations and the client header. Tiles are RAM-only; exclude live route/map captures from public test fixtures.

## Signed application updates

Updates change only the inactive application slot. They never write the bootloader, partition table or eFuses.

Use the [one-time preparation and update workflow](development.md#firmware-updates-for-users-and-developers) for commands. A signing key is required; an update host is optional. The default unsigned firmware must first be replaced through USB with an update-capable signed build. Subsequent updates must verify against that same key. The preparation tool generates no hardware-security provisioning commands and never flashes or publishes.

There are two explicit local actions:

- **Signed-file upload:** join protected setup Wi-Fi, choose the signed application `.bin` in Diagnostics and confirm. The HTTP server streams through two fixed 2 KiB chunks to the worker, with a five-minute deadline. Authentication uses the setup interface/Host/Origin/CSRF checks and WPA2 session password. Home Wi-Fi, trustworthy time and hosting are unnecessary. Exact length, project descriptor and a changed version are required; the SDK checks the target chip, image and signature before boot selection.
- **HTTPS manifest:** configure a manifest URL in setup, or use the optional build-menu default. Wi-Fi and trustworthy time are required. The bounded manifest contains schema version `1`, `board_profile`, `version`, `image_url`, integral `size` and lowercase hexadecimal `sha256`. `prepare_ota.py --image-url ...` generates it from the verified signed binary. The expected board profile is `waveshare-esp32s3-touch-lcd7-800x480`. Binary and manifest must share the exact HTTPS origin; redirects, wrong targets, oversized images and bad hashes are rejected.

Both paths use the same bounded image writer. `esp_ota_end` performs SDK image/signature verification; only success permits `esp_ota_set_boot_partition`. An interrupted transfer aborts its write handle without changing the selected boot slot. Updates are unavailable when signature verification is absent, including development builds. Progress and the last non-sensitive result code are available on screen/in setup diagnostics; signatures or TLS are never bypassed for debugging.

This profile protects network updates, not physical reflashing or stored credentials. It intentionally permits a different previously signed version for development rollback; irreversible security-version anti-downgrade eFuses are not used. Keep the signing key outside the repository and every shared package. Losing it requires trusted USB recovery with a new key. Review the pinned SDK's [signing-without-Secure-Boot guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/security/secure-boot-v2.html) before provisioning a different profile.

## Boot health and rollback

After 30 seconds of successful storage access and recent UI progress, a pending candidate is marked valid. Calendar access, Wi-Fi and sensor presence are not boot-health requirements. A failed candidate requests rollback; resetting before confirmation also rolls back if a previous valid slot exists. A healthy first USB installation initializes valid OTA metadata so it can become that fallback. Failed metadata writes are retried; they are not treated as successful confirmation. A fallback must reject an incompatible cache and never display partial data as current.

If there is no previous valid image, USB recovery is necessary. Incompatible settings/feed data is rejected without automatic erasure. When changing firmware formats, confirm that the fallback can read the saved settings; otherwise USB recovery may be required. Brightness has its own versioned key and does not change the settings/feed schema. Power-cut and rollback behavior still requires the [board tests](verification.md#board-workflow).

## USB recovery

1. Connect the **USB TO UART** port with one USB power source.
2. Close other serial applications. Hold **BOOT**, tap **RESET**, then release BOOT.
3. Flash a known compatible image using the [build/flash commands](development.md#first-build-and-flash). A normal flash does not intentionally erase settings/cache, whereas the [merged factory installer](development.md#local-install-package) resets settings and OTA metadata.
4. If settings need removal and the display works, use the confirmed local reset. `idf.py -p PORT erase-flash` is a last resort: it deletes credentials/cache and requires a full reinstall.

Verify recovery under the exact provisioned security profile before deployment; encrypted/Secure Boot devices may restrict these steps. Keep keys and private dumps out of issues and release archives.
