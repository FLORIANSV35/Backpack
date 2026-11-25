# ExpressLRS Backpack

[![Release](https://img.shields.io/github/v/release/ExpressLRS/Backpack?include_prereleases)](https://github.com/ExpressLRS/Backpack/releases)
[![License](https://img.shields.io/github/license/ExpressLRS/Backpack)](https://github.com/ExpressLRS/Backpack/blob/master/LICENSE)
[![Chat](https://img.shields.io/discord/596350022191415318)](http://discord.gg/dS6ReFY)
[![Open Collective backers](https://img.shields.io/opencollective/backers/expresslrs?label=Open%20Collective%20backers)](https://opencollective.com/expresslrs)

## About this branch (`graphn-radio-bkpk`)

This branch tracks upstream `master` with no code changes yet, scoped down
to a single radio: the **RadioMaster GX12** (ESP32-C3 internal TX module,
firmware `ESP32C3_TX_Backpack`).

**Build scope:** `platformio.ini` only includes `targets/txbp_esp.ini` on
this branch, so `pio run` builds/lists just the TX backpack environments
(`ESP_TX_Backpack_*` for ESP8285 radios and `ESP32C3_TX_Backpack_*` for the
GX12 and other ESP32-C3 radios — both families live in the same `.ini`
file). Other vendors' `targets/*.ini` and `src/*.cpp` files are untouched on
disk (not deleted) so future `upstream/master` merges stay simple.

**Configurator scope:** `hardware/targets.json` only lists the
`radiomaster` vendor, and only its `gx12` entry, on this branch — that's
what a Configurator pointed at this fork will show. Every other
vendor/product was removed from this file; their `targets/*.ini`
environments still exist and still build, they're just not in the device
picker.

**Firmware version:** `python/elrs_helpers.py` reports a fixed version,
`1.5.9-graphn`, instead of deriving it from `git describe`/branch name —
see [graphn-hdz-bkpk](https://github.com/FLORIANSV35/Backpack/tree/graphn-hdz-bkpk)
for why.

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
