// BLE transport for scan mode, mirroring the plain-text serial protocol
// (STEP:/RANGE:/ACK:/BOOT:/JSON drone lines/...) over a Nordic UART Service
// (NUS) -style GATT service, so the web app/APK can connect wirelessly
// where Web Serial isn't available (Android has no Web Serial — only
// WebUSB, which this board's UART bridge can't speak — so this is the only
// realistic wireless path for the Android APK).
//
// Also used in backpack mode, but then exclusively — see setBleOnly()
// below — since backpack mode's real Serial is the MSP link to RotorHazard
// and must not have anything else mixed into it.
//
// Not yet tested on hardware (no board connected while this was written).
// Needs, once it is: a real BLE central (Chrome on a computer or Android)
// connecting, checking lines arrive intact for both short (ACK:) and long
// (JSON drone/{"id":...}) lines, and checking the RX command path (typing
// RANGE:/STEP:/etc from the web app over BLE) actually reaches
// handleCommand(). See the chunking note in BleUart.cpp for the main risk
// (lines longer than the negotiated MTU).
#pragma once

#include <Arduino.h>

// DualSerial: a Stream that forwards everything written to it to the real
// hardware Serial *and* to any connected BLE central, and reads from
// whichever of the two (hardware Serial or the BLE RX characteristic) has
// bytes waiting. drone_scan_tool.ino does `#define Serial IO` right after
// including this header, so every existing Serial.print/read call in that
// file is redirected here automatically — BleUart.cpp itself does NOT
// define that macro, so it can still reach the real Serial object directly.
class DualSerial : public Stream {
public:
  void begin(unsigned long baud);

  // Stream
  int available() override;
  int read() override;
  int peek() override;
  void flush();

  // Print
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;

  // In backpack mode the real Serial is 100% reserved for RotorHazard's MSP
  // stream: nothing else may read from or write to it. Call with true right
  // before backpackSetup() so IO (the scan code's "Serial") only talks to
  // BLE from then on, while the RX5808 sweep + beacon sniffer keep running
  // underneath, output reachable over Bluetooth instead of USB.
  void setBleOnly(bool bleOnly);
};

extern DualSerial IO;

// Starts advertising the BLE UART service. Call once from setup(), after
// IO.begin(). Safe to call even if the BLE stack ends up not being used
// (e.g. nobody connects) — scan mode's own behaviour doesn't depend on it.
void bleUartBegin(const char *deviceName);

// PHY du lien BLE, négocié au niveau contrôleur avec le téléphone (procédure
// LE PHY Update — pas besoin que Chrome/Web Bluetooth la gère) : phy=3
// demande le PHY "Coded" S=8 (Long Range, portée maxi, débit ~125 kbit/s),
// 2 le 2M (rapide, portée réduite), 1 le 1M normal. Rien n'est garanti : il faut que le contrôleur
// du téléphone le supporte — le PHY réellement obtenu est annoncé par
// "ACK:BLEPHY=..." (voir bleUartPhyPoll), pas supposé. Le choix est ré-appliqué
// à chaque nouvelle connexion tant que l'ESP n'a pas redémarré.
// Retourne false si la pile BLE a refusé la demande (pas de PHY changé).
bool bleUartSetPhy(uint8_t phy); // 1=1M (normal), 2=2M (rapide, portée réduite), 3=Coded (Long Range)
// PHY actuel du premier client connecté (1=1M, 2=2M, 3=Coded). false si personne.
bool bleUartGetPhy(uint8_t *tx, uint8_t *rx);
// À appeler régulièrement depuis loop() : envoie "ACK:BLEPHY=<1M|2M|CODED>"
// quand le PHY du lien vient de changer.
void bleUartPhyPoll();
// Traite "BLE:PHY:CODED|2M|1M" et "BLE:PHY?" (réponses "ACK:BLEPHY=…" via IO).
// Retourne false si la commande n'est pas une commande BLE:PHY.
bool bleUartHandlePhyCommand(const char *cmd);
