# Timer Backpack for ESP32-C3 with Bluetooth (Arduino sketch)

## About this branch (`timer-esp32c3-ble-lr`)

Branched from [timer-esp32c3-ble](https://github.com/FLORIANSV35/Backpack/tree/timer-esp32c3-ble),
adding one change: ESP-NOW frames are forced to WiFi LR (long range) PHY via
`esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_LORA_500K)` in
`SetSoftMACAddress()` (`TimerBackpack.cpp`), on top of `WIFI_PROTOCOL_LR`
already being advertised on the interface. Re-applied on every rebind (not
just once at boot), since `SetSoftMACAddress()` also runs from
`adoptGroupAddress()`.

**Every device in the bind group must run an `-lr` branch build for this to
work** — LR is a different PHY modulation, not just a slower rate, so a
normal-rate peer can't decode LR frames at all (and vice versa). Pairs with
[graphn-hdz-bkpk-lr](https://github.com/FLORIANSV35/Backpack/tree/graphn-hdz-bkpk-lr),
[graphn-rapidf-bkpk-lr](https://github.com/FLORIANSV35/Backpack/tree/graphn-rapidf-bkpk-lr)
and
[graphn-radio-bkpk-lr](https://github.com/FLORIANSV35/Backpack/tree/graphn-radio-bkpk-lr).

Unlike the other three `-lr` branches, this one has no runtime web UI toggle
(this sketch has no AsyncWebServer/config page — it's BLE/USB-MSP only), so
LR is hardcoded on here; use the plain `timer-esp32c3-ble` branch instead if
you need to bind a non-LR device.

A standalone Arduino sketch (not a PlatformIO environment of this repository),
adapted from this project's `src/Timer_main.cpp`, for an **ESP32-C3** board.
It is the timer backpack of the [Drone Scan Tool](https://github.com/FLORIANSV35/Drone-Scan-Tool)
race timer, and also works as a plain Timer Backpack for RotorHazard.

What it adds compared to `Timer_main.cpp`:

- **Bluetooth (Nordic UART service) as a second MSP link**, equivalent to the USB
  serial port: MSP frames in both directions, mixed with a few text commands.
  Everything the backpack relays from the radio (`DVR Rec`, `MSP_SET_VTX_CONFIG`,
  version reply) goes out on both USB and Bluetooth; frames received over
  Bluetooth are handled exactly like frames received over USB.
- `LAPTIMER:OSD:<phrase>|<text>` text command: shows text on one pilot's OSD
  (multi-line with `~`) and answers `ACK:OSD=SENT|ESPNOW_FAIL`.
- `BACKPACK:PHRASE:<phrase>` / `BACKPACK:PHRASE?` / `BACKPACK:BIND` over Bluetooth
  (the backpack restarts when the phrase changes, so the radio source address
  that VRx backpacks filter on is updated).
- `BLE:PHY:1M|2M|CODED`: asks the phone for a Bluetooth PHY (LE PHY Update) and
  reports the one obtained (`ACK:BLEPHY=...`).
- The BLE name is in the advertising packet (`DST-Backpack`), so Android shows names.

The bind phrase is stored on the device only (`config.h` has a placeholder).

## Build

```
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc .
arduino-cli upload  -p <port> --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc .
```

Needs the NimBLE-Arduino library (2.x). `USB CDC On Boot` must be enabled (the
USB port is the MSP link). LED / bind button default to GPIO 8 / GPIO 9
(see `config.h`). Verified on hardware: OSD on HDZero goggles over ESP-NOW,
MSP version request over USB and Bluetooth; not verified against a live
RotorHazard.

GPL-3.0-only, like the rest of this repository.
