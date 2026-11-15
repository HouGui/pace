// SPDX-License-Identifier: MIT
import {createHash} from 'node:crypto';

import Feed from '../feed.js';

async function readCalendar(config, calendarId, range, accessToken, fetcher, deadline) {
  const calendar = calendarId ? `/me/calendars/${encodeURIComponent(calendarId)}/calendarView` :
                                '/me/calendarView';
  let url = new URL(`https://graph.microsoft.com/v1.0${calendar}`);
  url.searchParams.set('startDateTime', new Date(range.days[0].start_utc * 1000).toISOString());
  url.searchParams.set(
      'endDateTime', new Date(range.days[range.days.length - 1].end_utc * 1000).toISOString());
  url.searchParams.set('$top', '129');
  url.searchParams.set('$select', 'id,subject,start,end,isAllDay,isCancelled,categories,location');
  const events = [];
  for (let page = 0; page < 8; ++page) {
    if (url.origin !== 'https://graph.microsoft.com' || url.username || url.password || url.hash ||
        !url.pathname.startsWith('/v1.0/'))
      throw new Error('temporarily_unavailable');
    const remaining = deadline - Date.now();
    if (remaining <= 0) throw new Error('temporarily_unavailable');
    const response = await fetcher(url, {
      headers: {Authorization: `Bearer ${accessToken}`, Prefer: 'outlook.timezone="UTC"'},
      redirect: 'error',
      signal: AbortSignal.timeout(Math.min(remaining, 10000))
    });
    if (!response.ok)
      throw new Error(
          response.status === 401     ? 'reauthorization_required' :
              response.status === 403 ? 'access_denied' :
                                        'temporarily_unavailable');
    const chunks = [];
    let size = 0;
    for await (const chunk of response.body) {
      size += chunk.length;
      if (size > 262144) throw new Error('capacity_exceeded');
      chunks.push(chunk);
    }
    const data = JSON.parse(Buffer.concat(chunks).toString('utf8'));
    if (!Array.isArray(data.value)) throw new Error('temporarily_unavailable');
    for (const event of data.value) {
      const mapped = Feed.outlook(event, config.calendar_time_zone);
      if (mapped) events.push(mapped);
    }
    if (events.length > 128) throw new Error('capacity_exceeded');
    if (!data['@odata.nextLink']) return events;
    url = new URL(data['@odata.nextLink']);
  }
  throw new Error('capacity_exceeded');
}

export async function calendarFeed(
    config, preferences, accessToken, fetcher = fetch, now = Math.floor(Date.now() / 1000)) {
  const range = Feed.window(preferences, now, preferences.month_view);
  const deadline = Date.now() + 20000;
  const hash = id => createHash('sha256').update(id).digest('hex');
  const events =
      await readCalendar(config, config.calendar_id, range, accessToken, fetcher, deadline);
  const feed = Feed.build(preferences, now, range, events, config.category_map, hash);
  if (!preferences.secondary_calendar || !config.secondary_calendar_id ||
      config.secondary_calendar_id === config.calendar_id)
    return feed;
  let extra = null;
  try {
    const secondaryConfig = {
      ...config,
      calendar_time_zone: config.secondary_calendar_time_zone || config.calendar_time_zone
    };
    extra = await readCalendar(
        secondaryConfig, config.secondary_calendar_id, range, accessToken, fetcher, deadline);
  } catch {
    // Do not expose account details or discard the successful main calendar.
  }
  return Feed.withSecondary(feed, config.secondary_calendar_label, extra, hash);
}
