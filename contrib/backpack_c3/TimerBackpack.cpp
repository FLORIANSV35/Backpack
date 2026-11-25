// Adapted from the ExpressLRS Backpack project (src/Timer_main.cpp and its
// device/LED/button helpers) — see TimerBackpack.h for what was dropped.
// https://github.com/ExpressLRS/Backpack — GPL-3.0-only
// Copyright (c) ExpressLRS contributors
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <MD5Builder.h>
#include <Preferences.h>

#include "TimerBackpack.h"
#include "config.h"
#include "msp.h"
#include "msptypes.h"
#include "elrs_eeprom.h"
#include "bp_config.h"
#include "BleUart.h" // pour relayer DVR-REC/OSD au Laptimer web via IO (BLE) — voir ProcessMSPPacketFromPeer()

// ═══════════════════════════════════════════════
//  State
// ═══════════════════════════════════════════════

typedef enum { bp_starting, bp_binding, bp_running } bp_state_e;

static bp_state_e connectionState = bp_starting;
static uint32_t   bindingStart    = 0;
static bool       isBinding       = false;

// Pas de struct/état à part pour l'OSD : backpackSendOSD() réutilise
// directement sendAddress + MSP_ELRS_SET_SEND_UID, exactement comme le fait
// le vrai plugin RotorHazard (VRxC_ELRS) — voir ce fichier plus bas.

static ELRS_EEPROM eeprom;
static bool         storageReady = false;

static uint8_t groupAddress[6] = {0,0,0,0,0,0}; // bound ESP-NOW group address (UID)
static uint8_t sendAddress[6]  = {0,0,0,0,0,0}; // current send target (usually == groupAddress)

static MSP msp;
static esp_now_peer_info_t peerInfo;

static QueueHandle_t rxqueue = nullptr; // packets received over ESP-NOW, to forward to Serial
static QueueHandle_t txqueue = nullptr; // packets received over Serial, to forward over ESP-NOW
static SemaphoreHandle_t sendLock = nullptr;
static bool     espnowCTS    = true;
static bool     sendSuccess  = false;
static int      sendAttempt  = 0;
static const int maxAttempt  = 5;

#define BINDING_NO_PEER_TIMEOUT  120000UL  // ms with no matching peer before binding gives up

// ═══════════════════════════════════════════════
//  Storage (lazy: usable from scan mode too, for backpackSetPhrase())
// ═══════════════════════════════════════════════

// Same MD5('-DMY_BINDING_PHRASE="<phrase>"')[0:6] scheme the ExpressLRS
// Configurator uses, factored out so it can be reused for a pilot's OWN
// bind phrase (Laptimer OSD) without touching the timer's own
// groupAddress/sendAddress (that one stays reserved for RotorHazard).
static void phraseToAddress(const char *phrase, uint8_t *outAddr) {
  char buf[112]; // "-DMY_BINDING_PHRASE=\"" + up to 64 phrase chars + "\""
  snprintf(buf, sizeof(buf), "-DMY_BINDING_PHRASE=\"%s\"", phrase);

  MD5Builder md5;
  md5.begin();
  md5.add((uint8_t*)buf, (uint16_t)strlen(buf));
  md5.calculate();
  uint8_t digest[16];
  md5.getBytes(digest);

  memcpy(outAddr, digest, 6);
  outAddr[0] &= ~0x01; // must stay a valid unicast MAC — see note at OnDataRecv
}

// Hashes `phrase` into groupAddress/sendAddress and persists it. Shared by
// backpackSetPhrase() (explicit, over serial) and ensureStorage()'s
// DEFAULT_BIND_PHRASE fallback (silent, automatic).
static void applyPhrase(const char *phrase) {
  phraseToAddress(phrase, groupAddress);
  memcpy(sendAddress, groupAddress, 6);
  bpConfig.SetGroupAddress(groupAddress);
  bpConfig.SetPhrase(phrase); // kept only so it can be read back later (backpackGetPhrase)
  bpConfig.Commit();
}

static void ensureStorage() {
  if (storageReady) return;
  eeprom.Begin();
  bpConfig.SetStorageProvider(&eeprom);
  bpConfig.Load();

  if (bpConfig.GetPhrase()[0] == '\0') {
    // Nothing set yet (factory-fresh, or after a full flash erase) — apply
    // the configured default instead of starting with an all-zero address.
    applyPhrase(DEFAULT_BIND_PHRASE);
  }

  memcpy(groupAddress, bpConfig.GetGroupAddress(), 6);
  groupAddress[0] &= ~0x01; // re-mask in case this was stored before the fix below existed
  memcpy(sendAddress, groupAddress, 6);
  storageReady = true;
}

void backpackSetPhrase(const char *phrase) {
  ensureStorage();
  applyPhrase(phrase);

  IO.printf("ACK:PHRASE=%s,UID=%02X%02X%02X%02X%02X%02X\n",
                bpConfig.GetPhrase(),
                groupAddress[0], groupAddress[1], groupAddress[2],
                groupAddress[3], groupAddress[4], groupAddress[5]);

  // Backpack déjà démarré : l'adresse MAC d'émission (celle que les VRx
  // filtrent) n'est fixée qu'au démarrage (SetSoftMACAddress) — sans
  // redémarrage, on enverrait avec l'ancienne phrase et les lunettes
  // ignoreraient tout. En mode scan (backpack pas encore lancé), rien à faire.
  if (connectionState != bp_starting) {
    delay(200);
    ESP.restart();
  }
}

// Reads back whatever phrase was last set with backpackSetPhrase(), straight
// from the ESP's own storage — the UID alone can't be reversed into it.
const char *backpackGetPhrase() {
  ensureStorage();
  return bpConfig.GetPhrase();
}

void backpackPhraseToUid(const char *phrase, uint8_t uid[6]) {
  phraseToAddress(phrase, uid);
}

// ═══════════════════════════════════════════════
//  LED (status) — GPIO defined by PIN_BACKPACK_LED
// ═══════════════════════════════════════════════

static void ledSet(bool on) { digitalWrite(PIN_BACKPACK_LED, on ? HIGH : LOW); }

static void ledUpdate(uint32_t now) {
  static uint32_t lastToggle = 0;
  static bool     on = false;
  uint32_t period;

  switch (connectionState) {
    case bp_binding: period = 150; break;             // fast blink while binding
    default:         period = on ? 1900 : 100; break; // short heartbeat blip while running
  }
  if (now - lastToggle >= period) {
    on = !on;
    ledSet(on);
    lastToggle = now;
  }
}

static void ledBlip() {
  // Lets ledUpdate's heartbeat double as "ESP-NOW activity" by forcing an
  // immediate short on-blip; simplest is just to flash it directly here.
  ledSet(true);
}

// ═══════════════════════════════════════════════
//  Button (BOOT) — tap = enter binding
// ═══════════════════════════════════════════════

static void buttonUpdate(uint32_t now) {
  static uint32_t lastChange = 0;
  static bool     pressed    = false;
  const uint32_t  DEBOUNCE_MS = 25;

  bool down = (digitalRead(PIN_BACKPACK_BUTTON) == LOW); // INPUT_PULLUP, active low

  if (down != pressed) {
    if (now - lastChange > DEBOUNCE_MS) {
      pressed = down;
      lastChange = now;
      if (!pressed) backpackEnterBinding(); // appui court au relâchement
    }
  }
}

// ═══════════════════════════════════════════════
//  ESP-NOW plumbing
// ═══════════════════════════════════════════════

// groupAddress is always already masked to a valid unicast MAC (first byte's
// LSB cleared) by whoever last set it — see the three call sites above —
// so OnDataRecv's memcmp(mac_addr, groupAddress, ...) compares against the
// exact same value this device's radio actually uses, matching what a peer
// (radio backpack, goggles) that derives the same masked address would send.
#define ESPNOW_CHANNEL 1 // see the comment on SetSoftMACAddress() below

static void SetSoftMACAddress() {
  WiFi.mode(WIFI_STA);
  // Pin a known channel explicitly: ESP-NOW only delivers between peers on
  // the *same* channel, so leaving this to whatever the radio happened to
  // default to is asking for a silent mismatch with a peer that assumes the
  // usual channel 1.
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_mac(WIFI_IF_STA, groupAddress);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  // Force the ESP-NOW-specific PHY rate to WiFi LR (long range), matching the
  // rest of the bind group (goggles, radio, rapidFIRE -lr branches). This is
  // a different modulation, not just a slower rate, so every peer must run
  // an -lr build or they simply can't decode each other's frames. Re-applied
  // here (not just once in backpackSetup()) so a rebind via adoptGroupAddress
  // keeps it in effect.
  esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_LORA_500K);
}

static void registerPeer(const uint8_t *address) {
  esp_now_del_peer(address);
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, address, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

// Adopt a group address received (over Serial or ESP-NOW) while binding.
static void adoptGroupAddress(const uint8_t *addr) {
  memcpy(groupAddress, addr, 6);
  groupAddress[0] &= ~0x01; // idempotent if already masked by the sender, as it should be
  memcpy(sendAddress, groupAddress, 6);
  bpConfig.SetGroupAddress(groupAddress);
  bpConfig.Commit();
  SetSoftMACAddress();
  registerPeer(groupAddress);
  connectionState = bp_running;
  isBinding = false;
  IO.printf("ACK:BIND=OK,UID=%02X%02X%02X%02X%02X%02X\n",
                groupAddress[0], groupAddress[1], groupAddress[2],
                groupAddress[3], groupAddress[4], groupAddress[5]);
}

// Le Bluetooth se comporte comme l'USB : tout ce que le backpack renvoie en MSP
// (switch DVR-REC, canal VTx, réponse de version) part sur les deux liens.
static void sendMspToHosts(mspPacket_t *packet) {
  msp.sendPacket(packet, &Serial);
  msp.sendPacket(packet, &IO); // IO = Bluetooth seul (voir setBleOnly dans backpack_c3.ino)
}

static void SendVersionResponse() {
  // The real ExpressLRS Backpack reports this as an ASCII version string
  // (confirmed by capturing a genuine backpack's reply: "1.5.5", not 3
  // binary bytes) — RotorHazard's VRxC_ELRS plugin parses it as text to
  // check against its minimum supported version, so sending raw bytes here
  // silently fails that check (no crash, just never shows a version).
  static const char version[] = "1.5.5";
  mspPacket_t out;
  out.reset();
  out.makeResponse();
  out.function = MSP_ELRS_GET_BACKPACK_VERSION;
  for (size_t i = 0; i < sizeof(version) - 1; i++) out.addByte((uint8_t)version[i]);
  sendMspToHosts(&out);
}

// Vers l'adresse actuellement visée par sendAddress — c'est RotorHazard/sa
// propre MSP_ELRS_SET_SEND_UID qui la fait pointer vers tel ou tel pilote
// normalement ; backpackSendOSD() fait pareil, temporairement, plus bas.
static int sendMSPViaEspnow(mspPacket_t *packet) {
  uint8_t packetSize = msp.getTotalPacketSize(packet);
  uint8_t nowDataOutput[packetSize];
  if (!msp.convertToByteArray(packet, nowDataOutput)) return -1;
  int err = esp_now_send(sendAddress, nowDataOutput, packetSize);
  ledBlip();
  return err;
}

// ESP-NOW n'accepte qu'un paquet non acquitté à la fois en vol — le reste
// du code (txqueue) respecte déjà ça via espnowCTS/sendLock, mais
// backpackSendOSD() envoie plusieurs paquets d'affilée en dehors de cette
// file, donc il doit attendre lui-même entre chaque. Bloquant (jusqu'à
// ~200ms), acceptable vu la fréquence (par tour, pas par tick).
static bool sendMSPViaEspnowWait(mspPacket_t *packet) {
  uint32_t t0 = millis();
  while (!espnowCTS && (millis() - t0) < 200) delay(1);
  espnowCTS = false;
  sendSuccess = false;
  if (sendMSPViaEspnow(packet) != 0) { espnowCTS = true; return false; }
  t0 = millis();
  while (!espnowCTS && (millis() - t0) < 200) delay(1);
  return sendSuccess;
}

// A packet arrived from RotorHazard over Serial (USB), headed for the peer.
static void ProcessMSPPacketFromTimer(mspPacket_t *packet) {
  if (connectionState == bp_binding) {
    if (packet->function == MSP_ELRS_BIND) adoptGroupAddress(packet->payload);
    return;
  }

  switch (packet->function) {
    case MSP_ELRS_BACKPACK_SET_MODE:
      if (packet->payloadSize == 1 && packet->payload[0] == 'B') {
        backpackEnterBinding();
      }
      break;
    case MSP_ELRS_GET_BACKPACK_VERSION:
      SendVersionResponse();
      break;
    case MSP_ELRS_SET_SEND_UID: {
      uint8_t useCustom = packet->readByte();
      if (useCustom == 0x01 && !packet->readError) {
        for (uint8_t i = 0; i < 6; i++) sendAddress[i] = packet->readByte();
      } else {
        memcpy(sendAddress, groupAddress, 6);
      }
      registerPeer(sendAddress);
      break;
    }
    default:
      // transparently forward anything else (OSD text, race status, ...)
      sendMSPViaEspnow(packet);
      break;
  }
}

// A packet arrived from a peer over ESP-NOW, headed for RotorHazard.
static void ProcessMSPPacketFromPeer(mspPacket_t *packet) {
  if (connectionState == bp_binding) {
    if (packet->function == MSP_ELRS_BIND) adoptGroupAddress(packet->payload);
    return;
  }

  // Only these two, relayed straight to the host (RotorHazard over USB, the
  // Laptimer web app over Bluetooth — both as MSP); everything else from
  // peers is ignored.
  switch (packet->function) {
    case MSP_ELRS_BACKPACK_SET_RECORDING_STATE: // DVR-REC switch: race start/stop
    case MSP_SET_VTX_CONFIG:                    // VTx channel change from the radio
      sendMspToHosts(packet);
      break;
  }
}

// esp_now's callback signatures changed with arduino-esp32 core v3 (IDF 5):
// the mac address moved inside an info/tx_info struct instead of being
// passed directly. Support both so this builds on core v2 and v3.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  (void)info;
  espnowCTS = true;
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  if (sendLock) xSemaphoreGive(sendLock);
}

static void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  const uint8_t *mac_addr = info->src_addr;
#else
static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;
  espnowCTS = true;
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  if (sendLock) xSemaphoreGive(sendLock);
}

static void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
#endif
  static MSP recv_msp;
  bool fromKnownPeer = (connectionState == bp_binding) || (memcmp(mac_addr, groupAddress, 6) == 0);
  for (int i = 0; i < data_len; i++) {
    if (recv_msp.processReceivedByte(data[i])) {
      if (fromKnownPeer && rxqueue) {
        xQueueSend(rxqueue, recv_msp.getReceivedPacket(), (TickType_t)1024);
      }
      recv_msp.markPacketReceived();
    }
  }
  ledBlip();
}

// ═══════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════

void backpackEnterBinding() {
  connectionState = bp_binding;
  bindingStart = millis();
  isBinding = true;
}

// Calqué exactement sur le vrai plugin RotorHazard (VRxC_ELRS, elrs_backpack.py) :
// SET_SEND_UID(pilote) → SET_OSD clear → SET_OSD write (centré sur 50
// colonnes, comme center_osd() côté plugin) → SET_OSD display →
// SET_SEND_UID(reset). Pas de heartbeat — ELRS_SET_OSD n'en demande pas,
// contrairement au MSP_DISPLAYPORT "canvas mode" de Betaflight avec lequel
// il partage son numéro de fonction (182) mais pas la sémantique.
bool backpackSendOSD(const char *phrase, const char *text) {
  if (connectionState != bp_running) return false; // pas de peer valide pendant le binding
  if (!phrase || !phrase[0]) return false;         // pas de bind phrase pilote connue : rien à faire

  uint8_t pilotAddr[6];
  phraseToAddress(phrase, pilotAddr);
  registerPeer(pilotAddr); // esp_now_add_peer — exigé avant un send vers cette adresse

  uint8_t savedSendAddress[6];
  memcpy(savedSendAddress, sendAddress, 6);
  memcpy(sendAddress, pilotAddr, 6); // = SET_SEND_UID(pilote) le temps de cette rafale

  // Le texte peut contenir plusieurs lignes séparées par '~' (de haut en
  // bas) : la dernière reste en ligne 14, les précédentes sont 4 lignes plus
  // haut chacune — pour afficher p.ex. "GO" au-dessus d'une annonce.
  const int MAX_LINES = 4;
  const char *lineStart[MAX_LINES];
  size_t      lineLen[MAX_LINES];
  int nLines = 0;
  for (const char *c = text; nLines < MAX_LINES; ) {
    const char *sep = strchr(c, '~');
    lineStart[nLines] = c;
    lineLen[nLines] = sep ? (size_t)(sep - c) : strlen(c);
    nLines++;
    if (!sep) break;
    c = sep + 1;
  }

  mspPacket_t pkt;
  bool ok = true;

  // reset() met type=MSP_PACKET_UNKNOWN — sans makeCommand(), convertToByteArray()
  // refuse le paquet (retourne 0) et sendMSPViaEspnow() échoue avant même
  // d'émettre quoi que ce soit sur la radio. C'est ça, et seulement ça, qui
  // produisait ESPNOW_FAIL depuis le début (vérifié avec un sketch de test
  // isolé) — pas un problème de canal, de MAC spoofée ou de LR mode.
  pkt.reset(); pkt.makeCommand(); pkt.function = MSP_ELRS_SET_OSD; pkt.addByte(MSP_DP_CLEAR_SCREEN);
  ok &= sendMSPViaEspnowWait(&pkt);

  for (int li = 0; li < nLines; li++) {
    size_t len = lineLen[li];
    if (len > 50) len = 50; // canevas OSD ELRS : 50 colonnes (vu dans center_osd() du plugin)
    if (len == 0) continue;
    int row = 14 - 4 * (nLines - 1 - li);
    if (row < 0) row = 0;
    uint8_t col = (50 - len) / 2; // centré, comme le fait le plugin
    pkt.reset(); pkt.makeCommand(); pkt.function = MSP_ELRS_SET_OSD; pkt.addByte(MSP_DP_WRITE_STRING);
    pkt.addByte((uint8_t)row); // ligne (sur 18)
    pkt.addByte(col);
    pkt.addByte(0);   // attribut (texte normal)
    for (size_t i = 0; i < len; i++) pkt.addByte((uint8_t)lineStart[li][i]);
    ok &= sendMSPViaEspnowWait(&pkt);
  }

  pkt.reset(); pkt.makeCommand(); pkt.function = MSP_ELRS_SET_OSD; pkt.addByte(MSP_DP_DRAW_SCREEN);
  ok &= sendMSPViaEspnowWait(&pkt);

  memcpy(sendAddress, savedSendAddress, 6); // = SET_SEND_UID(reset), comme le plugin
  return ok;
}

// Octet reçu par Bluetooth : le Bluetooth est un second lien MSP comme l'USB.
// Retourne true s'il fait partie d'une trame MSP (consommé ici : elle rejoint
// la file d'envoi comme celles venant de l'USB), false si c'est du texte
// (commande "BACKPACK:…", "LAPTIMER:OSD:…", etc.) à traiter par l'appelant.
bool backpackBleFeed(uint8_t c) {
  static MSP bleMsp;
  if (bleMsp.idle() && c != '$') return false;
  if (bleMsp.processReceivedByte(c)) {
    if (txqueue) xQueueSend(txqueue, bleMsp.getReceivedPacket(), (TickType_t)0);
    bleMsp.markPacketReceived();
  }
  return true;
}

// Traite la partie après "LAPTIMER:OSD:" d'une commande texte reçue (en BLE) :
// "<bind phrase du pilote>|<texte>" — partagé entre le firmware scan+backpack
// (drone_scan_tool.ino) et la variante "backpack seul" (backpack_c3.ino).
// Répond "ACK:OSD=SENT|ESPNOW_FAIL|ERR_NO_PHRASE,UID=…" via IO.
void backpackHandleOsdCommand(const char *payload) {
  const char *sep = strchr(payload, '|');
  if (!sep) { IO.println("ACK:OSD=ERR_NO_PHRASE"); return; }
  char phraseBuf[65]; // payload (const) ne peut être modifié : on copie la phrase à part
  size_t plen = sep - payload;
  if (plen > 64) plen = 64;
  memcpy(phraseBuf, payload, plen);
  phraseBuf[plen] = '\0';
  bool ok = backpackSendOSD(phraseBuf, sep + 1);
  uint8_t uid[6];
  phraseToAddress(phraseBuf, uid);
  IO.printf("ACK:OSD=%s,UID=%02X%02X%02X%02X%02X%02X\n",
            ok ? "SENT" : "ESPNOW_FAIL",
            uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);
}

void backpackSetup() {
  Serial.begin(BACKPACK_BAUD_RATE);
  delay(300);

  ensureStorage();

  pinMode(PIN_BACKPACK_LED, OUTPUT);
  pinMode(PIN_BACKPACK_BUTTON, INPUT_PULLUP);

  rxqueue  = xQueueCreate(10, sizeof(mspPacket_t));
  txqueue  = xQueueCreate(64, sizeof(mspPacket_t));
  sendLock = xSemaphoreCreateBinary();
  xSemaphoreGive(sendLock);

  SetSoftMACAddress();

  if (esp_now_init() != ESP_OK) {
    Serial.println("BOOT:MODE=BACKPACK,ESPNOW_INIT_FAILED");
  }
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);
  registerPeer(groupAddress);

  connectionState = bp_running;

  Serial.printf("BOOT:MODE=BACKPACK,UID=%02X%02X%02X%02X%02X%02X\n",
                groupAddress[0], groupAddress[1], groupAddress[2],
                groupAddress[3], groupAddress[4], groupAddress[5]);
}

void backpackLoop() {
  uint32_t now = millis();

  ledUpdate(now);
  buttonUpdate(now);


  if (connectionState == bp_binding && isBinding &&
      (now - bindingStart) > BINDING_NO_PEER_TIMEOUT) {
    connectionState = bp_running;
    isBinding = false;
    IO.println("ACK:BIND=TIMEOUT");
  }

  // Read Serial: feed the MSP parser, and in parallel watch for plain text
  // command lines (a human/our web app typing them, as opposed to the
  // binary MSP frames RotorHazard sends). The two never collide: MSP frames
  // always start with '$','X', which these lines never contain. This lets
  // you drive the mode entirely from serial, with no physical button needed.
  static char lineBuf[20];
  static uint8_t lineLen = 0;

  while (Serial.available()) {
    uint8_t c = Serial.read();

    if (msp.processReceivedByte(c)) {
      if (txqueue) xQueueSend(txqueue, msp.getReceivedPacket(), (TickType_t)1024);
      msp.markPacketReceived();
    }

    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        if (strcmp(lineBuf, "BACKPACK:BIND") == 0) {
          backpackEnterBinding();
          Serial.println("ACK:BIND=WAITING");
        }
        lineLen = 0;
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = (char)c;
    } else {
      lineLen = 0; // not a command we recognise, discard
    }
  }

  // Drain txqueue (Serial -> ESP-NOW), respecting ESP-NOW send back-pressure
  if (sendLock) xSemaphoreTake(sendLock, (TickType_t)512);
  if (txqueue && uxQueueMessagesWaiting(txqueue) > 0 && espnowCTS) {
    mspPacket_t packet;
    xQueuePeek(txqueue, &packet, (TickType_t)512);
    uint16_t function = packet.function;

    if (function == MSP_ELRS_GET_BACKPACK_VERSION ||
        function == MSP_ELRS_BACKPACK_SET_MODE ||
        function == MSP_ELRS_SET_SEND_UID) {
      if (sendLock) xSemaphoreGive(sendLock);
      ProcessMSPPacketFromTimer(&packet);
      xQueueReceive(txqueue, &packet, (TickType_t)512);
    } else {
      espnowCTS = false;
      if (sendLock) xSemaphoreGive(sendLock);
      if (++sendAttempt >= maxAttempt || sendSuccess) {
        sendAttempt = 0;
        ProcessMSPPacketFromTimer(&packet);
        xQueueReceive(txqueue, &packet, (TickType_t)512);
      } else {
        ProcessMSPPacketFromTimer(&packet);
      }
    }
  } else if (sendLock) {
    xSemaphoreGive(sendLock);
  }

  // Drain rxqueue (ESP-NOW -> Serial)
  if (rxqueue && uxQueueMessagesWaiting(rxqueue) > 0) {
    mspPacket_t rxPacket;
    if (xQueueReceive(rxqueue, &rxPacket, (TickType_t)512) == pdTRUE) {
      ProcessMSPPacketFromPeer(&rxPacket);
    }
  }
}
