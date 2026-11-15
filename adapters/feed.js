// SPDX-License-Identifier: MIT
// Shared by Apps Script (V8) and the optional Node adapter; no runtime dependencies.
'use strict';

var PaceFeed = (() => {
  const fail = code => {
    throw new Error(code);
  };

  const utf8Length = s => unescape(encodeURIComponent(s)).length;

  function bounded(value, max, empty = false) {
    if (typeof value !== 'string' || (!empty && !value) || utf8Length(value) > max ||
        /[\u0000-\u001f\u007f\u202a-\u202e\u2066-\u2069]/u.test(value))
      fail('capacity_exceeded');
    return value;
  }

  function preferences(p) {
    if (!p || !['h12', 'h23'].includes(p.hour_cycle)) fail('unsupported_preferences');
    bounded(p.time_zone, 64);
    bounded(p.locale, 32);
    try {
      new Intl.DateTimeFormat(p.locale, {timeZone: p.time_zone, hourCycle: p.hour_cycle});
    } catch {
      fail('unsupported_preferences');
    }
    return {time_zone: p.time_zone, locale: p.locale, hour_cycle: p.hour_cycle};
  }

  function dateAt(seconds, zone) {
    const parts =
        new Intl
            .DateTimeFormat(
                'en-GB', {timeZone: zone, year: 'numeric', month: '2-digit', day: '2-digit'})
            .formatToParts(new Date(seconds * 1000));
    const part = name => parts.find(p => p.type === name).value;
    return `${part('year')}-${part('month')}-${part('day')}`;
  }

  function nextDate(date) {
    return new Date(Date.parse(`${date}T12:00:00Z`) + 86400000).toISOString().slice(0, 10);
  }

  function dayStart(date, zone) {
    const center = Date.parse(`${date}T00:00:00Z`) / 1000;
    let low = center - 86400, high = center + 86400;
    while (low < high) {
      const mid = Math.floor((low + high) / 2);
      if (dateAt(mid, zone) < date)
        low = mid + 1;
      else
        high = mid;
    }
    if (dateAt(low, zone) !== date) fail('unsupported_preferences');
    return low;
  }

  function offsetAt(seconds, zone) {
    const parts = new Intl
                      .DateTimeFormat('en-GB', {
                        timeZone: zone,
                        year: 'numeric',
                        month: '2-digit',
                        day: '2-digit',
                        hour: '2-digit',
                        minute: '2-digit',
                        second: '2-digit',
                        hourCycle: 'h23'
                      })
                      .formatToParts(new Date(seconds * 1000));
    const part = name => Number(parts.find(p => p.type === name).value);
    return Date.UTC(
               part('year'), part('month') - 1, part('day'), part('hour'), part('minute'),
               part('second')) /
        1000 -
        seconds;
  }

  function window(p, now, monthView = false) {
    preferences(p);
    if (!Number.isInteger(now) || now < 1577836800 || now >= 4102444800) fail('invalid_request');
    const days = [];
    let date = dateAt(now, p.time_zone);
    if (monthView) date = date.slice(0, 8) + '01';
    let start = dayStart(date, p.time_zone);
    for (let i = 0; i < (monthView ? 62 : 14); ++i) {
      const next = nextDate(date), end = dayStart(next, p.time_zone);
      if (end - start < 72000 || end - start > 100800) fail('unsupported_preferences');
      const label =
          new Intl
              .DateTimeFormat(
                  p.locale,
                  {timeZone: p.time_zone, weekday: 'short', day: '2-digit', month: 'short'})
              .format(new Date((start + 43200) * 1000));
      days.push({date, label: bounded(label, 64), start_utc: start, end_utc: end});
      date = next;
      start = end;
    }
    const clock = [];
    const end = days[days.length - 1].end_utc;
    let segment = days[0].start_utc, offset = offsetAt(segment, p.time_zone);
    // IANA civil transitions in supported years are separated by more than an hour.
    for (let probe = segment + 3600; probe < end + 3600; probe += 3600) {
      const last = Math.min(probe, end - 1), next = offsetAt(last, p.time_zone);
      if (next !== offset) {
        let low = Math.max(segment, last - 3600), high = last;
        while (low < high) {
          const mid = Math.floor((low + high) / 2);
          if (offsetAt(mid, p.time_zone) === offset)
            low = mid + 1;
          else
            high = mid;
        }
        clock.push({start_utc: segment, end_utc: low, offset_seconds: offset});
        segment = low;
        offset = next;
      }
    }
    clock.push({start_utc: segment, end_utc: end, offset_seconds: offset});
    if (clock.length > 32) fail('capacity_exceeded');
    return {days, clock};
  }

  function timestamp(value) {
    if (typeof value !== 'string' || !/(Z|[+-]\d\d:\d\d)$/.test(value))
      fail('temporarily_unavailable');
    const t = Date.parse(value) / 1000;
    if (!Number.isInteger(t) || t < 1577836800 || t >= 4102444800) fail('temporarily_unavailable');
    return t;
  }

  function date(value) {
    if (typeof value !== 'string' || !/^20\d\d-\d\d-\d\d$/.test(value) ||
        !Number.isFinite(Date.parse(`${value}T00:00:00Z`)) ||
        new Date(`${value}T00:00:00Z`).toISOString().slice(0, 10) !== value)
      fail('temporarily_unavailable');
    return value;
  }

  function timedLabel(seconds, p) {
    return bounded(
        new Intl
            .DateTimeFormat(p.locale, {
              timeZone: p.time_zone,
              hour: '2-digit',
              minute: '2-digit',
              hourCycle: p.hour_cycle,
              timeZoneName: 'shortOffset'
            })
            .format(new Date(seconds * 1000)),
        32);
  }

  function google(event) {
    if (event.status === 'cancelled') return null;
    if (!event.start || !event.end) fail('temporarily_unavailable');
    if (event.start.date)
      return {
        id: event.id,
        title: event.summary || '',
        location: event.location || '',
        all_day: true,
        start_date: date(event.start.date),
        end_date: date(event.end.date),
        source_categories: event.colorId ? [event.colorId] : []
      };
    return {
      id: event.id,
      title: event.summary || '',
      location: event.location || '',
      all_day: false,
      start_utc: timestamp(event.start.dateTime),
      end_utc: timestamp(event.end.dateTime),
      source_categories: event.colorId ? [event.colorId] : []
    };
  }

  function outlook(event, calendarZone) {
    if (event.isCancelled) return null;
    if (!event.start || !event.end) fail('temporarily_unavailable');
    const common = {
      id: event.id,
      title: event.subject || '',
      location: event.location?.displayName || '',
      all_day: !!event.isAllDay,
      source_categories: event.categories || []
    };
    const utc = value => {
      if (value.timeZone !== 'UTC' && !/(Z|[+-]\d\d:\d\d)$/.test(value.dateTime))
        fail('temporarily_unavailable');
      return timestamp(
          value.dateTime.replace(/(\.\d{3})\d+/, '$1') +
          (/(Z|[+-]\d\d:\d\d)$/.test(value.dateTime) ? '' : 'Z'));
    };
    // Restore civil dates after Graph's requested UTC conversion. Calendar zone
    // is explicit adapter configuration, independent of device display preferences.
    if (common.all_day)
      return {
        ...common,
        start_date: dateAt(utc(event.start), calendarZone),
        end_date: dateAt(utc(event.end), calendarZone)
      };
    return {...common, start_utc: utc(event.start), end_utc: utc(event.end)};
  }

  function location(value) {
    if (value == null) return '';
    if (typeof value !== 'string') fail('temporarily_unavailable');
    return bounded(value.replace(/[\r\n\t]+/g, ' ').trim(), 192, true);
  }

  function build(p, now, range, events, mappings = [], hash = id => id) {
    preferences(p);
    if (!Array.isArray(events) || events.length > 128 || !Array.isArray(mappings) ||
        mappings.length > 8)
      fail('capacity_exceeded');
    const categories = mappings.map(m => ({id: bounded(m.id, 32), label: bounded(m.label, 32)}));
    if (new Set(categories.map(c => c.id)).size !== categories.length) fail('invalid_request');
    const output = events.filter(Boolean).map(e => {
      const result = {
        id: bounded(hash(bounded(e.id, 2048)), 96),
        title: bounded(e.title, 128, true),
        location: location(e.location),
        all_day: e.all_day
      };
      result.category_ids = [...new Set(
          mappings.filter(m => e.source_categories.includes(m.provider_id)).map(m => m.id))];
      if (result.category_ids.length > 4) fail('capacity_exceeded');
      if (e.all_day) {
        result.start_date = date(e.start_date);
        result.end_date = date(e.end_date);
        if (result.start_date >= result.end_date) fail('temporarily_unavailable');
      } else {
        if (!(e.start_utc < e.end_utc)) fail('temporarily_unavailable');
        Object.assign(result, {
          start_utc: e.start_utc,
          end_utc: e.end_utc,
          start_label: timedLabel(e.start_utc, p),
          end_label: timedLabel(e.end_utc, p)
        });
      }
      return result;
    });
    if (new Set(output.map(e => e.id)).size !== output.length) fail('temporarily_unavailable');
    const feed = {
      schema_version: 2,
      ok: true,
      generated_at_utc: now,
      ...preferences(p),
      ...range,
      categories,
      events: output
    };
    if (utf8Length(JSON.stringify(feed)) > 65536) fail('capacity_exceeded');
    return feed;
  }

  // A failed optional calendar must never discard the complete main calendar.
  function withSecondary(feed, label, events, hash = id => id) {
    let metadata = {label: '', available: false};
    try {
      metadata.label = bounded(label || '', 32, true);
      if (!events) throw new Error('temporarily_unavailable');
      const secondary = build(
          feed, feed.generated_at_utc, {days: feed.days, clock: feed.clock}, events, [],
          id => hash(`secondary:${id}`));
      const merged = [...feed.events, ...secondary.events.map(e => ({...e, secondary: true}))];
      if (merged.length > 128 || new Set(merged.map(e => e.id)).size !== merged.length)
        throw new Error('capacity_exceeded');
      const result = {...feed, secondary_calendar: {...metadata, available: true}, events: merged};
      if (utf8Length(JSON.stringify(result)) <= 65536) return result;
    } catch {
      // Provider/configuration/capacity failures are represented without private error text.
    }
    const result = {...feed, secondary_calendar: metadata};
    return utf8Length(JSON.stringify(result)) <= 65536 ? result : feed;
  }

  function error(code) {
    const allowed = [
      'access_denied', 'reauthorization_required', 'capacity_exceeded', 'unsupported_preferences',
      'temporarily_unavailable', 'invalid_request'
    ];
    return {
      schema_version: 2,
      ok: false,
      error: {code: allowed.includes(code) ? code : 'temporarily_unavailable'}
    };
  }

  function authenticate(body, secret) {
    if (typeof body !== 'string' || utf8Length(body) > 2048) fail('invalid_request');
    let request;
    try {
      request = JSON.parse(body);
    } catch {
      fail('invalid_request');
    }
    if (!request || request.schema_version !== 2 || typeof request.device_token !== 'string' ||
        typeof secret !== 'string' || secret.length < 43 ||
        request.device_token.length !== secret.length)
      fail('access_denied');
    let mismatch = 0;
    for (let i = 0; i < secret.length; ++i)
      mismatch |= request.device_token.charCodeAt(i) ^ secret.charCodeAt(i);
    if (mismatch) fail('access_denied');
    for (const capability of ['month_view', 'secondary_calendar'])
      if (request[capability] !== undefined && typeof request[capability] !== 'boolean')
        fail('invalid_request');
    return {
      ...preferences(request.preferences),
      month_view: request.month_view === true,
      secondary_calendar: request.secondary_calendar === true
    };
  }

  return {
    window,
    build,
    withSecondary,
    google,
    outlook,
    authenticate,
    error,
    dateAt,
    dayStart,
    offsetAt,
    utf8Length
  };
})();

if (typeof module !== 'undefined') module.exports = PaceFeed;
