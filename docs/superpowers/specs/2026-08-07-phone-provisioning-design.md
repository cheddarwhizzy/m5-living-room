# Phone-based provisioning for the M5 Dial

Date: 2026-08-07
Status: Approved, not yet implemented

## Problem

Wi-Fi credentials and the Home Assistant token are compile-time `#define`s in
`include/WiFiConfig.h`. Changing any of them means editing a header, rebuilding,
and flashing. The file is also tracked in git, so a live HA token and the Wi-Fi
password are in the repository history.

The goal is to configure the dial from a phone, the way an IoT device normally
is.

## Approach

The dial hosts a captive portal over its own access point. No app is installed
on the phone; setup happens in the browser.

An app was considered and rejected. A native app is a second codebase to
maintain, and Espressif's off-the-shelf provisioning apps only handle Wi-Fi —
neither solves the actual hard part, which is getting a ~180-character HA JWT
onto the device. A web form does, because it accepts a paste from a password
manager.

### Credential model

The portal takes a long-lived HA token pasted into a form.

HA's OAuth2 (IndieAuth) flow is the better long-term answer: the credential
becomes scoped and revocable from HA's own UI, and no raw JWT is handled by
hand. It is deferred rather than dismissed. Its real payoff is provisioning
devices the operator does not control, and this is one dial on a private
network. The refresh-on-expiry path is also where this class of firmware tends
to become unreliable — a failed refresh at 03:00 is a dead dial.

`DeviceConfig` is therefore designed as the seam. Moving to OAuth later changes
that one unit, not the HA client and not the portal.

## Provisioning flow

```
boot
 │
 ├─ button held at power-on? ──────────────┐
 │                                          │
 ├─ creds in NVS? ── no ───────────────────┤
 │         │                                │
 │        yes                               ▼
 │         │                        ┌──────────────────┐
 │         ▼                        │ PROVISIONING     │
 │   connect wifi                   │                  │
 │         │                        │ SoftAP + captive │
 │    fail 3x / ~2min ─────────────▶│ portal @         │
 │         │                        │ 192.168.4.1      │
 │      success                     │                  │
 │         │                        │ screen shows QR  │
 │         ▼                        └────────┬─────────┘
 │   normal operation                        │
 │   (OTA, HA, scenes)  ◀─── save + reboot ──┘
```

### Triggers

Three, deliberately.

**No credentials in NVS.** The first-boot case.

**Sustained Wi-Fi failure** — three retries over roughly two minutes. This covers
a changed Wi-Fi password or a relocated dial, which would otherwise require a
USB cable. The delay exists so a router reboot does not drop the dial into
provisioning when it should simply retry. While the portal is up the dial keeps
retrying the saved credentials in the background; if the cause was transient the
dial heals itself and leaves provisioning with no interaction.

**Button held at power-on.** Necessary because sustained-failure does not cover
the case where Wi-Fi succeeds but the HA token is rejected — the dial is online
and useless, with no other way in. Holding at power-on cannot fire accidentally
and does not collide with the existing long-press-for-area gesture.

Provisioning is not exposed in a touchscreen settings menu. That is more UI to
build and maintain for something touched roughly twice a year.

### On-screen during provisioning

The dial has a screen, which removes the worst part of captive-portal setup.
`M5GFX` provides `qrcode()` (`LGFXBase.hpp:916`), so the dial renders a Wi-Fi
join code:

```
WIFI:S:M5Dial-Setup;T:WPA;P:<random>;;
```

Point a camera at the dial, tap the notification, the phone joins. iOS then
auto-opens the captive portal; Android requires browsing to `192.168.4.1`.

### AP security

The AP password is **randomly generated per provisioning session** and shown on
screen.

This is what makes it acceptable to carry a token over that AP in cleartext.
HTTPS on a SoftAP with no real certificate is theater, so the mitigations are
operational: the secret is fresh each time, readable only by someone physically
looking at the dial, and the AP is torn down the moment provisioning succeeds. A
10-minute idle timeout reboots the dial so it cannot sit exposed if the operator
walks away.

## Components

`ESPAsyncWebServer` and `AsyncTCP` are already declared in `platformio.ini` but
unused. The portal uses them rather than adding a web stack.

| Unit | Responsibility | Depends on |
|---|---|---|
| `include/DeviceConfig.h` | Owns the credentials. Reads/writes NVS, reports `isConfigured()`. Single source of truth for SSID, password, HA URL, HA token, OTA password | `Preferences` |
| `include/ProvisioningPortal.h` | SoftAP, captive DNS, web form. Hands validated values to `DeviceConfig` | `DeviceConfig`, `ESPAsyncWebServer` |
| `include/ProvisioningUI.h` | Draws QR code, AP password, status | `M5GFX` |
| `include/HomeAssistant.h` (modified) | Takes URL and token from `DeviceConfig` instead of `#define`s | `DeviceConfig` |

The boundary that matters: `ProvisioningPortal` never touches HA, and
`HomeAssistant` never touches NVS. `DeviceConfig` is the seam between them, and
is what makes the later OAuth swap a single-file change.

`WiFiConfig.h` becomes compile-time defaults only — gitignored and optional. It
lets a freshly flashed dial self-provision without the portal. NVS values always
win.

## The form

```
┌─────────────────────────────┐
│  M5 Dial Setup              │
│                             │
│  Wi-Fi network   [ ▾ scan ] │
│  Password        [ •••••• ] │
│                             │
│  Home Assistant URL         │
│  [ https://...            ] │
│                             │
│  Long-lived access token    │
│  [ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  ] │
│                             │
│  [ Test & Save ]            │
└─────────────────────────────┘
```

The network list comes from `WiFi.scanNetworks()`, with a manual-entry option
for hidden SSIDs. The token field is a textarea sized as a paste target.

Single page, inline CSS, no external assets — it must render with no internet,
because the phone is joined to an AP with no uplink.

**"Test & Save" actually tests.** The dial joins the Wi-Fi, calls HA's `/api/`
with the token, and commits to NVS only on a real HTTP 200. Failures return to
the form with the specific cause ("Wi-Fi connected, HA rejected the token").

This is the most important detail in the design. Saving unverified credentials
means rebooting into a broken state, which means a USB cable — exactly the
outcome this work exists to eliminate.

## Error handling

| Condition | Behaviour |
|---|---|
| Bad Wi-Fi password | Caught by Test; form stays up with the reason |
| Bad HA token | Caught by Test; form stays up with the reason |
| Portal open, nobody connects | 10-minute timeout, reboot, retry saved credentials |
| Wi-Fi drops during normal operation | Existing reconnect logic. Does **not** open the portal — only sustained failure at boot does |
| NVS empty or corrupt | Treated as unconfigured; portal opens |

## Testing

There is no test harness in this project, and building a unit-test suite for
ESP32 firmware is not justified here. Verification is manual against the real
dial, as with the OTA work, and results are reported as observed rather than
assumed:

1. Erase NVS; confirm the portal appears and the QR code scans.
2. Provision from a phone; confirm scenes load.
3. Reboot; confirm normal startup with no portal.
4. Provision with a deliberately wrong token; confirm Test rejects it and the
   dial does not reboot into a broken state.
5. Hold the button at power-on; confirm the portal opens despite valid
   credentials.

## Risks

**This touches `setup()` and the HA client's credential path** — the code that
makes the dial function at all. A defect here strands the device on USB
recovery. Mitigating factors: the `WiFiConfig.h` defaults remain as a fallback,
and OTA makes iteration cheap.

**This work depends on OTA landing first.** OTA is in PR #1 and unmerged at time
of writing. Without it, every provisioning iteration needs a USB cable — and
worse, the OTA password itself is one of the values `DeviceConfig` will own, so
building provisioning first would mean reworking it.

**The HA token currently in git history is not removed by this work.** Moving
credentials to NVS stops new secrets being committed; it does not rotate the
existing one. The token in `WiFiConfig.h` expires in 2036 and should be rotated
in HA independently of this change.

## Out of scope

- HA OAuth2 / IndieAuth (deferred; `DeviceConfig` is the seam for it)
- Rotating or purging the existing token from git history
- A touchscreen settings menu
- Configuring anything beyond Wi-Fi, HA URL, and HA token
