# Calendar feed — v2

Implemented device-facing format. [Example](calendar-feed.example.json) contains synthetic data. Version 2 replaces the undeployed v1 draft by adding clock-offset segments; v1 is rejected.

## Transport

The device sends an HTTPS POST:

```json
{"schema_version":2,"device_token":"<paired-secret>","month_view":true,"secondary_calendar":true,"preferences":{"time_zone":"Etc/UTC","locale":"en-GB","hour_cycle":"h23"}}
```

`month_view` is an optional boolean request capability. Supplied adapters return 62 consecutive dates beginning on the first of the generation month when true, covering the current and following month. Omitted/false keeps the legacy 14 dates beginning today. Updated firmware accepts 14–62 dates, so older adapters/caches remain readable; older firmware must not request month coverage.

Preferences are owner configuration, not fixed defaults. The adapter validates IANA zone, locale and `h12`/`h23`, authenticates the secret before accessing the provider, and selects the calendar from its own configuration.

TLS validates the chain and hostname using the SDK certificate bundle. Request ≤2 KiB; response ≤65,536 bytes, including chunked bodies. Firmware uses 5-second I/O timeouts and a 60-second synchronization deadline checked between operations; DNS/connection behavior and slow-response recovery need the hardware tests. The watchdog provides a separate 60-second recovery limit. The [Google-only redirect exception](../adapters.md#transport-details) never forwards the POST body. Generic feeds reject redirects and encoded/compressed bodies.

## Success

| Field | Meaning |
| --- | --- |
| `schema_version`, `ok` | Integer `2`, boolean `true` |
| `generated_at_utc` | Integral Unix seconds when the complete snapshot was generated |
| `time_zone`, `locale`, `hour_cycle` | Must exactly match the requested preferences |
| `days` | 14–62 consecutive civil dates; generation must fall within them with at least seven dates remaining (including that day) |
| `clock` | Contiguous offset segments covering exactly the same UTC interval |
| `categories` | Up to eight unique `{id,label}` entries |
| `events` | Complete occurrences overlapping the window; an empty array is valid |

A day is `{date,label,start_utc,end_utc}`. Dates use `YYYY-MM-DD`; UTC ends are exclusive. Adjacent days meet exactly and must be consecutive local dates. Supported day duration is 20–28 hours, covering ordinary and half-hour DST changes. The on-device validation horizon is 2020–2099.

A clock segment is `{start_utc,end_utc,offset_seconds}`. Ends are exclusive; segments are ordered, contiguous and nonempty. Offset is an integral number of seconds in −50,400…50,400. Use a new segment at the exact UTC transition instant. The device adds the matching offset only within that segment. It validates clock/day agreement at both ends of every day/segment overlap, including transitions inside a day. Adapters own the IANA rules used to produce those segments.

The current view needs seven complete future dates. When fewer remain, the cache is expired. Untrusted time suppresses highlights and night blanking. A cold-boot cache is not evidence of trustworthy time.

## Events

Common fields: unique stable occurrence `id`, plain UTF-8 `title`, optional plain UTF-8 `location` (empty/omitted means unspecified), boolean `all_day`, optional `category_ids` with up to four distinct IDs present in the category table.

| Variant | Required fields | Rule |
| --- | --- | --- |
| Timed | `start_utc`, `end_utc`, `start_label`, `end_label` | Start < exclusive end; overlap every applicable day. Labels include an offset when local hours repeat. The UI retains these labels in day/details; compact week rows and the next-event banner omit GMT/UTC suffixes. Header time/offset comes from `clock`, not from event labels. |
| All-day | `start_date`, `end_date` | Start < exclusive civil end date; never convert through UTC midnight on the device. |

Fields of the other variant must be absent. The adapter expands recurrence/exceptions, removes cancelled instances, includes events already in progress at window start and rejects incomplete pagination. The device sorts main-calendar events before secondary events, then all-day before timed events, timed starts and IDs for deterministic ties. Category labels accompany colors in the detail view.

## Optional second calendar

The request capability `secondary_calendar: true` permits a second source. Omitted/false keeps main-only output, including for older firmware. Supplied adapters include this response metadata only when the optional source is configured:

```json
"secondary_calendar": {"label": "Birthdays", "available": true}
```

`label` is plain UTF-8, at most 32 bytes; empty uses a localized generic name. Secondary events carry `"secondary": true`; absent/false means main. Secondary IDs must be stable and distinct from main IDs (supplied adapters namespace before hashing). Category mapping remains for the main source; secondary accent is device configuration. Both sources share the existing window, preferences and total limits; recurrence and exclusive all-day dates work identically.

A failed, malformed or oversized optional source contributes **no** events and sets `available: false`; main events remain complete. If even the unavailable metadata would exceed the response byte limit, the adapter sends the complete main feed without it. Secondary events without configured/available metadata are invalid. Settings, secrets and calendar IDs are not included in this metadata. Main-source failure remains a normal feed error. This is an additive v2 extension; cache validation and snapshot ownership are unchanged.

## Bounds

| Item | Maximum |
| --- | ---: |
| Response / request bytes | 65,536 / 2,048 |
| Events / days / clock segments | 128 / 14–62 / 32 |
| Event ID / title / location UTF-8 bytes | 96 / 128 / 192 |
| Category ID / label UTF-8 bytes | 32 / 32 |
| Categories / categories per event | 8 / 4 |
| Day label / time label UTF-8 bytes | 64 / 32 |
| Time zone / locale bytes | 64 / 32 |
| Container nesting / parsed nodes | 8 / 4,096 |
| Configured endpoint / temporary redirect bytes | 512 / 2,048 |

Duplicate JSON keys/IDs, unknown category references, invalid UTF-8/control characters, embedded NUL, nonintegral/nonfinite times, invalid dates, inconsistent intervals, mixed variants and limit violations are rejected. Unknown extra fields may be ignored only within these limits. A response outside the live freshness window (over five minutes ahead or over 30 minutes old) is not accepted as a successful refresh.

Validation uses a scratch snapshot. Failure never overwrites the active view or saved good feed. Capacity overflow is an error, not a truncated list of appointments. UI overflow is separate.

## Errors

```json
{"schema_version":2,"ok":false,"error":{"code":"access_denied"}}
```

Allowed codes: `access_denied`, `reauthorization_required`, `capacity_exceeded`, `unsupported_preferences`, `temporarily_unavailable`, `invalid_request`. Do not include stack traces or account details. HTTP 200 can still carry an error, especially from Apps Script. Empty `events` in a valid success is distinct from an error.
