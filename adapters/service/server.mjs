// SPDX-License-Identifier: MIT
import {LogLevel, PublicClientApplication} from '@azure/msal-node';
import {readFile, rename, stat, writeFile} from 'node:fs/promises';
import {createServer} from 'node:https';

import Feed from '../feed.js';

import {calendarFeed} from './outlook.mjs';

const configPath = process.env.PACE_ADAPTER_CONFIG || 'adapter.local.json';
const cachePath = process.env.PACE_TOKEN_CACHE || 'token-cache.local.json';

async function privateFile(path) {
  const metadata = await stat(path);
  if (process.platform !== 'win32' && (metadata.mode & 0o077))
    throw new Error('Private configuration/cache files must have mode 600');
  return readFile(path, 'utf8');
}

const config = JSON.parse(await privateFile(configPath));
if (typeof config.device_token !== 'string' || config.device_token.length < 43 ||
    !/^[a-zA-Z0-9.-]+$/.test(config.tenant))
  throw new Error('Invalid adapter configuration');
new Intl.DateTimeFormat('en', {timeZone: config.calendar_time_zone});
const cachePlugin = {
  beforeCacheAccess: async context => {
    try {
      context.tokenCache.deserialize(await privateFile(cachePath));
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  },

  afterCacheAccess: async context => {
    if (context.cacheHasChanged) {
      await writeFile(`${cachePath}.tmp`, context.tokenCache.serialize(), {mode: 0o600});
      await rename(`${cachePath}.tmp`, cachePath);
    }
  }
};
const client = new PublicClientApplication({
  auth:
      {clientId: config.client_id, authority: `https://login.microsoftonline.com/${config.tenant}`},
  cache: {cachePlugin},
  system: {
    loggerOptions: {logLevel: LogLevel.Error, piiLoggingEnabled: false, loggerCallback: () => {}}
  }
});
const scopes = ['https://graph.microsoft.com/Calendars.ReadBasic'];
if (process.argv.includes('--login')) {
  try {
    await client.acquireTokenByDeviceCode(
        {scopes, deviceCodeCallback: response => console.log(response.message)});
    console.log('Authorization saved. Start the adapter with npm start.');
  } catch {
    console.error('Authorization failed; check application registration and tenant policy.');
    process.exitCode = 1;
  }
} else {
  let busy = false;
  const server = createServer(
      {
        cert: await readFile(config.certificate),
        key: await readFile(config.private_key),
        minVersion: 'TLSv1.2'
      },
      async (request, response) => {
        response.setHeader('Content-Type', 'application/json');
        response.setHeader('Cache-Control', 'no-store');
        const send = (status, data) => {
          response.writeHead(status);
          response.end(JSON.stringify(data));
        };
        if (request.method !== 'POST' || request.url !== '/feed') {
          send(404, Feed.error('invalid_request'));
          return;
        }
        if (busy) {
          send(503, Feed.error('temporarily_unavailable'));
          return;
        }
        busy = true;
        try {
          let size = 0;
          const chunks = [];
          for await (const chunk of request) {
            size += chunk.length;
            if (size > 2048) throw new Error('invalid_request');
            chunks.push(chunk);
          }
          const preferences =
              Feed.authenticate(Buffer.concat(chunks).toString('utf8'), config.device_token);
          const accounts = await client.getTokenCache().getAllAccounts();
          if (accounts.length !== 1) throw new Error('reauthorization_required');
          let token;
          try {
            token = await client.acquireTokenSilent({scopes, account: accounts[0]});
          } catch {
            throw new Error('reauthorization_required');
          }
          send(200, await calendarFeed(config, preferences, token.accessToken));
        } catch (error) {
          if (!response.destroyed) send(200, Feed.error(error.message));
        } finally {
          busy = false;
        }
      });
  server.requestTimeout = 10000;
  server.headersTimeout = 10000;
  server.timeout = 30000;
  server.maxConnections = 4;
  server.listen(
      config.port, config.bind,
      () => console.log('PACE Outlook adapter ready. Request bodies and tokens are not logged.'));
}
