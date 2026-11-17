// This file intentionally does NOT `#define Serial IO` (unlike
// drone_scan_tool.ino) — `Serial` here is always the real hardware UART,
// so DualSerial has something concrete to mirror to.
#include "BleUart.h"
#include <NimBLEDevice.h>

// Nordic UART Service UUIDs — a de facto standard for "BLE as a serial
// port", recognised by generic BLE terminal apps too, not just our own web
// app. We only need one characteristic (NimBLEStreamServer handles both
// writes-from-client and notifies-to-client on it), unlike the usual
// separate RX/TX pair.
#define BLE_UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UART_CHAR_UUID    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

static NimBLEStreamServer bleStream;

// Set by setBleOnly(true) when entering backpack mode: the real Serial is
// then RotorHazard's MSP link and IO must neither read from it nor write to
// it, only to/from BLE.
static bool s_bleOnly = false;

DualSerial IO;

void DualSerial::begin(unsigned long baud) {
  Serial.begin(baud);
}

void DualSerial::setBleOnly(bool bleOnly) {
  s_bleOnly = bleOnly;
}

int DualSerial::available() {
  int n = s_bleOnly ? 0 : Serial.available();
  if (bleStream.ready()) n += bleStream.available();
  return n;
}

int DualSerial::read() {
  if (!s_bleOnly && Serial.available()) return Serial.read();
  if (bleStream.ready())  return bleStream.read();
  return -1;
}

int DualSerial::peek() {
  if (!s_bleOnly && Serial.available()) return Serial.peek();
  if (bleStream.ready())  return bleStream.peek();
  return -1;
}

void DualSerial::flush() {
  if (!s_bleOnly) Serial.flush();
}

size_t DualSerial::write(uint8_t c) {
  if (!s_bleOnly) Serial.write(c);
  if (bleStream.ready()) bleStream.write(c);
  return 1;
}

size_t DualSerial::write(const uint8_t *buffer, size_t size) {
  if (!s_bleOnly) Serial.write(buffer, size);
  if (bleStream.ready()) bleStream.write(buffer, size);
  return size;
}

// Keeps the connection alive/responsive and restarts advertising after a
// disconnect. BLE_HS_CONN_HANDLE_NONE-style bookkeeping is handled inside
// NimBLEStreamServer itself; this is just the server-level housekeeping the
// library expects an app to do.
static NimBLEServer *s_server = nullptr;
static uint8_t s_wantPhy = 1; // 1=1M, 2=2M, 3=Coded (valeurs HCI)
static volatile bool s_phyDirty = false;

// PHY_OPTIONS "S=8 préféré" (2) quand on veut du Coded : la portée maximale.
static bool applyPhy(uint16_t connHandle) {
  if (!s_server) return false;
  if (s_wantPhy == 3) return s_server->updatePhy(connHandle, BLE_GAP_LE_PHY_CODED_MASK, BLE_GAP_LE_PHY_CODED_MASK, 2);
  if (s_wantPhy == 2) return s_server->updatePhy(connHandle, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
  return s_server->updatePhy(connHandle, BLE_GAP_LE_PHY_1M_MASK, BLE_GAP_LE_PHY_1M_MASK, 0);
}

class BleUartServerCallbacks : public NimBLEServerCallbacks {
  void onPhyUpdate(NimBLEConnInfo &, uint8_t, uint8_t) override {
    s_phyDirty = true; // annoncé depuis loop() (bleUartPhyPoll), pas depuis la tâche BLE
  }
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    // Shorter connection interval than the default: lower latency for the
    // command/response protocol (ACK:/BOOT:/etc.) than the out-of-the-box
    // params, which favour power saving over responsiveness.
    server->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 200);
    if (s_wantPhy != 1) applyPhy(connInfo.getConnHandle());
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    NimBLEDevice::startAdvertising();
  }
};
static BleUartServerCallbacks bleServerCallbacks;

void bleUartBegin(const char *deviceName) {
  NimBLEDevice::init(deviceName);

  NimBLEServer *server = NimBLEDevice::createServer();
  s_server = server;
  server->setCallbacks(&bleServerCallbacks);

  // 1024 bytes each way is comfortably more than our longest line (a JSON
  // drone-beacon record); NimBLEStreamServer chunks to the negotiated MTU
  // internally, so lines longer than one BLE packet aren't our problem.
  bleStream.begin(NimBLEUUID(BLE_UART_SERVICE_UUID), NimBLEUUID(BLE_UART_CHAR_UUID), 1024, 1024, false);

  // Le nom dans le paquet d'annonce lui-même, l'UUID de service (128 bits =
  // 18 octets, trop gros pour tenir avec un nom) dans la réponse de scan.
  // Sinon NimBLE met le nom dans la réponse de scan, que Chrome sur Android
  // n'affiche pas : le sélecteur ne montrait que des adresses MAC.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setName(deviceName);
  NimBLEAdvertisementData scanData;
  scanData.addServiceUUID(NimBLEUUID(BLE_UART_SERVICE_UUID));
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->start();
}

bool bleUartSetPhy(uint8_t phy) {
  s_wantPhy = phy;
  if (!s_server) return false;
  bool ok = true;
  for (uint16_t h : s_server->getPeerDevices()) ok &= applyPhy(h);
  return ok;
}

bool bleUartGetPhy(uint8_t *tx, uint8_t *rx) {
  if (!s_server) return false;
  auto peers = s_server->getPeerDevices();
  if (peers.empty()) return false;
  return s_server->getPhy(peers[0], tx, rx);
}

void bleUartPhyPoll() {
  if (!s_phyDirty) return;
  s_phyDirty = false;
  uint8_t tx = 0, rx = 0;
  if (!bleUartGetPhy(&tx, &rx)) return;
  const char *name = (tx == 3 && rx == 3) ? "CODED" : (tx == 2 && rx == 2) ? "2M" : (tx == 1 && rx == 1) ? "1M" : "MIXED";
  IO.printf("ACK:BLEPHY=%s\n", name);
}

static const char *phyName(uint8_t tx, uint8_t rx) {
  return (tx == 3 && rx == 3) ? "CODED" : (tx == 2 && rx == 2) ? "2M" : (tx == 1 && rx == 1) ? "1M" : "MIXED";
}

bool bleUartHandlePhyCommand(const char *cmd) {
  if (strncmp(cmd, "BLE:PHY", 7) != 0) return false;
  bool reqOk = true;
  if (strcmp(cmd, "BLE:PHY:CODED") == 0)    reqOk = bleUartSetPhy(3);
  else if (strcmp(cmd, "BLE:PHY:2M") == 0)  reqOk = bleUartSetPhy(2);
  else if (strcmp(cmd, "BLE:PHY:1M") == 0)  reqOk = bleUartSetPhy(1);
  if (!reqOk) IO.println("ACK:BLEPHY=REQUEST_REFUSED");
  uint8_t tx = 0, rx = 0;
  if (bleUartGetPhy(&tx, &rx)) IO.printf("ACK:BLEPHY=%s\n", phyName(tx, rx));
  else IO.println("ACK:BLEPHY=NONE");
  return true;
}
