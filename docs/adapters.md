# Calendar, weather and travel adapters

The device reads one [bounded HTTPS feed](interfaces/calendar-feed.md). The adapter combines the main calendar and an optional second calendar; account access and recurrence expansion stay there. Google and Outlook use the same firmware and renderer.

| Option | Runs where | Extra requirement |
| --- | --- | --- |
| Demo | Device | Wi-Fi/time; synthetic events in UTC |
| Google | Google Apps Script | Owner deployment and read-only authorization |
| Outlook / Microsoft 365 | Optional Node service | A maintained HTTPS host and Microsoft application registration |
| Another provider | Your adapter | Produce the v2 contract; pass the feed checker |

The optional Outlook host does not change the server-free Google baseline. Hosted accounts and tenant policies must be tested separately; automated tests use synthetic responses.

## Google Calendar

Complete these steps on a computer with internet access, using the Google account that can read your chosen calendar. In Google Calendar, open that calendar's **Settings and sharing → Integrate calendar** and copy **Calendar ID**. Use the ID, not its display name or an iCal URL; keep the calendar private.

1. Create a standalone project in [Google Apps Script](https://script.google.com/).
2. Paste [Code.gs](../adapters/google/Code.gs) into `Code.gs`. Add a script file named **Feed** and paste [feed.js](../adapters/feed.js). No changes inside either source file are needed.
3. In Project Settings, show the manifest. Replace `appsscript.json` with [the supplied manifest](../adapters/google/appsscript.json). It enables Calendar v3 and requests only `calendar.events.readonly`.
4. Generate a pairing secret locally:

   ```sh
   python3 -c "import secrets; print(secrets.token_urlsafe(32))"
   ```

   Copy it privately; never commit it or paste it into an issue.
5. In **Project Settings → Script properties**, add:

   | Property | Value |
   | --- | --- |
   | `CALENDAR_ID` | `primary`, or the calendar ID from Google Calendar settings |
   | `DEVICE_TOKEN` | The generated secret |
   | `CATEGORY_MAP` | Optional JSON mapping, described below |
   | `SECONDARY_CALENDAR_ID` | Optional second calendar ID accessible to this account; blank disables it |
   | `SECONDARY_CALENDAR_LABEL` | Optional display label, e.g. `Birthdays`; maximum 32 UTF-8 bytes |

6. Select and run **authorize** in the editor. Review the read-only permission request. This checks owner access without logging appointments.
7. Choose **Deploy → New deployment → Web app**. Execute as **Me**; allow **Anyone** to reach the endpoint. The adapter authenticates every POST before querying Calendar; the calendar itself remains private.
8. Copy the deployment's `/exec` URL (not `/dev`). Follow [device setup](development.md#device-setup), keep the loaded home Wi-Fi settings, choose **Google Apps Script**, and paste that endpoint and the same `DEVICE_TOKEN` as the pairing secret. Confirm your time zone, then **Preview settings → Save and restart**. No reflash is needed. Opening the endpoint directly in a browser does not test PACE's authenticated POST request.
9. Add, edit and cancel a synthetic recurring appointment. Confirm the changes reach the display within the requirement's poll window.

If your organization disallows public web-app endpoints or authorization, ask its administrator or choose another permitted adapter. Do not publish the calendar as a workaround. When changing code, update the deployment version. Rotate an exposed secret in both Script Properties and device setup.

### Updating an existing bridge

For month coverage, locations and the optional second calendar, replace both **Code.gs** and **Feed** with the current supplied files, then choose **Deploy → Manage deployments → Edit → New version → Deploy**. Retain Script Properties and the existing `/exec` URL. Tap **Sync now** on PACE. An older bridge remains readable but its unloaded month dates and absent locations cannot be reconstructed by firmware.

Google's event `location` and Outlook's `location.displayName` become the plain-text location. Adapter code converts address line breaks/tabs to spaces and enforces the contract's UTF-8 byte limit. Descriptions, attendee lists and coordinates are not copied.

### Optional second calendar

Use a separate calendar you can read, such as birthdays, anniversaries or reminders. PACE reads existing occurrences; create recurring dates in your calendar app. Do not infer birthdays from contacts or publish a private calendar.

For Google, set the two `SECONDARY_CALENDAR_*` Script Properties above, run **authorize**, and update both scripts and the deployment version. For Outlook, the configuration wizard asks for the second calendar ID, label and optional source time zone; existing installations can set `secondary_calendar_id`, `secondary_calendar_label` and `secondary_calendar_time_zone` in private `adapter.local.json`, then restart the service. Blank ID disables it. Use a different calendar from the primary source; identical IDs are ignored.

Set **Second calendar highlight** in PACE's setup page to choose its accent. No second device endpoint or secret is required. The adapter keeps the main feed if the second calendar fails or cannot fit completely; the device shows an unavailable notice. Main errors still use the normal cached-calendar behavior. A feed already at the byte limit may lack room for that notice. See [view priority](requirements.md#calendar-views-and-touch) and the [wire contract](interfaces/calendar-feed.md#optional-second-calendar).

### Category mapping

An optional `CATEGORY_MAP` value maps Google event colors to generic IDs/labels:

```json
[{"provider_id":"2","id":"team","label":"Team"}]
```

Choose the corresponding ID/color in the device wizard. No names or email domains are inferred. Up to eight mappings and four categories per occurrence are supported. Unmapped events stay neutral. A title or complete response that exceeds the contract returns a capacity error instead of silently dropping appointments.

### Transport details

Apps Script Content Service redirects once from `script.google.com` to `script.googleusercontent.com`. The device permits only this HTTPS host pair, only in the Google transport profile, and changes the second request to GET without the pairing body. Generic feeds and updates reject redirects. Neither temporary URLs nor request bodies are logged.

Calendar queries use expanded instances, the complete window requested by the device and bounded pagination; window choices and limits are defined in the [feed contract](interfaces/calendar-feed.md). Google quotas and service availability still apply. References: [web-app deployment](https://developers.google.com/apps-script/guides/web), [Content Service](https://developers.google.com/apps-script/guides/content), [events.list](https://developers.google.com/workspace/calendar/api/v3/reference/events/list).

## Outlook / Microsoft 365

This optional adapter uses Microsoft's MSAL library and delegated **Calendars.ReadBasic** permission. It never stores Microsoft tokens on the display.

1. In the [Microsoft Entra admin center](https://entra.microsoft.com/), register an application for the account types you need. Under Authentication, enable **Allow public client flows**. Add Microsoft Graph's delegated `Calendars.ReadBasic` permission. No client secret is needed.
2. Prepare an HTTPS hostname/certificate trusted by the device's CA bundle. Use your existing host/reverse proxy; do not expose an unmaintained development computer. Hosting/deployment is an owner operation, not part of the firmware build.
3. On that host, install a supported Node LTS, then:

   ```sh
   cd adapters/service
   npm ci --ignore-scripts
   npm run configure
   ```

4. Enter the application ID, the mailbox calendar's **IANA time zone**, and TLS certificate/key paths. The wizard creates private `adapter.local.json`. Open that local file to set `bind`/`port`, review the main/optional second calendar IDs, and copy `device_token` into PACE. The default binding is loopback, not public.
5. Run `npm run login`. Follow the Microsoft sign-in instruction shown in your terminal. This saves a private refresh-token cache; tenant policy may require administrator consent or disallow device-code sign-in.
6. Run `npm start` under your host's normal service manager. Route the chosen HTTPS endpoint to `/feed`. Keep configuration, token cache and TLS keys readable only by that service account; on Unix use mode `600`.
7. In PACE choose **Calendar feed** and enter your HTTPS `/feed` endpoint and pairing secret. Test a recurring event, a cancellation and an all-day event before regular use.

`calendar_id` empty selects the signed-in user's default calendar. `category_map` uses the same mapping shape as Google, with Outlook category names as `provider_id`. `calendar_time_zone` restores all-day civil dates from Graph's UTC output; it must match the source calendar. Calendars mixing unusual all-day source zones need explicit validation before use.

The service permits one request at a time, bounds provider pages/bytes and rejects pagination links outside Microsoft Graph before attaching tokens. Authentication failure requires `npm run login` again. Back up only private configuration using your normal secure process; do not commit or include it in release packages.

References: [Graph calendarView](https://learn.microsoft.com/en-us/graph/api/calendar-list-calendarview?view=graph-rest-1.0), [device-code authentication](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code), [MSAL Node](https://github.com/AzureAD/microsoft-authentication-library-for-js/tree/dev/lib/msal-node).

## Custom adapters

Implement the [v2 feed](interfaces/calendar-feed.md), expand recurring occurrences upstream and remove cancellations. Return a complete window or an explicit error. Do not send attendee lists, descriptions or conference links.

Validate synthetic output using the [host tests and feed checker](development.md#without-a-connected-board). Adding a provider must not require changing `pace_core` or `pace_ui`.

## Weather

The optional Open-Meteo adapter requests current `temperature_2m` in Celsius with UTC Unix time. Configure a location label and coordinates in setup. It polls independently from calendar state, labels estimates as Outside, and ages stale/unavailable results according to [FR-11](requirements.md).

The service sees coordinates and the device's IP. Its free endpoint is for non-commercial use; attribution is required. The Outside card is marked as a model estimate and the footer attributes **Open-Meteo**. Review [API documentation](https://open-meteo.com/en/docs) and [service/data terms](https://open-meteo.com/en/terms) before commercial distribution. The location label is display text; latitude/longitude select the forecast grid cell. Copy decimal coordinates from a map (latitude first, longitude second). City lookup is not implemented. Validate a captured response with `build-host/pace_feed_check --weather /path/to/weather.json`; stale, wrong-unit or malformed samples are rejected.

## Traffic

Choose **Open routing** for car and public-transport journeys without an API key or billing account. These are independent community services, not a guaranteed free replacement for every Google Maps feature.

| Service | Supplies | Limits |
| --- | --- | --- |
| OSRM with OpenStreetMap | Car route, distance and estimated driving time | No live congestion; route preview is simplified |
| Transitous with MOTIS | Public-transport journeys, walking connections and available live updates | Coverage depends on local feeds; some or all legs may use only a timetable |
| Google Routes (optional) | Traffic-aware car estimates and transit journeys | Owner API key, billing and provider terms; existing settings keep this provider |

### Set up open routing

1. Hold **Settings / Einstellungen** for two seconds, join the displayed setup Wi-Fi and open `http://192.168.4.1`.
2. Keep the loaded Wi-Fi/calendar settings; blank saved private fields retain their values. Enable **Traffic** and choose **Open routing (no key)**.
3. Add one to four named journeys. Enter both start and destination as `latitude, longitude`, following the [coordinate examples](development.md#coordinate-format). PACE does not search for addresses in open-routing mode.
4. Select **Car** or **Public transport** for each journey. For transit, enter a contact email or public project website. Transitous requires an identifying client contact; PACE sends it in the `User-Agent` header with its name/version. Use a project contact if available. It is private device configuration, never a source-code constant.
5. Keep the default servers, or enter compatible HTTPS servers under **Advanced routing servers**. Preview, save and restart. Open **Traffic / Verkehr**; tap a journey for its details and geographic preview. Scroll to read advisories.

**Refresh:** open routing fetches each configured route once when the view opens; there is no automatic background or continuous transit polling. Reopen the view to refresh, subject to the [FR-15 limits](requirements.md). A quota/access failure needs correction or waiting, not repeated tapping. Calendar and weather remain independent.

**What the display means:** car estimates exclude current traffic jams. Transit shows journey duration, the local departure time when calendar clock coverage permits, and whether any leg has live updates. Live data on one leg does not establish live coverage for the whole journey. Total transit distance is unavailable because MOTIS reports distance only for non-transit legs. Advisory headlines are shown; consult the operator for full notices before travelling.

### Geographic previews

The open-routing detail page shows a small standard, north-up street map from OpenStreetMap, with a blue route line and A/B endpoints. OSRM supplies a simplified road line. Transit connects returned walking/boarding/alighting points; its line is explicitly approximate, not road/track alignment. Missing geometry or tiles produces a clear unavailable message while travel details remain usable. The map is visible on opening the detail; scroll for the remaining text.

Tiles download only for the selected viewport. The bounded RAM cache, deadlines and decode limits are defined in [requirements](requirements.md#calendar-views-and-touch). There is no prefetch, bulk/offline download or map SDK. Google estimates remain available, but their geometry is not drawn on an OpenStreetMap background; Google requires its routes on a Google map.

### Community service rules and privacy

- The default OSRM server is `https://routing.openstreetmap.de/routed-car`. [FOSSGIS service rules](https://routing.openstreetmap.de/about.html) require attribution, an identifying user agent, no scraping/heavy use and at most one request per second. PACE serializes and spaces all route requests. Requests may be retained in server logs.
- The default transit endpoint is `https://api.transitous.org/api/v6/plan`. [Transitous policy](https://transitous.org/api/) permits light, open-source, non-commercial use on a best-effort basis. Contact the operator before significant use, many devices or automated workloads; its policy identifies more than 720 planning requests per day as heavy use. PACE does not contact the operator for you. Source conditions are listed at [Transitous sources](https://transitous.org/sources/); [OpenStreetMap attribution](https://www.openstreetmap.org/copyright) also applies.
- Street tiles use `https://tile.openstreetmap.org/{z}/{x}/{y}.png`. PACE sends an identifying user agent, caches for seven days until bounded-cache eviction/reboot, and displays attribution on the map. Review the [OSM tile policy](https://operations.osmfoundation.org/policies/tiles/); public capacity is not guaranteed. `tile_endpoint` can select a compatible 256-pixel PNG XYZ server with compatible attribution requirements.
- These servers receive coordinates or map tile locations, public IP and the client header. Route names stay on the device. Google instead receives the configured addresses. Estimates are discarded when leaving Traffic; map tiles remain only in a bounded RAM cache until eviction/reboot; route settings follow the [device storage profile](ota-security.md#device-data-profiles). No provider response, contact, key or route is logged by PACE.
- Self-hosting OSRM/MOTIS gives control over service availability and logs, but needs a maintained server, HTTPS certificate and applicable map/transit datasets. It is optional and is not cost-free infrastructure. Review the chosen data/service licences before commercial distribution.

### Settings and parser boundaries

The optional `traffic` object in [device settings](device.example.json) contains `enabled`, `provider` (`open` or `google_routes`) and `routes`. Each route has `name`, `origin`, `destination`, and `mode` (`car` or `transit`). Open routing accepts coordinate pairs; Google accepts complete addresses including city/country. Bounds are UTF-8 bytes: name 48, endpoint text 128, key/contact 128, server URL 256. Open settings use optional `road_endpoint`/`transit_endpoint`/`tile_endpoint` defaults and `contact` (required for transit). Google uses `api_key`. The setup editor retains disabled-feature fields for later editing; open routing never uses the Google key. Stored keys stay hidden in the editor. An enabled legacy object without `provider` retains Google; omission disables Traffic.

Route requests use verified HTTPS, a ten-second deadline, no redirects, and a 32 KiB response limit. Tiles have a separate 64 KiB limit and accept only bounded 256 × 256 PNGs; no compressed metadata or animated images. JSON depth is 10 for road/Google and 12 for MOTIS; the calendar limit is unchanged. MOTIS accepts up to eight itineraries and 32 legs, chooses the earliest arrival among non-cancelled results, and rejects malformed or oversized replies. Provider errors never replace calendar/weather state. Open endpoints accept no embedded credentials or query parameters. Protocol reference: [OSRM v5 API](https://project-osrm.org/docs/v5.24.0/api/) and [MOTIS schema v2.11.4](https://github.com/motis-project/motis/blob/v2.11.4/openapi.yaml).

Use `build-host/pace_feed_check --osrm /path/to/route.json` or `--transit /path/to/route.json` to validate a synthetic/captured public-coordinate response. Do not put real journeys or contacts into fixtures. Live-provider evidence belongs in [verification](verification.md).

### Optional Google Routes

Follow Google's [Routes API setup](https://developers.google.com/maps/documentation/routes/get-api-key), enable billing, and create a dedicated key restricted to Routes API with a daily quota. A stable public egress IP can also be restricted; browser-referrer restrictions do not suit device requests. Select **Google Routes** in setup and enter that key. Review [billing](https://developers.google.com/maps/documentation/routes/usage-and-billing), [Routes policies](https://developers.google.com/maps/documentation/routes/policies), [Maps terms](https://maps.google.com/help/terms_maps/) and [privacy](https://policies.google.com/privacy), including applicable EEA rules, before use/distribution.

The direct adapter sends the key in an HTTPS header and requests bounded duration, distance, overview geometry, descriptions, warnings and transit line names. Car uses `TRAFFIC_AWARE`; transit uses `TRANSIT`. The display attributes Google Maps. This option requires no extra server but may incur charges. No Google Routes key is needed for open routing, Calendar or Open-Meteo.
