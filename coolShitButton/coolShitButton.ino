#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

const int PIN_R = 25, PIN_G = 32, PIN_B = 33;
const int PIN_BTN = 27;
const bool COMMON_ANODE = true;
const unsigned long LONG_PRESS_MS = 2000;

enum State { ST_WAITING, ST_READY, ST_BUSY, ST_SUCCESS, ST_ERR_LINK, ST_ERR_AI };
State state = ST_WAITING;
unsigned long stateUntil = 0;

static const uint8_t HID_REPORT_MAP[] = {
  0x05, 0x01,
  0x09, 0x06,
  0xA1, 0x01,
  0x85, 0x01,
  0x05, 0x07,
  0x19, 0xE0,
  0x29, 0xE7,
  0x15, 0x00,
  0x25, 0x01,
  0x75, 0x01,
  0x95, 0x08,
  0x81, 0x02,
  0x95, 0x01,
  0x75, 0x08,
  0x81, 0x03,
  0x95, 0x06,
  0x75, 0x08,
  0x15, 0x00,
  0x25, 0x65,
  0x05, 0x07,
  0x19, 0x00,
  0x29, 0x65,
  0x81, 0x00,
  0xC0
};

NimBLEHIDDevice* hid;
NimBLECharacteristic* input;
NimBLECharacteristic* nusTx;

bool connected = false;
bool armed = true;
bool lastBtn = HIGH;
unsigned long lastDebounce = 0;
unsigned long pressStart = 0;
bool longFired = false;
unsigned long confirmUntil = 0;
int cR = 0, cG = 0, cB = 0;

void writeRGB(int r, int g, int b) {
  if (COMMON_ANODE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(PIN_R, r);
  analogWrite(PIN_G, g);
  analogWrite(PIN_B, b);
}

void setState(State s, unsigned long durationMs) {
  state = s;
  stateUntil = durationMs ? millis() + durationMs : 0;
}

void sendKey(uint8_t modifiers, uint8_t keycode) {
  uint8_t report[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0};
  input->setValue(report, 8);
  input->notify();
  delay(20);

  uint8_t release[8] = {0};
  input->setValue(release, 8);
  input->notify();
  delay(20);
}

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    connected = true;
    setState(ST_READY, 0);
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    connected = false;
    setState(ST_WAITING, 0);
    NimBLEDevice::startAdvertising();
  }
};

class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    String v = String(c->getValue().c_str());
    v.trim();
    Serial.println("PC -> " + v);
    if (v == "BUSY")         setState(ST_BUSY, 0);
    else if (v == "OK")      setState(ST_SUCCESS, 10000);
    else if (v == "ERRLINK") setState(ST_ERR_LINK, 5000);
    else if (v == "ERRAI")   setState(ST_ERR_AI, 5000);
  }
};

void fire() {
  sendKey(0x08, 0x06);
  delay(250);
  sendKey(0x01 | 0x04 | 0x08, 0x0E);
  Serial.println("Cmd+C puis raccourci envoyes");
  
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("--- Demarrage ---");
  pinMode(PIN_BTN, INPUT_PULLUP);
  writeRGB(0, 0, 0);

  NimBLEDevice::init("CoolButton");
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Hugo");
  hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
  input = hid->getInputReport(1);
  hid->setBatteryLevel(100);
  hid->startServices();

  NimBLEService* nus = server->createService(NUS_SERVICE);

  nusTx = nus->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* nusRx = nus->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  nusRx->setCallbacks(new RxCB());

  nus->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->addServiceUUID(NUS_SERVICE);
  adv->enableScanResponse(true);
  adv->setMinInterval(32);
  adv->setMaxInterval(64);
  adv->start();

  Serial.println("CoolButton pret (HID + NUS)");
  Serial.print("Bonds enregistres: ");
  Serial.println(NimBLEDevice::getNumBonds());
}

void loop() {
  bool btn = digitalRead(PIN_BTN);
  unsigned long t = millis();

  if (btn != lastBtn && t - lastDebounce > 50) {
    lastDebounce = t;
    if (btn == LOW) {
      pressStart = t;
      longFired = false;
    } else if (!longFired) {
      if (armed && connected) {
        fire();
      } else {
        Serial.println("Ignore (desarme ou non connecte)");
      }
    }
    lastBtn = btn;
  }

  if (btn == LOW && !longFired && t - pressStart > LONG_PRESS_MS) {
    longFired = true;
    armed = !armed;
    cR = 255;
    cG = armed ? 255 : 0;
    cB = armed ? 255 : 0;
    confirmUntil = t + 400;
    Serial.println(armed ? "ARME" : "DESARME");
  }

  if (t < confirmUntil) {
    writeRGB(cR, cG, cB);
    delay(10);
    return;
  }

  if (!armed) {
    writeRGB(0, 0, 0);
    delay(10);
    return;
  }

  if (stateUntil && t > stateUntil) {
    setState(connected ? ST_READY : ST_WAITING, 0);
  }

  if (state == ST_WAITING)        writeRGB(0, 0, (t % 1000) < 500 ? 255 : 0);
  else if (state == ST_READY)     writeRGB(0, 0, 255);
  else if (state == ST_BUSY) {
    unsigned long p = t % 1800;
    float tri = (p < 900) ? p / 900.0 : (1800 - p) / 900.0;
    writeRGB(0, (int)(pow(tri, 2.2) * 255), 0);
  }
  else if (state == ST_SUCCESS)   writeRGB(0, 255, 0);
  else if (state == ST_ERR_LINK)  writeRGB(255, 0, 0);
  else if (state == ST_ERR_AI)    writeRGB(160, 0, 255);

  delay(10);
}