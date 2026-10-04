# Remote unlock plan (agreed 2026-10-04)

Goal: unlock the cage from anywhere through the Base44 app (`polkproduction.base44.app`),
with per-person accountability, a permanent log, and nothing exposed on the shop router.

## Architecture

```
Base44 app "Unlock" (logged-in admin/manager)
  -> Base44 function cageUnlock
  -> HTTPS PUT https://cage-api.<domain>/unlock        (Cloudflare Access service token)
  -> Cloudflare Tunnel -> Raspberry Pi at the shop (cloudflared)
  -> lock's local API  http://<lock IP>/unlock         (lock API key)
  -> lock: countdown on screen, relay pulse, logs relay_pulse with cmd=<id>

Lock log (LittleFS CSV)
  -> Node-RED on the Pi reads http://<lock IP>/log?since=... every ~15 s (LAN, free)
  -> when there are new events: POST to Base44 function cageDeviceSync (events mode)
  -> heartbeat (current /status) every 15 min and on door/chain change (heartbeat mode)
  -> cageDeviceSync closes the matching CageCommand when it sees relay_pulse cmd=<id>
```

Humans also get `https://cage.<domain>` (Cloudflare Access email one-time PIN) to reach the
lock's own page as a backup. **Decision pending:** make that path status-only so unlocks
always go through Base44's audit trail (see Open items).

## Why this one (options considered and dropped)

| Option | Why not |
|---|---|
| Port forwarding | Plain HTTP + API key over the internet. No. |
| Long-poll to Base44 | ~575 integration credits/month, edge timeout unverified, TLS on the ESP every 45 s |
| MQTT via EMQX Cloud | Works, but adds a third-party broker in the unlock path and TLS memory risk on the ESP8266 |
| Adafruit IO | Simple, but shared login (no per-person "by"), not integrated with Base44 |
| QR code on the OLED | 41x41 modules at 1 px didn't scan reliably on the real screen; removed |

Pi + Tunnel wins: the ESP8266 stays plain-HTTP on the LAN (no TLS memory risk), no broker,
~1 s unlock, ~35-50 Base44 integration credits/month, and the Pi (Node-RED) does the heavy lifting.

## Lock API (already in firmware, see cage_lock.ino header)

- `PUT /unlock` with `X-Api-Key`. Params in query or JSON body: `name` (for), `by`, `cmd`, `ts`.
  - 202 `{"ok":true,"fires_in_ms":N}` - countdown started, relay fires at the end.
  - 401 bad key, 409 busy, 409 `duplicate` (cmd reused; last 32 kept in flash),
    403 `stale` (|now - ts| > 60 s), 503 `clock not synced`, 400 `bad cmd`.
- `GET /status` - door, chain, state, rssi, wifi, open_s, time, boot_id, fw, relay.
- `GET /log?since=EPOCH` (key) - CSV `epoch,local_time,uptime_s,event,detail`.
  detail is `key=value;key=value`, e.g. `relay_pulse` -> `for=JP;by=Tony;cmd=abc123`,
  `door` -> `closed;open_s=192;for=JP;by=Tony`.
- `PUT /wifisetup` (key) - switch to the CageLock setup hotspot.

## Base44 side (from Base44's review; nothing built yet)

Secrets (Secrets page, SCREAMING_SNAKE like the app's existing ones):
`CF_ACCESS_CLIENT_ID`, `CF_ACCESS_CLIENT_SECRET`, `CAGE_LOCK_API_KEY`, `CAGE_DEVICE_KEY`, `CAGE_HMAC_SECRET`.

Entities (admin/manager access rules like the purchasing entities):
- **CageDevice**: device_id, name, door, chain, state, wifi_rssi, wifi_quality, open_s, ip,
  firmware_version, boot_id, last_seen, last_event_at, active, notes
- **CageEvent**: device_id, seq, event_key, boot_id, batch_id, epoch, local_time, uptime_s,
  event, detail, raw, received_at
- **CageCommand**: device_id, action, for_name, by_name, requested_by_email, status,
  idempotency_key, created_date, sent_at, fires_in_ms, completed_at, result, attempts, expires_at

`cageUnlock` (logged-in, admin/manager): IN `{device_id, for_name, note?}`.
Sends `PUT https://cage-api.<domain>/unlock` with JSON body `{name, by, cmd, ts}` and headers
`CF-Access-Client-Id`, `CF-Access-Client-Secret`, `X-Api-Key`; `redirect: "manual"`, 5 s timeout.
202 -> sent; 401/409/403/503 -> failed with reason; **3xx = Cloudflare Access refused** -> failed;
timeout -> failed. `by` comes from the server-side user, never the browser.

`cageDeviceSync` (public; `X-Device-Key` + `X-Device-Timestamp` + HMAC-SHA256 signature over body,
called by Node-RED):
```
heartbeat: {device_id, mode:"heartbeat", status:{door,chain,state,rssi,wifi,open_s,lock_time}, uptime_s, boot_id, fw}
events:    {device_id, mode:"events", batch_id, events:[{seq, event_key, epoch, local_time, uptime_s, event, detail, raw}]}
response:  {ok, accepted?, duplicates?, server_time, next_heartbeat_s: 900}
```
Dedup on Pi-assigned `seq` (persisted on the Pi) plus `event_key = device_id|boot_id|uptime_s|event`.

Command lifecycle: pending -> sent (202) -> done (relay_pulse with matching cmd arrives) |
failed | expired (UI computes from expires_at; no scheduled sweeper - costs ~860 credits/month).
UI must show "Sent - awaiting confirmation" vs "Confirmed" vs "Expired".

Status page: live via entity subscriptions; show last heartbeat AND last event; "Check now"
button (one call through the tunnel to /status); command history; don't show green "online"
on 15-minute-old data.

## Cloudflare Tunnel setup (not done yet)

1. Pi: Raspberry Pi OS Lite 64-bit, updated, default password changed, DHCP reservation.
   Router: DHCP reservation for the **lock** too (e.g. 192.168.1.50).
2. Zero Trust -> Networks -> Tunnels -> Create (Cloudflared) "shop-pi" -> Debian arm64 ->
   run the `sudo cloudflared service install <token>` command on the Pi -> shows Healthy.
3. Public hostnames on that tunnel: `cage-api.<domain>` and `cage.<domain>` -> `HTTP` -> `<lock IP>:80`.
4. Access -> Service credentials -> Service Tokens -> create "base44-cage" (copy id/secret into
   Base44 secrets; secret shown once). Access -> Applications -> self-hosted `cage-api.<domain>`,
   policy **Service Auth** with that token. Second app `cage.<domain>`, policy **Allow** listed
   emails, login method One-time PIN.
5. Test: `curl.exe -s "https://cage-api.<domain>/status" -H "CF-Access-Client-Id: <id>" -H "CF-Access-Client-Secret: <secret>"`
   -> lock JSON = good; HTML login page = token/policy wrong.

## Security decisions

- Two layers on unlock: Cloudflare Access service token AND the lock's API key.
- `ts` + `cmd` on every remote unlock; lock refuses stale, duplicate or unsynced-clock requests.
- Names travel in the JSON body, not the URL (stays out of Cloudflare/tunnel logs).
- Base44 identifies the requester server-side and mirrors commands into its EventLog.
- Node-RED -> Base44 is HMAC-signed with timestamp; device key and lock key are separate secrets.
- The Pi holds every credential: no default passwords, Node-RED admin locked down, OS patched,
  Pi itself not exposed (only the tunnel).
- Keep a physical key. If the Pi, tunnel or shop internet is down, remote unlock is down;
  the lock and its local page keep working.
