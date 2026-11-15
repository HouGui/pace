// SPDX-License-Identifier: MIT
#include "pace/calendar_view.hpp"
#include "ui_state.hpp"
#include <cstdio>

namespace pace::ui_detail {
namespace {
const char *status_text(RouteStatus status) {
    switch (status) {
    case RouteStatus::pending:
        return tr("Waiting for Wi-Fi/time or refresh", "Warte auf WLAN/Zeit oder Aktualisierung");
    case RouteStatus::no_route:
        return tr("No route · check endpoints and coverage",
                  "Keine Route · Orte und Abdeckung prüfen");
    case RouteStatus::access_denied:
        return state.runtime->settings.traffic.provider == RoutingProvider::open
                   ? tr("Provider denied access · check contact/rules",
                        "Zugriff abgelehnt · Kontakt/Regeln prüfen")
                   : tr("Check API key, Routes API and billing",
                        "API-Schlüssel, Routes API und Abrechnung prüfen");
    case RouteStatus::quota:
        return tr("Provider quota reached", "Anfragelimit des Anbieters erreicht");
    case RouteStatus::invalid:
        return tr("Provider response unsupported", "Anbieterantwort nicht unterstützt");
    default:
        return tr("Travel time unavailable", "Fahrzeit nicht verfügbar");
    }
}

lv_obj_t *line(lv_obj_t *parent, const char *text, const lv_font_t *font = &pace_font_16) {
    check_font(text);
    auto *value = label(parent, text, 724);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(value, font, 0);
    return value;
}
} // namespace

void make_traffic(std::int64_t mono) {
    lv_obj_clean(state.agenda);
    lv_obj_set_flex_flow(state.agenda, LV_FLEX_FLOW_COLUMN);
    const auto &settings = state.runtime->settings.traffic;
    const bool open = settings.provider == RoutingProvider::open;
    if (!settings.count) {
        line(state.agenda, tr("Traffic · your regular journeys", "Verkehr · deine täglichen Wege"),
             &pace_font_24);
        line(state.agenda,
             tr("Hold Settings to add up to four routes by car or public transport. Calendar stays "
                "your home screen.",
                "Einstellungen gedrückt halten, um bis zu vier Wege mit Auto oder ÖPNV "
                "anzulegen. Der Kalender bleibt die Startansicht."));
        line(state.agenda, tr("Open car and transit routing: no key or billing. Configure "
                              "coordinates in Settings.",
                              "Offenes Auto- und ÖPNV-Routing: ohne Schlüssel oder Abrechnung. "
                              "Koordinaten in Einstellungen eintragen."));
        return;
    }
    auto *attribution = line(state.agenda, open ? "© OpenStreetMap · OSRM / MOTIS" : "Google Maps");
    lv_obj_set_style_text_color(attribution, lv_color_hex(0x5e5e5e), 0);
    if (state.selected_route < 0) {
        auto *grid = lv_obj_create(state.agenda);
        lv_obj_set_size(grid, 760, 246);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        for (unsigned i = 0; i < settings.count; ++i) {
            const auto &route = settings.routes[i];
            const auto &result = state.health.routes[i];
            const bool current =
                state.health.network && state.health.time_trusted && route_current(result, mono);
            auto *card = button(
                grid, "", 374, 118,
                [](lv_event_t *event) {
                    state.selected_route = static_cast<int>(
                        reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event)));
                    navigate();
                },
                i);
            lv_obj_clean(card);
            lv_obj_set_pos(card, (i % 2) * 382, (i / 2) * 126);
            lv_obj_set_style_bg_color(card, lv_color_hex(0xf0f5f4), 0);
            auto *name = label(card, route.name.data(), 350);
            check_font(route.name.data());
            lv_obj_set_style_text_font(name, &pace_font_24, 0);
            lv_obj_set_pos(name, 6, 2);
            auto *mode = label(
                card, route.mode == TravelMode::car ? tr("Car", "Auto") : tr("Transit", "ÖPNV"),
                160);
            lv_obj_set_pos(mode, 6, 36);
            char text[128];
            if (current)
                std::snprintf(text, sizeof(text), "%u min",
                              static_cast<unsigned>((result.seconds + 59) / 60));
            else
                std::snprintf(text, sizeof(text), "--");
            auto *duration = label(card, text, 180);
            lv_obj_set_style_text_font(duration, &pace_font_32, 0);
            lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_pos(duration, 170, 29);
            auto *summary = label(card,
                                  current && result.summary[0]
                                      ? result.summary.data()
                                      : tr("Tap for route details", "Für Routendetails tippen"),
                                  350);
            check_font(lv_label_get_text(summary));
            lv_obj_set_pos(summary, 6, 67);
            if (current)
                std::snprintf(
                    text, sizeof(text), tr("%lld min ago · %s", "vor %lld min · %s"),
                    static_cast<long long>((mono - result.fetched_at_monotonic_ms) / 60000),
                    result.warnings[0] ? tr("See advisory", "Hinweis beachten")
                    : open && route.mode == TravelMode::car
                        ? tr("No live traffic", "Ohne aktuelle Staus")
                    : open ? (result.realtime ? tr("Some live data", "Teils Echtzeitdaten")
                                              : tr("Timetable", "Fahrplan"))
                           : tr("Leave now", "Jetzt starten"));
            else
                std::snprintf(text, sizeof(text), "%s",
                              result.status == RouteStatus::ok
                                  ? tr("Expired / offline", "Veraltet / offline")
                                  : status_text(result.status));
            auto *age = label(card, text, 350);
            lv_obj_set_pos(age, 6, 88);
        }
        return;
    }
    button(state.agenda, tr("All routes", "Alle Wege"), 140, 32, [](lv_event_t *) {
        state.selected_route = -1;
        navigate();
    });
    for (unsigned i = 0; i < settings.count; ++i) {
        if (static_cast<int>(i) != state.selected_route)
            continue;
        const auto &route = settings.routes[i];
        const auto &result = state.health.routes[i];
        const bool current =
            state.health.time_trusted && state.health.network && route_current(result, mono);
        auto *card = lv_obj_create(state.agenda);
        lv_obj_set_width(card, 750);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_set_style_pad_row(card, 3, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xf0f5f4), 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        char text[384];
        std::snprintf(text, sizeof(text), "%s · %s", route.name.data(),
                      route.mode == TravelMode::car ? tr("Car", "Auto")
                                                    : tr("Public transport", "ÖPNV"));
        line(card, text, &pace_font_24);
        if (current) {
            auto *overview = lv_obj_create(card);
            lv_obj_set_size(overview, 724, 180);
            lv_obj_set_style_pad_all(overview, 0, 0);
            lv_obj_set_style_border_width(overview, 0, 0);
            lv_obj_set_style_bg_opa(overview, LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(overview, LV_OBJ_FLAG_SCROLLABLE);
            auto *map = lv_obj_create(overview);
            lv_obj_set_pos(map, 460, 0);
            lv_obj_set_size(map, 258, 180);
            lv_obj_set_style_pad_all(map, 0, 0);
            lv_obj_set_style_pad_row(map, 2, 0);
            lv_obj_set_style_border_width(map, 0, 0);
            lv_obj_set_style_bg_opa(map, LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(map, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(map, LV_FLEX_FLOW_COLUMN);
            make_route_map(map);
            std::snprintf(text, sizeof(text), "%u min",
                          static_cast<unsigned>((result.seconds + 59) / 60));
            auto *duration = line(overview, text, &pace_font_32);
            lv_obj_set_width(duration, 430);
            if (open) {
                auto *kind =
                    line(overview, route.mode == TravelMode::car
                                       ? tr("Car estimate · no live congestion",
                                            "Autoschätzung · ohne aktuelle Staus")
                                       : tr("Journey duration · walking and transfers included",
                                            "Reisedauer · mit Fußwegen und Umstiegen"));
                lv_obj_set_width(kind, 430);
                lv_obj_set_pos(kind, 0, 45);
            }
            std::snprintf(text, sizeof(text), "%s > %s", route.origin.data(),
                          route.destination.data());
            line(card, text);
            if (result.summary[0])
                line(card, result.summary.data(), &pace_font_24);
            if (result.departure_utc) {
                char departure[40];
                if (clock_text(*state.snapshot, result.departure_utc,
                               state.runtime->settings.preferences.twelve_hour, true, departure,
                               sizeof(departure))) {
                    std::snprintf(text, sizeof(text),
                                  tr("Journey starts at %s", "Reisebeginn um %s"), departure);
                    line(card, text);
                }
                line(card,
                     result.realtime
                         ? tr("Some legs include live updates; check operator advisories.",
                              "Einzelne Abschnitte mit Echtzeitdaten; Betreiberhinweise prüfen.")
                         : tr("Timetable only · delays may be missing",
                              "Nur Fahrplan · Verspätungen können fehlen"));
            }
            std::snprintf(text, sizeof(text), tr("Checked %lld min ago", "Vor %lld min geprüft"),
                          static_cast<long long>((mono - result.fetched_at_monotonic_ms) / 60000));
            line(card, text);
            if (result.distance_known)
                std::snprintf(text, sizeof(text), "%.1f km", result.meters / 1000.0);
            else
                std::snprintf(text, sizeof(text), "%s",
                              tr("Total distance unavailable", "Gesamtdistanz nicht verfügbar"));
            if (state.runtime->settings.language == Language::german)
                for (char *c = text; *c; ++c)
                    if (*c == '.')
                        *c = ',';
            line(card, text);
            if (result.warnings[0])
                line(card, result.warnings.data());
            if (open && route.mode == TravelMode::transit)
                line(card, "Transitous: transitous.org/sources");
            if (open)
                line(card, "© OpenStreetMap contributors · openstreetmap.org/copyright");
        } else {
            line(card, "--", &pace_font_32);
            line(card, result.status == RouteStatus::ok
                           ? tr("Estimate expired or offline · reopen to refresh",
                                "Schätzung veraltet oder offline · zum Aktualisieren neu öffnen")
                           : status_text(result.status));
        }
    }
}
} // namespace pace::ui_detail
