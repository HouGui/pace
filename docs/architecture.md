# Architecture

[Requirements](requirements.md) define behavior; [verification](verification.md) records evidence. The domain library has no ESP-IDF, LVGL, provider or operating-system dependencies.

The network worker fetches and validates provider data, stores complete snapshots and transfers them to the UI task. The UI task alone renders through LVGL and handles the display, touch panel and indoor sensor. Protected setup submits bounded commands to the worker.

## Components

| Component | Owns |
| --- | --- |
| `pace_core` | Fixed-capacity model, dates, overlap, ordering, highlights, freshness, night policy, SHT40 CRC/conversion |
| `pace_protocol` | Bounded JSON validation, settings, feed, weather and route translation, URL policy |
| `pace_board` | Reference pin map, RGB timing, shared I²C bus, expander, touch and sensor transactions |
| `pace_services` | Wi-Fi/time, serialized HTTPS, NVS, setup server, update and startup health |
| `pace_ui` | LVGL objects, rendering, touch navigation, backlight schedule and sensor sampling |
| `main` | Composition and startup |

## Optional traffic boundary

Calendar remains primary. `pace_core/traffic.hpp` owns bounded route settings/results and the visible-only request schedule. `pace_protocol/traffic_settings.cpp` validates travel configuration; `osrm.cpp`, `transit.cpp` and `traffic.cpp` translate their respective providers; `route_geometry.cpp` reduces encoded route lines into bounded coordinates; `pace_services/traffic.cpp` handles one HTTPS request per worker iteration; `pace_ui/traffic.cpp` renders the overview/details and `route_map.cpp` presents the street map and endpoint markers; `map_tiles.cpp` decodes bounded tiles through LVGL’s bundled LodePNG. No new runtime, worker task or mandatory backend is introduced.

Results travel by value in the existing health queue; the UI never reads worker-owned mutable data. An atomic visibility flag gates requests, including setup/update overlays. Calendar and weather are serviced before traffic. Leaving the view discards provider results; only user-entered settings persist. Reopening is rate-limited, stale/offline estimates disappear, and failure cannot mutate the calendar snapshot or temperatures. Behavior and timings are owned by [FR-15](requirements.md); provider fields/limits belong in [adapters](adapters.md#traffic).

Open routing uses coordinate pairs and configurable HTTPS endpoints. It fetches once per view opening; Google retains timed refresh. All providers share one bounded result and scheduler. Provider JSON/URLs stay out of the renderer. Transit keeps departure time, a partial-live-data flag and a schematic stop path; OSRM uses decoded overview geometry. Google does not request unused geometry. Health buffers live off the task stacks; no route heap allocation persists between requests.

Street tiles use `pace_core/map.cpp` for bounded Web Mercator projection. The worker owns a fixed PSRAM tile cache and one frame; two single-entry queues transfer that frame to/from the UI. Only the UI owns the RGB565 composite and LVGL objects. It decodes at most one tile per iteration, frees the temporary RGBA buffer immediately, and draws the fixed route into the composite once. Scrolling reuses that image. One completed composite is cached by route and fetch time for reopening; changed routes invalidate it. Image users are deleted before returning the frame. HTTP never runs on the UI task. Allocation/decode/network failure leaves route text usable. Cache/frame storage is approximately 1.3 MiB when open routing is configured; the composite is 80 KiB and temporary decode output is bounded. Exact limits belong in [requirements](requirements.md#calendar-views-and-touch).

## Optional second calendar

Both adapters reuse their provider pagination for a second configured source within the existing overall request deadline. A shared feed helper validates and namespaces secondary events, then appends them only when the complete merged feed fits. Failure preserves the main feed. Opt-in request capability protects older clients. The domain carries only a source flag and bounded display metadata; there is no second device account, endpoint, polling task or cache. Main events sort first and alone drive Now/Next; the renderer uses one configurable accent plus labels/markers. Wire details belong in the [feed contract](interfaces/calendar-feed.md#optional-second-calendar).

## Ownership and scheduling

The main task exclusively owns LVGL and board I/O. It services touch/LVGL every 10 ms, samples SHT40 every 30 seconds and redraws changed views or minute boundaries. The sensor conversion waits 10 ms; I²C transactions time out after 40 ms. No ISR performs networking, allocation or application work.

One network task serializes calendar, weather, traffic and update work. Bounded HTTP/route buffers live on the heap to leave stack space for TLS. Native Kconfig enables verified TLS 1.2/1.3 in fresh and existing builds. Connection/calendar settings remain immutable until a settings save restarts the device. Brightness is a separate non-secret display preference: the UI previews locally, an atomic pending value coalesces saves, and the worker commits a versioned three-byte NVS record before updating the saved value. Failed writes leave the previous value intact. HTTP handlers save validated settings or enqueue one of four pending commands. The setup editor restores retained secrets before full validation; [security](ota-security.md#setup-protection) defines that contract. HTTP handlers share bounded static scratch. Settings parsing uses one bounded heap object. Compiler frame limits protect the small HTTP/network stacks. Setup pauses unfinished station connections and reconnect scans; an established home connection may remain active. A separate atomic flag coalesces manual synchronization while offline/busy. Successful polling uses the requirement cadence; manual requests bypass retry delays once prerequisites are ready.

Two fixed-capacity snapshots exchange ownership through two single-entry queues. The worker parses into the free snapshot, persists a complete valid response, then publishes its pointer. The UI returns its previous snapshot before the next fetch. Invalid responses never replace the active snapshot. A separate one-entry queue coalesces health updates.

The UI task and network task register with the task watchdog. Both must keep making progress; a 60-second stall triggers recovery. The boot health check also requires a recent UI heartbeat. Network availability and sensor presence do not decide OTA boot validity.

Local signed uploads pass through two fixed 2 KiB queue entries from the existing HTTP server to the network worker. The worker exclusively writes the inactive app slot, checks exact size and descriptors, then invokes SDK signature verification before boot selection. One update request owns the channel until completion; uploads have a five-minute deadline and cancellation signal. HTTPS updates use the same image writer plus manifest/hash/origin checks. Local uploads do not require network time because they do not use TLS. See [security and recovery](ota-security.md).

## Time and persistence

UTC is established by SNTP. A cold boot does not trust the last cached wall clock. Large observed clock jumps invalidate trust until another synchronization; SNTP itself is unauthenticated.

Feed v2 supplies local-day intervals **and UTC offset segments** for the entire cached window. This handles daylight-saving changes inside a day without shipping a time-zone database to the MCU. Outside that coverage, local clock/night scheduling are unavailable. See the [contract](interfaces/calendar-feed.md).

Settings and the last complete feed use versioned JSON in separate NVS partitions. The feed blob has a fixed 36-byte `PC02`/SHA-256 source header, committed together with its JSON. The header binds the cache to endpoint, pairing token and transport profile, so changing accounts cannot show another source's old agenda. This avoids persisting compiler-dependent C++ layouts. NVS commits are checked; boot reads are validated again. Unknown formats never trigger automatic erasure. A failed cache write leaves the new RAM view usable with a storage fault indication. Power-loss behavior still requires the hardware test.

An encryption-enabled build opens both NVS partitions through the same SDK security scheme and fails closed on key errors. Provisioning and recovery policy live in [security](ota-security.md#device-data-profiles).

## Memory and failures

- The domain holds at most 128 events, up to 62 days, eight categories and 32 clock segments.
- JSON input has byte, depth, string and node limits. Parsers reject duplicate keys/IDs and invalid references before publication.
- RGB uses one 768,000-byte PSRAM framebuffer, two internal DMA bounce buffers and a 64,000-byte partial LVGL draw buffer. Single buffering can tear during redraw; stability/readability must be measured.
- Network body storage is allocated once. Parser allocations are bounded by input limits and released after each response. The day view pages eight events at a time and the month grid has at most 42 cells. Appointment details use one selected event. Calendar objects rebuild only for navigation, new snapshots and minute/time changes; temperature and health updates reuse their labels. Snapshot replacement remaps the selected date and event ID before returning the old snapshot. LVGL uses the SDK C allocator rather than a separate fixed heap.
- Peripheral/HTTP errors retain usable data. Retries use bounded exponential backoff with jitter. Periodic health logs contain resource counts, never appointments or credentials.
- The [partition table](../firmware/partitions.csv) retains two 3 MiB app slots and a 256 KiB cache within 8 MiB flash.

## Dependencies and build boundaries

Keep native ESP-IDF services, SDK cJSON and pinned LVGL as the firmware runtime. PNG decoding uses LVGL’s bundled LodePNG; no separate map SDK, Arduino layer or runtime framework is added. Enable only used GUI widgets. The UI component declares mandatory widgets, fonts and PNG decoding through native Kconfig selections, so older saved configurations also satisfy the compiled code’s dependencies. Component `REQUIRES` entries describe public headers; implementation-only SDK components use `PRIV_REQUIRES`. The optional Outlook service uses Node built-ins plus MSAL. Host testing and CAD/font tools are development-only. Exact versions/licenses belong in [README](../README.md#license-and-third-party-components); retain reviewed pins and lockfiles when updating.

## Display orientation and language

The reference enclosure uses `CONFIG_PACE_GUI_ROTATE_180=y`. The flush callback reverses each packed RGB565 partial buffer in place and rotates its destination rectangle. LVGL transforms touch coordinates using the same display rotation; board touch output stays raw. No additional framebuffer is allocated. The bench orientation can be selected in the PACE build menu.

Control language is an immutable English/German setting independent of the adapter's date-label locale. Existing settings infer it from the locale. A bounded Latin-1 font subset supplements the pinned Montserrat fonts; unsupported text remains UTF-8 and is flagged. Numerical temperature values use the selected language's decimal separator.

## Power

Permanent USB power is the baseline. Night mode switches the expander's backlight output; Wi-Fi, synchronization and touch continue. A wake touch is consumed, then the screen stays on for 120 seconds. Setup/update interaction prevents blanking. A 20–100% LVGL black overlay provides software dimming; it does not PWM the CH422G backlight-enable output or reduce its power. The overlay cannot receive touch events and is hidden for setup/updates. Brightness controls prevent scheduled blanking while active; cancelling or leaving them idle restores the saved value. Deep sleep and battery operation are deferred.

## Design decisions

| Choice | Reason |
| --- | --- |
| Integrated color touch display, bottom-mounted SHT40, USB power | Readable category colors and simple assembly; battery operation is deferred. |
| Native ESP-IDF and one LVGL task | Use the board's established SDK without another runtime or GUI synchronization layer. |
| Provider adapters and civil-time segments in the feed | Keep account APIs and time-zone databases out of the device while supporting DST. |
| Optional sources share the main feed | Preserve main-calendar priority without another device account or polling stack. |
| User-triggered open routing and bounded street maps | Avoid mandatory paid APIs and limit community-service load and device memory use. |
| Software brightness | The reference board exposes backlight enable, not a supported PWM brightness connection. |
| Protected setup edits with retained secrets | Small changes preserve existing settings; credentials are not returned to the browser. |
| Signed local uploads and optional HTTPS updates | Routine OTA needs no hosting service; both paths share signature verification and rollback. |
| Stock SDK builds plus a small preparation helper | Reuse standard flashing tools, enforce the board/SDK target and preserve saved configuration. |
| Verified TLS 1.2/1.3 and bounded heap scratch | Support current routing hosts while leaving task-stack space for TLS and error handling. |
