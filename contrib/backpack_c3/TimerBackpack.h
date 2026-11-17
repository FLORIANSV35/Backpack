// Timer Backpack ESP-NOW <-> MSP bridge (ESP32-C3, voir backpack_c3.ino).
//
// Adapted from the ExpressLRS Backpack project (src/Timer_main.cpp and the
// small device/LED/button helpers it uses), trimmed down to what a DIY
// timer backpack actually needs: a serial (USB) <-> ESP-NOW MSP bridge
// between RotorHazard and the pilots' VRx backpacks, plus the extras the
// Laptimer web app uses over Bluetooth (OSD pushed to a pilot's goggles,
// DVR-REC relayed as "LAPTIMER:DVR:1/0").
// https://github.com/ExpressLRS/Backpack — GPL-3.0-only
// Copyright (c) ExpressLRS contributors
//
// Dropped on purpose (not needed for this role, and not reimplemented):
//   - the ESP8266 code paths
//   - the Wi-Fi OTA update / captive portal (reflash over USB instead)
//   - the compile-time "binding phrase" baked into the binary by the
//     ExpressLRS Configurator/python build scripts (no such build step
//     here) — replaced by backpackSetPhrase(), computed at runtime with
//     the exact same MD5 scheme, so pairing with an officially-built
//     backpack using the same phrase still works.
#pragma once

#include <Arduino.h>

// Called once from setup(). Takes over Serial entirely (460800 baud, MSP
// framing) for the RotorHazard link.
void backpackSetup();

// Called every loop() iteration.
void backpackLoop();

// Derive this device's ESP-NOW group address (UID) from a human-readable
// binding phrase and persist it, using the same
// MD5('-DMY_BINDING_PHRASE="<phrase>"')[0:6] scheme as the official
// ExpressLRS Configurator. Si le backpack tourne déjà, l'ESP redémarre pour
// que l'adresse d'émission (celle que filtrent les VRx) soit la nouvelle.
void backpackSetPhrase(const char *phrase);

// Reads back the phrase last set with backpackSetPhrase(), from the ESP's
// own storage (empty string if none was ever set). The group address alone
// can't be reversed back into it, which is why it's kept separately.
const char *backpackGetPhrase();

// Calcule l'adresse ESP-NOW (6 octets) dérivée d'une bind phrase — même
// calcul que celui que fait l'ExpressLRS Configurator pour l'UID affiché
// à l'écran lors du bind.
void backpackPhraseToUid(const char *phrase, uint8_t uid[6]);

// Puts the backpack into ESP-NOW binding mode: it will adopt the group
// address sent by the next peer it hears from while binding. Also reachable
// with a short press of the BOOT button.
void backpackEnterBinding();

// Pousse une ligne de texte dans l'OSD d'UN pilote (lunettes/VTx), via le
// même mécanisme ESP-NOW que RotorHazard utilise déjà pour ses temps de
// tour — sert au Laptimer (web, piloté en BLE). La cible est dérivée du bind
// phrase de CE pilote. Le texte peut contenir plusieurs lignes séparées par
// '~' (de haut en bas, la dernière en ligne 14, chaque précédente 4 lignes
// plus haut). Pas d'effet si la phrase est vide ou pendant le binding.
// Retourne true seulement si tous les paquets ont bien été acquittés au
// niveau radio (ESP_NOW_SEND_SUCCESS) — false ne veut pas dire que l'OSD a
// changé (ça, on ne peut pas le savoir), juste qu'au moins l'envoi a échoué.
bool backpackSendOSD(const char *phrase, const char *text);

// Traite "<bind phrase du pilote>|<texte>" (ce qui suit "LAPTIMER:OSD:") et
// répond "ACK:OSD=…,UID=…" via IO. Voir backpackSendOSD().
void backpackHandleOsdCommand(const char *payload);

// Octet reçu par Bluetooth (voir backpack_c3.ino) : le Bluetooth est un second
// lien MSP, équivalent à l'USB. true = octet consommé comme partie d'une trame
// MSP ; false = texte, à traiter comme une commande.
bool backpackBleFeed(uint8_t c);
