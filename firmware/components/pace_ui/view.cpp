// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "pace/calendar_view.hpp"
#include "pace/core.hpp"
#include "ui_state.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pace::ui_detail {
lv_obj_t *label(lv_obj_t *parent, const char *text, int width) {
    auto *object = lv_label_create(parent);
    lv_obj_set_width(object, width);
    lv_label_set_text(object, text);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    return object;
}

void check_font(const char *text) {
    const auto *p = reinterpret_cast<const unsigned char *>(text);
    while (*p) {
        std::uint32_t point = *p++;
        if (point >= 0xc0) {
            const unsigned count = point < 0xe0 ? 1 : point < 0xf0 ? 2 : 3;
            point &= (1u << (6 - count)) - 1;
            for (unsigned i = 0; i < count && *p; ++i)
                point = (point << 6) | (*p++ & 63);
        }
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(&pace_font_24, &glyph, point, 0) || glyph.is_placeholder)
            state.unsupported_font = true;
    }
}

namespace {
const char *freshness_text(Freshness value) {
    switch (value) {
    case Freshness::current:
        return tr("Calendar current", "Kalender aktuell");
    case Freshness::stale:
        return tr("Cached / stale", "Gespeichert / veraltet");
    case Freshness::expired:
        return tr("Coverage expired", "Zeitraum abgelaufen");
    case Freshness::time_unknown:
        return tr("Time unknown / cached", "Uhrzeit unbekannt / gespeichert");
    default:
        return tr("Calendar unavailable", "Kalender nicht verfügbar");
    }
}

const char *feed_text(FeedResult result) {
    switch (result) {
    case FeedResult::ok:
        return tr("Calendar synchronized", "Kalender synchronisiert");
    case FeedResult::access_denied:
        return tr("Check calendar pairing secret", "Kalender: Kopplungsschlüssel prüfen");
    case FeedResult::reauthorization_required:
        return tr("Authorize calendar again", "Kalender erneut autorisieren");
    case FeedResult::capacity_exceeded:
        return tr("Calendar exceeds capacity", "Kalender überschreitet Kapazität");
    case FeedResult::unsupported_preferences:
        return tr("Unsupported calendar preferences", "Kalendereinstellungen nicht unterstützt");
    case FeedResult::temporarily_unavailable:
        return tr("Calendar connection unavailable", "Kalenderverbindung nicht verfügbar");
    case FeedResult::invalid_request:
        return tr("Calendar request rejected", "Kalenderanfrage abgelehnt");
    default:
        return tr("Calendar response invalid", "Kalenderantwort ungültig");
    }
}

const char *update_text() {
    using services::UpdateStage;
    if (!services::update_available())
        return tr("Updates are not configured. Install a signed build once using USB to enable "
                  "file uploads and optional HTTPS updates.",
                  "Updates sind nicht eingerichtet. Einmal signierte Firmware per USB "
                  "installieren. Danach sind Datei-Uploads und optionale HTTPS-Updates verfügbar.");
    if (!services::update_source_configured(state.runtime->settings.ota_manifest_url.data()) &&
        state.health.update_stage == UpdateStage::idle)
        return tr("No update host needed: hold Settings, join PACE Wi-Fi and upload a signed "
                  "firmware file in Diagnostics. An HTTPS source is optional.",
                  "Kein Updatehost nötig: Einstellungen gedrückt halten, mit PACE-WLAN verbinden "
                  "und unter Diagnose eine signierte Firmwaredatei hochladen. Eine HTTPS-Quelle "
                  "ist optional.");
    if (state.runtime->update_requested && !state.health.updating)
        return tr("Update requested. Waiting for the current operation.",
                  "Update angefordert. Der laufende Vorgang wird beendet.");
    switch (state.health.update_stage) {
    case UpdateStage::checking:
        return tr("Checking signed update...", "Signiertes Update wird geprüft...");
    case UpdateStage::downloading:
        return tr("Downloading update. Keep USB power connected.",
                  "Update wird geladen. USB-Stromversorgung angeschlossen lassen.");
    case UpdateStage::verifying:
        return tr("Verifying image and signature...", "Firmware und Signatur werden geprüft...");
    case UpdateStage::restarting:
        return tr("Update installed. Restarting...", "Update installiert. Neustart...");
    case UpdateStage::current:
        return tr("This version is already installed.", "Diese Version ist bereits installiert.");
    case UpdateStage::failed:
        return tr("Update failed or was rejected. The current firmware is retained. Check the "
                  "source and retry.",
                  "Update fehlgeschlagen oder abgelehnt. Die bisherige Firmware bleibt erhalten. "
                  "Quelle prüfen und erneut versuchen.");
    case UpdateStage::needs_network:
        return tr("Connect Wi-Fi and wait for the clock to synchronize, then retry.",
                  "WLAN verbinden und Zeitsynchronisation abwarten, dann erneut versuchen.");
    default:
        return tr("Install the configured signed update? Keep USB power connected until restart.",
                  "Das konfigurierte signierte Update installieren? USB-Stromversorgung bis zum "
                  "Neustart angeschlossen lassen.");
    }
}

void update_panel() {
    if (state.health.updating)
        state.update_open = true;
    if (!state.update_open) {
        lv_obj_add_flag(state.update_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(state.update_panel, LV_OBJ_FLAG_HIDDEN);
    char text[512];
    std::snprintf(text, sizeof(text), "%s %s\n\n%s", tr("Firmware", "Firmware"),
                  esp_app_get_description()->version, update_text());
    if (state.health.update_stage == services::UpdateStage::downloading) {
        const auto used = std::strlen(text);
        std::snprintf(text + used, sizeof(text) - used, "\n%u %%", state.health.update_percent);
    }
    lv_label_set_text(state.update_label, text);
    const bool busy = state.runtime->update_requested || state.health.updating;
    if (!services::update_available() || busy ||
        !services::update_source_configured(state.runtime->settings.ota_manifest_url.data()))
        lv_obj_add_state(state.update_confirm, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(state.update_confirm, LV_STATE_DISABLED);
    if (busy)
        lv_obj_add_state(state.update_close, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(state.update_close, LV_STATE_DISABLED);
}
} // namespace

void render(UtcSeconds now, std::int64_t mono) {
    char text[512];
    if (state.calendar_redraw)
        state.unsupported_font = false;
    const auto &settings = state.runtime->settings;
    char clock[40];
    if (!state.health.time_trusted ||
        !clock_text(*state.snapshot, now, settings.preferences.twelve_hour, true, clock,
                    sizeof(clock)))
        std::snprintf(clock, sizeof(clock), "%s", tr("Time unknown", "Uhrzeit unbekannt"));
    std::snprintf(text, sizeof(text), "%s%s", clock, settings.demo ? " | Demo" : "");
    lv_label_set_text(state.heading, text);
    lv_label_set_text(state.network_icon, state.health.network ? LV_SYMBOL_WIFI " " LV_SYMBOL_OK
                                                               : LV_SYMBOL_WIFI
                                              " " LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(state.network_icon,
                                lv_color_hex(state.health.network ? 0x145b52 : 0x9b392d), 0);
    state.runtime->traffic_visible = state.view == View::traffic && !state.health.setup &&
                                     !state.update_open && !state.health.updating &&
                                     !state.brightness_open;
    for (unsigned i = 0; i < state.tabs.size(); ++i) {
        const bool active = i == static_cast<unsigned>(state.view);
        lv_obj_set_style_bg_color(state.tabs[i], lv_color_hex(active ? 0x145b52 : 0xe8eeec), 0);
        lv_obj_set_style_text_color(state.tabs[i], lv_color_hex(active ? 0xffffff : 0x1d303b), 0);
    }
    state.next_event = -1;
    std::snprintf(text, sizeof(text), "%s",
                  tr("Your next seven days", "Deine nächsten sieben Tage"));
    if (state.snapshot->clock_count && state.health.time_trusted) {
        for (auto wanted : {Highlight::now, Highlight::next}) {
            for (unsigned i = 0; i < state.snapshot->event_count; ++i) {
                if (highlight(*state.snapshot, state.snapshot->events[i], now, true) == wanted) {
                    state.next_event = i;
                    break;
                }
            }
            if (state.next_event >= 0)
                break;
        }
        if (state.next_event >= 0) {
            const auto &event = state.snapshot->events[state.next_event];
            event_text(event, text, sizeof(text), now);
            const auto &timed = std::get<TimedInterval>(event.interval);
            if (timed.start_utc > now && timed.start_utc - now <= 7200) {
                const auto used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, " | in %lld min",
                              (long long)((timed.start_utc - now + 59) / 60));
            }
        }
    }
    lv_label_set_text(state.next_label, text);
    if (state.calendar_redraw) {
        if (state.view == View::traffic)
            make_traffic(mono);
        else
            make_agenda(now);
        state.calendar_redraw = false;
    }
    std::strcpy(text, "--");
    if (indoor_available(state.inside, mono))
        temperature_text(state.inside.celsius, settings.fahrenheit, settings.language, text,
                         sizeof(text));
    lv_label_set_text(state.inside_value, text);
    if (state.sensor_fault)
        std::snprintf(text, sizeof(text), "%s", tr("Sensor unavailable", "Sensor nicht verfügbar"));
    else if (state.inside.valid)
        std::snprintf(text, sizeof(text), tr("Measured · %lld min", "Gemessen · %lld min"),
                      (long long)((mono - state.inside.sampled_at_monotonic_ms) / 60000));
    else
        std::snprintf(text, sizeof(text), "%s", tr("Waiting for sensor", "Warte auf Sensor"));
    lv_label_set_text(state.inside_note, text);
    const auto weather =
        outdoor_freshness(state.health.outside, now, mono, state.health.time_trusted);
    std::strcpy(text, "--");
    if (weather == Freshness::current || weather == Freshness::stale)
        temperature_text(state.health.outside.celsius, settings.fahrenheit, settings.language, text,
                         sizeof(text));
    lv_label_set_text(state.outside_value, text);
    lv_label_set_text(state.outside_note, !settings.weather_enabled
                                              ? tr("Not configured", "Nicht eingerichtet")
                                              : settings.location.data());
    check_font(settings.location.data());
    const bool pending = state.runtime->sync_requested || state.health.sync_pending;
    const bool syncing = state.runtime->sync_in_progress || state.health.syncing;
    lv_label_set_text(state.sync_label, syncing   ? tr("Syncing...", "Lädt...")
                                        : pending ? tr("Sync queued", "Vorgemerkt")
                                                  : tr("Sync now", "Synchronisieren"));
    if (syncing || pending || !state.runtime->configured)
        lv_obj_add_state(state.sync_button, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(state.sync_button, LV_STATE_DISABLED);
    char sync[96];
    const auto fresh = freshness(state.snapshot->clock_count ? state.snapshot : nullptr, now,
                                 state.health.time_trusted);
    if (syncing)
        std::snprintf(sync, sizeof(sync), "%s",
                      tr("Synchronizing calendar...", "Kalender wird synchronisiert..."));
    else if (pending)
        std::snprintf(
            sync, sizeof(sync), "%s",
            tr("Sync queued · waiting for Wi-Fi/time", "Sync vorgemerkt · warte auf WLAN/Zeit"));
    else if (fresh == Freshness::current || fresh == Freshness::stale)
        std::snprintf(
            sync, sizeof(sync), tr("%s · %lld min ago", "%s · vor %lld min"), freshness_text(fresh),
            (long long)(std::max<UtcSeconds>(0, now - state.snapshot->generated_at_utc) / 60));
    else
        std::snprintf(sync, sizeof(sync), "%s", freshness_text(fresh));
    const char *fault =
        state.touch_fault               ? tr("Touch fault", "Touch-Fehler")
        : state.health.cache_error      ? tr("Cache write failed", "Speichern fehlgeschlagen")
        : state.health.brightness_error ? tr("Brightness not saved · retry Settings",
                                             "Helligkeit nicht gespeichert · erneut versuchen")
        : state.unsupported_font
            ? tr("Some characters are unsupported", "Einige Zeichen werden nicht unterstützt")
        : state.health.notice == services::Notice::setup_failed
            ? tr("Setup failed; use USB recovery", "Einrichtung fehlgeschlagen; USB verwenden")
        : state.health.notice == services::Notice::reset_failed
            ? tr("Reset failed; check storage", "Zurücksetzen fehlgeschlagen")
        : state.health.calendar != FeedResult::ok ? feed_text(state.health.calendar)
        : state.snapshot->secondary_configured && !state.snapshot->secondary_available
            ? tr("Second calendar unavailable", "Zweiter Kalender nicht verfügbar")
            : "";
    char source[96];
    if (settings.weather_enabled) {
        if (weather == Freshness::current || weather == Freshness::stale)
            std::snprintf(
                source, sizeof(source), "Open-Meteo · %lld min%s",
                (long long)(std::max<UtcSeconds>(0, now - state.health.outside.source_time_utc) /
                            60),
                weather == Freshness::stale ? tr(" · stale", " · veraltet") : "");
        else
            std::snprintf(source, sizeof(source), "%s",
                          tr("Open-Meteo · unavailable", "Open-Meteo · nicht verfügbar"));
    } else
        std::snprintf(source, sizeof(source), "%s",
                      tr("Hold Settings to configure", "Einstellungen gedrückt halten"));
    std::snprintf(text, sizeof(text), "%s\n%s\n%s\n%s", sync, source, fault,
                  state.health.network ? "" : tr("Wi-Fi offline", "WLAN offline"));
    lv_label_set_text(state.footer, text);
    if (state.health.setup) {
        lv_obj_remove_flag(state.setup_panel, LV_OBJ_FLAG_HIDDEN);
        std::snprintf(text, sizeof(text),
                      tr("Setup Wi-Fi: %s\nPassword: %s\nOpen http://192.168.4.1\nCloses after %u "
                         "minutes idle.",
                         "Einrichtungs-WLAN: %s\nPasswort: %s\nhttp://192.168.4.1 öffnen\nSchließt "
                         "nach %u Minuten Inaktivität."),
                      state.health.setup_ssid.data(), state.health.setup_password.data(),
                      services::kSetupIdleMinutes);
        lv_label_set_text(state.setup_label, text);
        char wifi[160];
        std::snprintf(wifi, sizeof(wifi), "WIFI:T:WPA;S:%s;P:%s;;", state.health.setup_ssid.data(),
                      state.health.setup_password.data());
        lv_qrcode_update(state.qr, wifi, std::strlen(wifi));
    } else
        lv_obj_add_flag(state.setup_panel, LV_OBJ_FLAG_HIDDEN);
    update_panel();
    render_brightness();
}
} // namespace pace::ui_detail
