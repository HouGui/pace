// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import test from 'node:test';
import vm from 'node:vm';

import Feed from '../adapters/feed.js';
import {calendarFeed} from '../adapters/service/outlook.mjs';

const preference = zone => ({time_zone: zone, locale: 'en-GB', hour_cycle: 'h23'});
const token = 'a'.repeat(43);
const now = Date.parse('2030-01-07T08:00:00Z') / 1000;

for (const [zone, date, hours] of [
         ['Europe/Berlin', '2030-03-31T12:00:00Z', 23],
         ['Europe/Berlin', '2030-10-27T12:00:00Z', 25],
         ['America/New_York', '2030-03-10T12:00:00Z', 23],
         ['Australia/Lord_Howe', '2030-04-07T12:00:00Z', 24.5],
         ['Asia/Kathmandu', '2030-01-07T12:00:00Z', 24],
         ['Pacific/Honolulu', '2030-01-07T12:00:00Z', 24]]) {
  test(`civil days and complete C++ feed validation: ${zone} ${hours}h`, () => {
    const p = preference(zone), at = Date.parse(date) / 1000, range = Feed.window(p, at);
    assert.equal((range.days[0].end_utc - range.days[0].start_utc) / 3600, hours);
    assert.equal(range.days.length, 14);
    const feed = Feed.build(p, at, range, []);
    if (process.env.PACE_FEED_CHECK) {
      const dir = mkdtempSync(join(tmpdir(), 'pace-feed-'));
      try {
        const path = join(dir, 'feed.json');
        writeFileSync(path, JSON.stringify(feed));
        const result = spawnSync(process.env.PACE_FEED_CHECK, [path], {encoding: 'utf8'});
        assert.equal(result.status, 0, result.stdout + result.stderr);
      } finally {
        rmSync(dir, {recursive: true, force: true});
      }
    }
  });
}
test('reject unauthorized requests and unsupported preferences', () => {
  const body =
      JSON.stringify({schema_version: 2, device_token: token, preferences: preference('Etc/UTC')});
  assert.equal(Feed.authenticate(body, token).time_zone, 'Etc/UTC');
  assert.throws(() => Feed.authenticate(body, 'b'.repeat(43)), /access_denied/);
  assert.throws(() => Feed.authenticate('x'.repeat(2049), token), /invalid_request/);
  assert.throws(() => Feed.window(preference('No/Such_Zone'), now), /unsupported_preferences/);
});

test('calendar categories, recurrence instances, cancellation and exclusive dates', () => {
  const p = preference('Etc/UTC'), range = Feed.window(p, now);
  const raw = {
    id: 'synthetic-occurrence',
    summary: 'Team workshop',
    start: {date: '2030-01-09'},
    end: {date: '2030-01-10'},
    colorId: '2'
  };
  const event = Feed.google(raw), mapping = [{provider_id: '2', id: 'team', label: 'Team'}];
  const result = Feed.build(p, now, range, [event], mapping);
  assert.equal(result.events[0].end_date, '2030-01-10');
  assert.deepEqual(result.events[0].category_ids, ['team']);
  assert.equal(Feed.google({status: 'cancelled'}), null);
  assert.throws(() => Feed.build(p, now, range, [event, event]), /temporarily_unavailable/);
  assert.throws(() => Feed.build(p, now, range, Array(129).fill(event)), /capacity_exceeded/);
  assert.throws(
      () => Feed.build(p, now, range, [{...event, title: 'é'.repeat(65)}]), /capacity_exceeded/);
});

test('Outlook all-day dates survive conversion across UTC midnight', () => {
  const event = Feed.outlook(
      {
        id: 'all-day',
        isAllDay: true,
        start: {dateTime: '2030-01-08T23:00:00.0000000', timeZone: 'UTC'},
        end: {dateTime: '2030-01-09T23:00:00.0000000', timeZone: 'UTC'}
      },
      'Europe/Berlin');
  assert.equal(event.start_date, '2030-01-09');
  assert.equal(event.end_date, '2030-01-10');
});

test('Outlook follows bounded Graph pages and strips private metadata', async () => {
  const calls = [], pages = [
    {
      value: [],
      '@odata.nextLink': 'https://graph.microsoft.com/v1.0/me/calendarView?$skiptoken=synthetic'
    },
    {
      value: [{
        id: 'synthetic',
        subject: 'Workshop',
        start: {dateTime: '2030-01-07T10:00:00.0000000', timeZone: 'UTC'},
        end: {dateTime: '2030-01-07T11:00:00.0000000', timeZone: 'UTC'},
        attendees: ['must-not-leak'],
        body: 'private'
      }]
    }
  ];
  const fetcher = async (url, options) => {
    calls.push([url.toString(), options]);
    return new Response(JSON.stringify(pages.shift()));
  };
  const result = await calendarFeed(
      {calendar_time_zone: 'Etc/UTC', category_map: []}, preference('Etc/UTC'), 'synthetic-token',
      fetcher, now);
  assert.equal(calls.length, 2);
  assert.equal(result.events.length, 1);
  assert.equal(calls[0][1].redirect, 'error');
  assert(!JSON.stringify(result).includes('must-not-leak'));
});

test('Outlook rejects foreign next links before sending credentials', async () => {
  let calls = 0;
  const fetcher = async () => {
    ++calls;
    return new Response(
        JSON.stringify({value: [], '@odata.nextLink': 'https://attacker.invalid/page'}));
  };
  await assert.rejects(calendarFeed(
      {calendar_time_zone: 'Etc/UTC', category_map: []}, preference('Etc/UTC'), 'synthetic-token',
      fetcher, now));
  assert.equal(calls, 1);
});

function googleRuntime(pages, secret = token, extraProperties = {}) {
  let calls = 0;
  const NativeDate = Date;

  class Clock extends NativeDate {
    static now() {
      return now * 1000;
    }
  }

  const context = vm.createContext({
    Date: Clock,
    Intl,
    PropertiesService: {
      getScriptProperties: () => ({
        getProperty: key =>
            ({DEVICE_TOKEN: secret, CALENDAR_ID: 'synthetic-calendar', ...extraProperties})[key] ||
            null
      })
    },
    Calendar: {
      Events: {
        list: () => {
          ++calls;
          return pages.shift();
        }
      }
    },
    Utilities: {
      DigestAlgorithm: {SHA_256: 'sha256'},
      Charset: {UTF_8: 'utf8'},
      computeDigest: (_, value) => [...createHash('sha256').update(value).digest()]
    },
    ContentService: {
      MimeType: {JSON: 'json'},
      createTextOutput: value => ({
        value,
        setMimeType() {
          return this;
        }
      })
    }
  });
  vm.runInContext(readFileSync(new URL('../adapters/feed.js', import.meta.url), 'utf8'), context);
  vm.runInContext(
      readFileSync(new URL('../adapters/google/Code.gs', import.meta.url), 'utf8'), context);
  return {
    post: body => JSON.parse(context.doPost({postData: {contents: JSON.stringify(body)}}).value),
    calls: () => calls
  };
}
test('Apps Script authentication happens before Calendar access', () => {
  const app = googleRuntime([]);
  const result =
      app.post({schema_version: 2, device_token: 'wrong', preferences: preference('Etc/UTC')});
  assert.equal(result.error.code, 'access_denied');
  assert.equal(app.calls(), 0);
});

test('Apps Script paginates, preserves an empty calendar and fails on incomplete pages', () => {
  const request = {schema_version: 2, device_token: token, preferences: preference('Etc/UTC')};
  const app = googleRuntime([{items: [], nextPageToken: 'next'}, {items: []}]);
  assert.equal(app.post(request).ok, true);
  assert.equal(app.calls(), 2);
  const incomplete = googleRuntime(Array(8).fill({items: [], nextPageToken: 'more'}));
  assert.equal(incomplete.post(request).ok, false);
  assert.equal(incomplete.calls(), 8);
});

function cppFeed(feed, accepted = true) {
  if (!process.env.PACE_FEED_CHECK) return;
  const dir = mkdtempSync(join(tmpdir(), 'pace-month-'));
  try {
    const path = join(dir, 'feed.json');
    writeFileSync(path, JSON.stringify(feed));
    const result = spawnSync(process.env.PACE_FEED_CHECK, [path], {encoding: 'utf8'});
    assert.equal(result.status === 0, accepted, result.stdout + result.stderr);
  } finally {
    rmSync(dir, {recursive: true, force: true});
  }
}

for (const [zone, date] of [
         ['Europe/Berlin', '2028-02-29T12:00:00Z'], ['Europe/Berlin', '2030-03-31T12:00:00Z'],
         ['Europe/Berlin', '2030-10-27T12:00:00Z'], ['Australia/Lord_Howe', '2030-04-07T12:00:00Z'],
         ['Pacific/Honolulu', '2030-12-31T12:00:00Z']]) {
  test(`full month window, German labels and locations: ${zone} ${date}`, () => {
    const p = {...preference(zone), locale: 'de-DE'}, at = Date.parse(date) / 1000;
    const range = Feed.window(p, at, true);
    assert.equal(range.days.length, 62);
    assert.equal(range.days[0].date.slice(8), '01');
    const event = Feed.google({
      id: 'location',
      summary: 'Bürobesuch',
      location: 'München · Straße 12',
      start: {date: range.days[1].date},
      end: {date: range.days[2].date}
    });
    const feed = Feed.build(p, at, range, [event]);
    assert.equal(feed.events[0].location, 'München · Straße 12');
    cppFeed(feed);
    const invalid = structuredClone(feed);
    invalid.days.push({...invalid.days.at(-1)});
    cppFeed(invalid, false);
    assert.throws(
        () => Feed.build(p, at, range, [{...event, location: 'ü'.repeat(97)}]),
        /capacity_exceeded/);
    assert.equal(
        Feed.build(p, at, range, [{...event, location: 'Room\nTwo'}]).events[0].location,
        'Room Two');
  });
}

test('month request capability is validated and old clients retain 14 days', () => {
  const app = googleRuntime([{items: []}]);
  const request = {
    schema_version: 2,
    device_token: token,
    preferences: preference('Etc/UTC'),
    month_view: true
  };
  assert.equal(app.post(request).days.length, 62);
  assert.equal(googleRuntime([{items: []}]).post({...request, month_view: false}).days.length, 14);
  assert.equal(
      googleRuntime([]).post({...request, month_view: 'yes'}).error.code, 'invalid_request');
});

test('Outlook requests month coverage and maps only display location', async () => {
  const calls = [];
  const fetcher = async url => {
    calls.push(new URL(url));
    return new Response(JSON.stringify({
      value: [{
        id: 'visit',
        subject: 'Visit',
        location: {displayName: 'Büro Süd', coordinates: {latitude: 1}},
        start: {dateTime: '2030-01-07T10:00:00', timeZone: 'UTC'},
        end: {dateTime: '2030-01-07T11:00:00', timeZone: 'UTC'}
      }]
    }));
  };
  const feed = await calendarFeed(
      {calendar_time_zone: 'Etc/UTC', category_map: []},
      {...preference('Etc/UTC'), month_view: true}, 'synthetic-token', fetcher, now);
  assert.equal(feed.days.length, 62);
  assert.equal(calls[0].searchParams.get('startDateTime'), '2030-01-01T00:00:00.000Z');
  assert(calls[0].searchParams.get('$select').includes('location'));
  assert.equal(feed.events[0].location, 'Büro Süd');
  assert(!JSON.stringify(feed).includes('coordinates'));
  assert(!('month_view' in feed));
  cppFeed(feed);
});

test('optional second calendar preserves main IDs and fails independently', () => {
  const p = preference('Etc/UTC');
  const range = Feed.window(p, now);
  const event = Feed.google({
    id: 'same-id',
    summary: 'Birthday example',
    start: {date: '2030-01-09'},
    end: {date: '2030-01-10'}
  });
  const primary = Feed.build(p, now, range, [event]);
  const merged = Feed.withSecondary(primary, 'Birthdays', [event]);
  assert.equal(merged.events[0].id, primary.events[0].id);
  assert.notEqual(merged.events[0].id, merged.events[1].id);
  assert.equal(merged.events[1].secondary, true);
  assert.equal(merged.secondary_calendar.available, true);
  cppFeed(merged);
  const unavailable = Feed.withSecondary(primary, 'Birthdays', null);
  assert.equal(unavailable.secondary_calendar.available, false);
  assert.deepEqual(unavailable.events, primary.events);
  cppFeed(unavailable);
  const overflow = Feed.withSecondary(
      primary, 'Birthdays', Array.from({length: 128}, (_, i) => ({...event, id: `${i}`})));
  assert.equal(overflow.secondary_calendar.available, false);
  assert.deepEqual(overflow.events, primary.events);
  const missing = structuredClone(merged);
  delete missing.secondary_calendar;
  cppFeed(missing, false);
  const wrong = structuredClone(merged);
  wrong.secondary_calendar.available = false;
  cppFeed(wrong, false);
  wrong.secondary_calendar.available = true;
  wrong.events[1].secondary = 'yes';
  cppFeed(wrong, false);
  wrong.events[1].secondary = true;
  wrong.secondary_calendar.label = 'ü'.repeat(17);
  cppFeed(wrong, false);
});

test('Google second source is opt-in, complete, and isolated on failure', () => {
  const props = {
    SECONDARY_CALENDAR_ID: 'synthetic-birthdays',
    SECONDARY_CALENDAR_LABEL: 'Birthdays'
  };
  const birthday = {
    id: 'synthetic',
    summary: 'Birthday example',
    start: {date: '2030-01-09'},
    end: {date: '2030-01-10'}
  };
  const request = {
    schema_version: 2,
    device_token: token,
    preferences: preference('Etc/UTC'),
    secondary_calendar: true
  };
  const feed =
      googleRuntime([{items: [birthday]}, {items: [birthday]}], token, props).post(request);
  assert.equal(feed.events.length, 2);
  assert.equal(feed.events[1].secondary, true);
  cppFeed(feed);
  const old = googleRuntime([{items: []}], token, props);
  assert(!old.post({...request, secondary_calendar: false}).secondary_calendar);
  assert.equal(old.calls(), 1);
  const failed = googleRuntime([{items: [birthday]}], token, props).post(request);
  assert.equal(failed.ok, true);
  assert.equal(failed.events.length, 1);
  assert.equal(failed.secondary_calendar.available, false);
  cppFeed(failed);
  assert.equal(
      googleRuntime([]).post({...request, secondary_calendar: 'yes'}).error.code,
      'invalid_request');
});

test(
    'Outlook second calendar uses its ID and keeps the main feed after an access error',
    async () => {
      const config = {
        calendar_time_zone: 'Etc/UTC',
        secondary_calendar_id: 'synthetic-birthdays',
        secondary_calendar_label: 'Birthdays',
        category_map: []
      };
      const p = {...preference('Etc/UTC'), secondary_calendar: true};
      const event = {
        id: 'same',
        subject: 'Birthday example',
        isAllDay: true,
        start: {dateTime: '2030-01-09T00:00:00Z', timeZone: 'UTC'},
        end: {dateTime: '2030-01-10T00:00:00Z', timeZone: 'UTC'}
      };
      const urls = [];
      const fetcher = async url => {
        urls.push(url.href);
        return new Response(JSON.stringify({value: [event]}));
      };
      const feed = await calendarFeed(config, p, 'synthetic-token', fetcher, now);
      assert.equal(urls.length, 2);
      assert(urls[1].includes('/calendars/synthetic-birthdays/calendarView'));
      assert.equal(feed.events[1].secondary, true);
      cppFeed(feed);
      const failed = await calendarFeed(
          config, p, 'synthetic-token',
          async url =>
              url.pathname.includes('/calendars/') ? new Response('', {status: 403}) : fetcher(url),
          now);
      assert.equal(failed.ok, true);
      assert.equal(failed.events.length, 1);
      assert.equal(failed.secondary_calendar.available, false);
      cppFeed(failed);
    });
