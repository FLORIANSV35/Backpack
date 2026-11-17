// Adapted from the ExpressLRS Backpack project (lib/MSP/msptypes.h)
// https://github.com/ExpressLRS/Backpack — GPL-3.0-only
// Copyright (c) ExpressLRS contributors
//
// Only the opcodes actually used by the Timer Backpack bridge are kept.
#pragma once

// Generic (Betaflight-style) MSP opcode, not ELRS-specific — the radio's TX
// backpack sends this over ESP-NOW on a VTx channel change, to relay to the
// timer over serial.
#define MSP_SET_VTX_CONFIG                      89

// ELRS specific opcodes
#define MSP_ELRS_BIND                           0x09
#define MSP_ELRS_GET_BACKPACK_VERSION           0x10
#define MSP_ELRS_SET_SEND_UID                   0x00B5

// Opcode OSD propriétaire ELRS (même numéro que MSP_DISPLAYPORT de
// Betaflight — 182/0xB6 — mais PAS la même sémantique : pas de heartbeat
// requis ici, juste clear/write/draw). C'est ce que le vrai plugin
// RotorHazard (VRxC_ELRS) envoie pour pousser les temps de tour dans l'OSD
// des lunettes via le backpack ; sous-commandes dans payload[0], vérifiées
// contre son code source (elrs_backpack.py / msp.py).
#define MSP_ELRS_SET_OSD                        0x00B6
#define MSP_DP_CLEAR_SCREEN                     2
#define MSP_DP_WRITE_STRING                     3
#define MSP_DP_DRAW_SCREEN                      4

// outgoing, originating from the backpack / forwarded to the VRx
#define MSP_ELRS_BACKPACK_SET_RECORDING_STATE   0x0305

// incoming, originating from the VRx / RotorHazard
#define MSP_ELRS_BACKPACK_SET_MODE              0x0380  // enable binding mode
