// Drone Scan Tool — variante "Timer Backpack seul" pour ESP32-C3.
//
// Le scan RX5808 et le sniffer de balises sont dans l'autre firmware (ESP32-S3,
// ../drone_scan_tool/) ; ici, uniquement le backpack. Seul BleUart.* est
// partagé avec lui (lien symbolique).
//
// Code backpack adapté d'ExpressLRS Backpack (TimerBackpack.cpp) ; la radio
// reste en permanence sur le canal ESP-NOW.
//
//   - USB (CDC, 460800 bauds)  : lien MSP avec RotorHazard, comme un Timer
//     Backpack ExpressLRS officiel (backpackSetup()/backpackLoop()).
//   - Bluetooth (service UART)  : second lien MSP, équivalent à l'USB (trames
//     MSP dans les deux sens : OSD, canal VTx, switch DVR-REC…), plus des
//     commandes texte de l'appli web — phrase, binding, OSD avec accusé,
//     PHY Bluetooth (Normal/2M/Long Range).
//
// Compilation : arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc .
#include <Arduino.h>
#include "BleUart.h"
#include "TimerBackpack.h"
#include "config.h"

static char   cmdBuf[128]; // "LAPTIMER:OSD:" (13) + phrase (64) + "|" + texte
static size_t cmdLen = 0;

static void handleCommand(const char *cmd) {
  if (strncmp(cmd, "BACKPACK:PHRASE:", 16) == 0) { backpackSetPhrase(cmd + 16); return; }
  if (strcmp(cmd, "BACKPACK:PHRASE?") == 0) { IO.print("ACK:PHRASE="); IO.println(backpackGetPhrase()); return; }
  if (strcmp(cmd, "BACKPACK:BIND") == 0) { backpackEnterBinding(); IO.println("ACK:BIND=START"); return; }
  if (strncmp(cmd, "LAPTIMER:OSD:", 13) == 0) { backpackHandleOsdCommand(cmd + 13); return; }
  if (bleUartHandlePhyCommand(cmd)) return;
  // Envoyé par l'appli web à chaque connexion.
  if (strcmp(cmd, "SCANAPP:HELLO") == 0) { IO.println("ACK:HELLO=BACKPACK"); return; }
}

void setup() {
  // Le Serial réel est le lien MSP avec RotorHazard : IO (donc tout ce
  // fichier) ne parle qu'en Bluetooth.
  IO.setBleOnly(true);
  bleUartBegin("DST-Backpack");
  backpackSetup();
}

void loop() {
  backpackLoop();
  bleUartPhyPoll();
  while (IO.available()) {
    char c = (char)IO.read();
    if (backpackBleFeed((uint8_t)c)) continue; // trame MSP (Bluetooth = USB)
    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) { cmdBuf[cmdLen] = '\0'; handleCommand(cmdBuf); cmdLen = 0; }
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}
