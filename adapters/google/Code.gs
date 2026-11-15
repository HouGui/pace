// SPDX-License-Identifier: MIT
// Add adapters/feed.js as a second Apps Script file named Feed. No source edits.
function authorize() {
  const calendar = PropertiesService.getScriptProperties().getProperty('CALENDAR_ID');
  if (!calendar) throw new Error('Set CALENDAR_ID in Project Settings > Script Properties first.');
  Calendar.Events.list(calendar, {maxResults: 1});
  const secondary = PropertiesService.getScriptProperties().getProperty('SECONDARY_CALENDAR_ID');
  if (secondary && secondary !== calendar) Calendar.Events.list(secondary, {maxResults: 1});
}

function doGet() {
  return ContentService.createTextOutput(
      'PACE adapter: configure the device to POST. No calendar data is served by GET.');
}

// Provider pagination is shared by both calendars and has one overall deadline.
function readCalendar(calendar, range, zone, deadline) {
  const events = [];
  let pageToken;
  for (let page = 0; page < 8; ++page) {
    if (Date.now() > deadline) throw new Error('temporarily_unavailable');
    const options = {
      timeMin: new Date(range.days[0].start_utc * 1000).toISOString(),
      timeMax: new Date(range.days[range.days.length - 1].end_utc * 1000).toISOString(),
      singleEvents: true,
      orderBy: 'startTime',
      showDeleted: false,
      maxResults: 129,
      timeZone: zone
    };
    if (pageToken) options.pageToken = pageToken;
    const response = Calendar.Events.list(calendar, options);
    for (const event of response.items || []) {
      const mapped = PaceFeed.google(event);
      if (mapped) events.push(mapped);
    }
    if (events.length > 128) throw new Error('capacity_exceeded');
    pageToken = response.nextPageToken;
    if (!pageToken) break;
  }
  if (pageToken || Date.now() > deadline) throw new Error('temporarily_unavailable');
  return events;
}

function doPost(e) {
  let feed;
  try {
    const properties = PropertiesService.getScriptProperties();
    const p = PaceFeed.authenticate(
        e && e.postData && e.postData.contents, properties.getProperty('DEVICE_TOKEN'));
    const now = Math.floor(Date.now() / 1000), deadline = Date.now() + 20000;
    const range = PaceFeed.window(p, now, p.month_view),
          calendar = properties.getProperty('CALENDAR_ID');
    if (!calendar) throw new Error('invalid_request');
    const mappings = JSON.parse(properties.getProperty('CATEGORY_MAP') || '[]');
    const events = readCalendar(calendar, range, p.time_zone, deadline);
    const hash = id =>
        Utilities.computeDigest(Utilities.DigestAlgorithm.SHA_256, id, Utilities.Charset.UTF_8)
            .map(b => ((b + 256) % 256).toString(16).padStart(2, '0'))
            .join('');
    feed = PaceFeed.build(p, now, range, events, mappings, hash);
    const secondary = properties.getProperty('SECONDARY_CALENDAR_ID');
    if (p.secondary_calendar && secondary && secondary !== calendar) {
      let extra = null;
      try {
        extra = readCalendar(secondary, range, p.time_zone, deadline);
      } catch {
        // Preserve the main calendar when the optional source is unavailable.
      }
      feed = PaceFeed.withSecondary(
          feed, properties.getProperty('SECONDARY_CALENDAR_LABEL'), extra, hash);
    }
  } catch (error) {
    feed = PaceFeed.error(error.message);
  }
  return ContentService.createTextOutput(JSON.stringify(feed))
      .setMimeType(ContentService.MimeType.JSON);
}
