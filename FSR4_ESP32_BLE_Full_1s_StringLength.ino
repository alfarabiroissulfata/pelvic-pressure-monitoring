/*
  FSR4 Monitoring - ESP32-WROOM-32
  Komunikasi: Bluetooth Low Energy (BLE), bukan Firebase/Wi-Fi.

  Sensor FSR402:
    FSR1 -> GPIO34
    FSR2 -> GPIO35
    FSR3 -> GPIO32
    FSR4 -> GPIO33

  Tombol ke GND (INPUT_PULLUP):
    BASELINE -> GPIO25
    START    -> GPIO26
    STOP     -> GPIO27

  BLE:
    Device name       : FSR4-Monitor
    Service UUID      : 6e400001-b5a3-f393-e0a9-e50e24dcca9e
    Command (Write)   : 6e400002-b5a3-f393-e0a9-e50e24dcca9e
    Data (Notify)     : 6e400003-b5a3-f393-e0a9-e50e24dcca9e
    Status (Notify)   : 6e400004-b5a3-f393-e0a9-e50e24dcca9e

  Interval pengiriman: 1000 ms (1 detik)
  Format data: ADC1,ADC2,ADC3,ADC4
  Status     : IDLE / BASELINE / READY / RUN / STOP
  Command    : BASELINE atau B, START atau S, STOP atau X

  Catatan:
  - Firmware mengirim ADC mentah. Konversi gram/Newton dilakukan di dashboard
    menggunakan tabel kalibrasi masing-masing sensor.
  - Baseline hanya merekam nilai acuan; tidak mengurangi nilai ADC dan tidak
    menggantikan proses kalibrasi.
  - Pastikan rangkaian pembagi tegangan FSR sesuai dan tegangan pin ESP32
    tidak pernah melebihi 3.3 V.
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------- PIN ----------------
static const uint8_t FSR_PINS[4] = {34, 35, 32, 33};
static const uint8_t BTN_BASELINE = 25;
static const uint8_t BTN_START    = 26;
static const uint8_t BTN_STOP     = 27;

// ---------------- BLE UUID ----------------
#define SERVICE_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define COMMAND_CHAR_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define DATA_CHAR_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define STATUS_CHAR_UUID    "6e400004-b5a3-f393-e0a9-e50e24dcca9e"

// ---------------- PARAMETER ----------------
const uint32_t SAMPLE_INTERVAL_MS = 1000; // satu paket data setiap 1 detik
const uint8_t ADC_SAMPLES = 16;
const uint8_t BASELINE_COUNT = 8;
const uint32_t DEBOUNCE_MS = 220;

BLEServer *pServer = nullptr;
BLECharacteristic *pDataCharacteristic = nullptr;
BLECharacteristic *pStatusCharacteristic = nullptr;
BLECharacteristic *pCommandCharacteristic = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool baselineCaptured = false;
bool isRunning = false;

uint16_t latestADC[4] = {0, 0, 0, 0};
uint16_t baselineADC[4] = {0, 0, 0, 0};
uint32_t lastSampleMs = 0;

struct ButtonState {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  uint32_t lastChangeMs;
};

ButtonState buttonBaseline{BTN_BASELINE, HIGH, HIGH, 0};
ButtonState buttonStart{BTN_START, HIGH, HIGH, 0};
ButtonState buttonStop{BTN_STOP, HIGH, HIGH, 0};

// ---------------- PEMBACAAN SENSOR ----------------
uint16_t readAveragedADC(uint8_t pin) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    total += analogRead(pin);
    delay(2);
  }
  return (uint16_t)(total / ADC_SAMPLES);
}

void readAllSensors() {
  for (uint8_t i = 0; i < 4; i++) {
    latestADC[i] = readAveragedADC(FSR_PINS[i]);
  }
}

// ---------------- STATUS BLE ----------------
void sendStatus(const char *status) {
  if (pStatusCharacteristic == nullptr) return;
  pStatusCharacteristic->setValue(status);
  if (deviceConnected) pStatusCharacteristic->notify();
  Serial.print("STATUS: ");
  Serial.println(status);
}

void sendSensorData() {
  if (!deviceConnected || pDataCharacteristic == nullptr) return;

  char payload[48];
  snprintf(payload, sizeof(payload), "%u,%u,%u,%u",
           latestADC[0], latestADC[1], latestADC[2], latestADC[3]);
  pDataCharacteristic->setValue((uint8_t *)payload, strlen(payload));
  pDataCharacteristic->notify();

  Serial.println(payload);
}

// ---------------- KONTROL PENGUKURAN ----------------
void captureBaseline() {
  isRunning = false;
  sendStatus("BASELINE");

  uint32_t sums[4] = {0, 0, 0, 0};
  for (uint8_t n = 0; n < BASELINE_COUNT; n++) {
    readAllSensors();
    for (uint8_t i = 0; i < 4; i++) sums[i] += latestADC[i];
    delay(40);
  }

  for (uint8_t i = 0; i < 4; i++) {
    baselineADC[i] = (uint16_t)(sums[i] / BASELINE_COUNT);
  }

  baselineCaptured = true;
  Serial.print("Baseline ADC: ");
  Serial.print(baselineADC[0]); Serial.print(',');
  Serial.print(baselineADC[1]); Serial.print(',');
  Serial.print(baselineADC[2]); Serial.print(',');
  Serial.println(baselineADC[3]);

  sendStatus("READY");
}

void startMeasurement() {
  if (!baselineCaptured) {
    // Tetap izinkan pengujian dimulai, tetapi ingatkan melalui Serial.
    Serial.println("Peringatan: START sebelum BASELINE.");
  }
  isRunning = true;
  lastSampleMs = millis();
  sendStatus("RUN");
}

void stopMeasurement() {
  isRunning = false;
  sendStatus("STOP");
}

void processCommand(String command) {
  command.trim();
  command.toUpperCase();
  Serial.print("COMMAND: ");
  Serial.println(command);

  if (command == "BASELINE" || command == "B") {
    captureBaseline();
  } else if (command == "START" || command == "S") {
    startMeasurement();
  } else if (command == "STOP" || command == "X") {
    stopMeasurement();
  }
}

// ---------------- CALLBACK BLE ----------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
    Serial.println("BLE client connected");
    // Kirim status terkini setelah client terhubung.
    if (isRunning) sendStatus("RUN");
    else if (baselineCaptured) sendStatus("READY");
    else sendStatus("IDLE");
  }

  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    Serial.println("BLE client disconnected");
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    value.trim();
    if (value.length() > 0) {
      processCommand(value);
    }
  }
};

// ---------------- TOMBOL FISIK ----------------
bool buttonPressed(ButtonState &button) {
  bool reading = digitalRead(button.pin);
  uint32_t now = millis();

  if (reading != button.lastReading) {
    button.lastChangeMs = now;
    button.lastReading = reading;
  }

  if ((now - button.lastChangeMs) > DEBOUNCE_MS && reading != button.stableState) {
    button.stableState = reading;
    if (button.stableState == LOW) return true;
  }
  return false;
}

void setupBLE() {
  BLEDevice::init("FSR4-Monitor");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *service = pServer->createService(SERVICE_UUID);

  pDataCharacteristic = service->createCharacteristic(
    DATA_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pDataCharacteristic->addDescriptor(new BLE2902());

  pStatusCharacteristic = service->createCharacteristic(
    STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusCharacteristic->addDescriptor(new BLE2902());

  pCommandCharacteristic = service->createCharacteristic(
    COMMAND_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCommandCharacteristic->setCallbacks(new CommandCallbacks());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  Serial.println("BLE advertising started: FSR4-Monitor");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_BASELINE, INPUT_PULLUP);
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_STOP, INPUT_PULLUP);

  analogReadResolution(12);
  for (uint8_t i = 0; i < 4; i++) {
    analogSetPinAttenuation(FSR_PINS[i], ADC_11db);
  }

  readAllSensors();
  setupBLE();
  sendStatus("IDLE");

  Serial.println("FSR4 system ready.");
  Serial.println("Open dashboard, connect to FSR4-Monitor, then BASELINE -> START -> STOP.");
}

void loop() {
  if (buttonPressed(buttonBaseline)) captureBaseline();
  if (buttonPressed(buttonStart)) startMeasurement();
  if (buttonPressed(buttonStop)) stopMeasurement();

  uint32_t now = millis();
  if (isRunning && (now - lastSampleMs >= SAMPLE_INTERVAL_MS)) {
    lastSampleMs = now;
    readAllSensors();
    sendSensorData();
  }

  // Mulai advertising kembali setelah koneksi BLE terputus.
  if (!deviceConnected && oldDeviceConnected) {
    delay(300);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
    Serial.println("BLE advertising restarted");
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  delay(5);
}
