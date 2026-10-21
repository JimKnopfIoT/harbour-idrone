# harbour-idrone

**A passive drone detector and proximity early-warning app for Sailfish OS.**

harbour-idrone listens for the radio signals that drones broadcast anyway — it
**never transmits, never connects, never interferes**. It decodes the broadcast
**Remote ID** that many current drones are required to send, and warns you when a
drone comes close. It is a defensive, on-device tool: everything runs locally,
there is no telemetry and no cloud component.

> ⚠️ **Scope & legality.** This is a passive *receiver*, not a radar and not a
> countermeasure. It only decodes signals that are already being broadcast in
> the clear. It does not jam, spoof, deauthenticate or otherwise interfere with
> anything. Whether and how you may use radio-analysis tools can depend on your
> jurisdiction — you are responsible for using it lawfully.

## What it can and cannot see

- **Can:** drones broadcasting standard Remote ID over **Bluetooth LE** (legacy
  advertising) and **Wi-Fi** (beacon vendor IE), decoded on the phone with no
  root and no accessories.
- **Cannot:** radio-silent / fully autonomous drones that emit nothing. No
  transmission, no detection.

The phone-only core is self-contained. Optional capability tiers (Bluetooth 5
Long Range, USB Wi-Fi monitor mode, external sniffers, SDR) are strictly
additive and never required for the core to build or run.

One such optional tier is now supported: an **external CatSniffer** (running NCC
Group's Sniffle firmware) extends reception to **Bluetooth 5 Long Range (Coded
PHY)** extended-advertising Remote ID, well beyond the phone's own BLE range. It
is driven by a small least-privilege serial helper that only *reads* the device
and forwards decoded packs to the app over D-Bus — still passive, still
receive-only. When present it is auto-detected and shown in the on-screen sensor
indicator; when absent the app runs exactly as before on the phone alone.

## How it works

A small C++/Qt backend runs two receive-only sensors and fuses their output
into a single contact model:

| Channel | Source | Privilege |
|---------|--------|-----------|
| Bluetooth LE | BlueZ LE scan over system D-Bus (Remote ID service data) | none |
| Wi-Fi | wpa_supplicant BSS scan, Remote ID vendor IE walk | none |

Both channels are decoded by one parser, the bundled
[`opendroneid-core-c`](https://github.com/opendroneid/opendroneid-core-c)
(Apache-2.0, included as a git submodule). Contacts are keyed on the Remote ID
serial (not the MAC, which rotates), the phone's GPS gives a haversine distance,
and a configurable threshold raises a proximity alarm. A self-contained
radar/map view (own position centred, range rings, OpenStreetMap silhouette
background) shows the bearing and distance of nearby aircraft. The operator's
broadcast position is deliberately never displayed.

The UI is bilingual (English source strings, German translation) and loads the
matching locale automatically.

## Privacy

- All processing is **on-device**. No analytics, no upload.
- The only network access is the **opt-in** OpenStreetMap map background; no
  drone data ever leaves the device.
- This repository contains **no** real device identifiers, serial numbers, MAC
  addresses, GPS coordinates or personal data, and it must stay that way. Any
  sample/demo data is synthetic.

## Building

Requires the [Sailfish OS SDK](https://sailfishos.org/develop/) (build engine +
a target for your device). From the project root:

```sh
git submodule update --init --recursive     # pulls opendroneid-core-c
mb2 -t SailfishOS-<version>-aarch64 build    # produces an RPM under RPMS/
```

Install the resulting RPM on the device:

```sh
scp RPMS/harbour-idrone-*.aarch64.rpm <device>:/tmp/
ssh <device> 'pkcon install-local -y /tmp/harbour-idrone-*.aarch64.rpm'
```

## Status & responsible use

**Proof of concept / work in progress**, shared **as is** with **no warranty**
of any kind (see the GPLv3). Detection is best-effort: expect misses and never
treat a quiet screen as proof that nothing is there. It is an aid, not a
guarantee.

harbour-idrone is a **passive, defensive** tool — it only receives. How you use
it is your responsibility.

## Optional hardware: setting up the CatSniffer

The external Bluetooth 5 Long Range tier uses an **Electronic Cats CatSniffer v3.2**
(TI CC1352P7). Two things are worth knowing up front — both cost us time:

**Fit a 2.4 GHz antenna.** The CC1352P7 is multi-band; Remote ID over Bluetooth
lives on **2.4 GHz**, but the antenna bundled with the CatSniffer is *not
necessarily* a 2.4 GHz one — the unit we used shipped with a **~868 MHz (sub-GHz)**
antenna, which badly cripples BLE range. Antennas are frequently unmarked, so
verify before trusting one: on a VNA the S11 return-loss dip should sit near
2.4 GHz, or simply fit a known-good 2.4 GHz antenna. **Recommendation: CatSniffer
v3.2 with a 2.4 GHz antenna.**

**Flash Sniffle firmware.** The CatSniffer ships with TI's packet-sniffer
firmware; Bluetooth 5 Long Range (Coded PHY) needs NCC Group's
[Sniffle](https://github.com/nccgroup/Sniffle). Flash it with Electronic Cats'
Catnip uploader (the on-board RP2040 drives the CC1352 bootloader — no button
press needed):

- Use the **`sniffle_cc1352p7_1M.hex`** build — `p7` matches the CC1352**P7**, and
  the `_1M` suffix selects the 921600-baud rate the CatSniffer's USB bridge uses.
  Flashing the wrong chip variant can lock you out of the bootloader, so
  double-check it.
- **Power-cycle** the device after flashing (unplug/replug) — otherwise it comes
  up silent.

Once it runs Sniffle, plug it into the phone and start the app: the CatSniffer is
auto-detected, shown in the on-screen sensor indicator, and its Long Range Remote
ID appears alongside the phone's own Bluetooth/Wi-Fi contacts. A small
least-privilege system service reads the device at 921600 baud and forwards the
decoded Remote ID to the sandboxed app over D-Bus — nothing to configure at
runtime.

AI assisted development

## License

This project is licensed under the **GNU General Public License v3.0 or later**
(see [`LICENSE`](LICENSE)). The bundled parser `opendroneid-core-c` is
Apache-2.0 (compatible; its license travels with the submodule). Map data ©
OpenStreetMap contributors (ODbL) — please observe their terms of use.
