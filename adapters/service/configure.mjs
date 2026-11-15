// SPDX-License-Identifier: MIT
import {randomBytes} from 'node:crypto';
import {writeFile} from 'node:fs/promises';
import {stdin, stdout} from 'node:process';
import {createInterface} from 'node:readline/promises';

const prompt = createInterface({input: stdin, output: stdout});
try {
  const client_id = (await prompt.question('Microsoft application (client) ID: ')).trim();
  const calendar_time_zone = (await prompt.question('Mailbox calendar IANA time zone: ')).trim();
  new Intl.DateTimeFormat('en', {timeZone: calendar_time_zone});
  if (!/^[0-9a-f-]{36}$/i.test(client_id)) throw new Error('Invalid application ID');
  const calendar_id = (await prompt.question('Main calendar ID (blank for default): ')).trim();
  const secondary_calendar_id =
      (await prompt.question('Optional second calendar ID (blank to skip): ')).trim();
  const secondary_calendar_label = secondary_calendar_id ?
      (await prompt.question('Second calendar label (e.g. Birthdays, max 32 UTF-8 bytes): '))
          .trim() :
      '';
  if (Buffer.byteLength(secondary_calendar_label) > 32) throw new Error('Label too long');
  const secondary_calendar_time_zone = secondary_calendar_id ?
      (await prompt.question('Second calendar IANA time zone (blank for mailbox zone): ')).trim() :
      '';
  if (secondary_calendar_time_zone)
    new Intl.DateTimeFormat('en', {timeZone: secondary_calendar_time_zone});
  const certificate = (await prompt.question('TLS certificate file (PEM, full chain): ')).trim();
  const private_key = (await prompt.question('TLS private key file (PEM): ')).trim();
  const config = {
    client_id,
    tenant: 'common',
    calendar_id,
    secondary_calendar_id,
    secondary_calendar_label,
    secondary_calendar_time_zone,
    calendar_time_zone,
    device_token: randomBytes(32).toString('base64url'),
    certificate,
    private_key,
    bind: '127.0.0.1',
    port: 8443,
    category_map: []
  };
  await writeFile(
      'adapter.local.json', JSON.stringify(config, null, 2) + '\n', {mode: 0o600, flag: 'wx'});
  stdout.write(
      'Created adapter.local.json. Open it locally to copy the pairing secret and set the listening address/port. Keep it private.\n');
} catch (error) {
  console.error(
      error.code === 'EEXIST' ? 'Configuration already exists; edit it locally.' :
                                'Configuration failed. Check the entered values.');
  process.exitCode = 1;
} finally {
  prompt.close();
}
