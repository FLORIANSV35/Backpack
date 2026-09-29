# ExpressLRS Backpack

[![Release](https://img.shields.io/github/v/release/ExpressLRS/Backpack?include_prereleases)](https://github.com/ExpressLRS/Backpack/releases)
[![License](https://img.shields.io/github/license/ExpressLRS/Backpack)](https://github.com/ExpressLRS/Backpack/blob/master/LICENSE)
[![Chat](https://img.shields.io/discord/596350022191415318)](http://discord.gg/dS6ReFY)
[![Open Collective backers](https://img.shields.io/opencollective/backers/expresslrs?label=Open%20Collective%20backers)](https://opencollective.com/expresslrs)

## About this branch (`graphn-rapidf-bkpk`)

This branch tracks upstream `master` and adds one patch on top, for the
`Rapidfire_ESP_RX_Backpack_via_UART` target (ESP8266/ESP8285 VRX backpack
wired to a rapidFIRE module over UART, e.g. a repurposed BetaFPV ELRS Lite
receiver):

- **RF/bus silence while armed.** The arm switch (mirrored via the VTX
  Administrator's "recording" state) now gates all outbound traffic from the
  backpack: no SPI command reaches the rapidFIRE module, and no ESP-NOW frame
  leaves the backpack, for the duration of the flight. No frequency change is
  possible while armed.
- **Single beep on frequency change.** The rapidFIRE beeps once per accepted
  band-set command and once per accepted channel-set command. The stock
  firmware resent the band-set unconditionally on every channel hop, giving 2
  beeps even when staying in the same band. The band-set is now only resent
  when the band actually changed.

Also includes an unrelated one-character fix: `#include <config.h>` →
`#include "config.h"` in `module_base.cpp`, to force resolution to this
project's own `config.h` instead of a same-named header from some
third-party library that may be installed under Arduino IDE (no effect
under PlatformIO).

Scope: only `src/rapidfire.h`, `src/rapidfire.cpp`, `src/Vrx_main.cpp` and
`src/module_base.cpp` are modified; nothing else in the repo differs from
upstream. This target has no telemetry (GPS/battery/link), OSD,
head-tracking or RTC support — only band/channel switching, gated as above.

**Build scope:** `platformio.ini` only includes `targets/rapidfire.ini` on
this branch, so `pio run` builds/lists just the Rapidfire VRX environments.
Other vendors' `targets/*.ini` and `src/*.cpp` files are untouched on disk
(not deleted) so future `upstream/master` merges stay simple.

**Configurator scope:** `hardware/targets.json` only lists the `rapidfire`
vendor on this branch — that's what a Configurator pointed at this fork will
show. Every other vendor was removed from this file; their `targets/*.ini`
environments still exist and still build, they're just not in the device
picker.

The ExpressLRS Backpack adds ESP-NOW–based wireless communication between ExpressLRS TX modules and compatible FPV hardware, allowing for remote configuration, control, and telemetry exchange. Developed and maintained by **ExpressLRS LLC** and its passionate open source community, working together to advance reliable, high-performance radio control technology.

ExpressLRS Backpack is developed and maintained by **ExpressLRS LLC** and its passionate open source community, working together to advance reliable, high-performance radio control technology.

## What is a "TX Backpack"?

Some of the ExpressLRS TX modules include an additional ESP8285 chip, which lets us communicate wirelessly with other ESP8285 enabled devices using a protocol called espnow. We call this chip the "TX-Backpack". The aim of the TX-Backpack is to allow wireless communication between ExpressLRS, and other FPV related devices for command and control, or for querying config.

## Sounds interesting... What type of FPV devices can it talk to?
 A prime use case is your video receiver module (or VRX). Currently there aren't many VRX modules that have an ESP8285 built in to allow them to communicate with ExpressLRS, so in most cases you need to add your own. A small ESP based receiver can be "piggybacked" onto your VRX module, which allows ExpressLRS to control the band and channel that your goggles are set to. We call this device the "VRX-Backpack". 

## Wow cool, so I'll be able to control the module via ELRS!? Which VRX modules does it work with?
The list of supported modules can be found on the wiki:
https://github.com/ExpressLRS/Backpack/wiki

## Great! I use one of the supported modules. How do I get a VRX-Backpack?
There are a few different options for both DIY or compatible off the shelf backpacks... check the wiki for the current list:
https://github.com/ExpressLRS/Backpack/wiki
