#include <Arduino.h>

// ######### BT

// resultant code from this module is huge; consider using https://github.com/h2zero/NimBLE-Arduino
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ######### WIFI

#include <WiFi.h>
#include <Preferences.h>

String ssids_array[50];
String network_string;
String connected_string;

const char* pref_ssid = "";
const char* pref_pass = "";

String client_wifi_ssid;
String client_wifi_password;

String bt_input;

const char* bluetooth_name = "ESP32-CONFIG";

long start_wifi_millis;
long wifi_timeout = 15000;
bool bluetooth_disconnect = false;

enum wifi_setup_stages { NONE, SCAN_START, SCAN_COMPLETE, SSID_ENTERED, WAIT_PASS, PASS_ENTERED, WAIT_CONNECT, LOGIN_FAILED };
enum wifi_setup_stages wifi_stage = NONE;

Preferences preferences;

bool init_wifi();
void scan_wifi_networks();

// #################################################################
// ######### BT

#define BAUDRATE 115200

#define SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;

bool deviceConnected = false;
std::string rxValue = "";

void ble_send_chunk(std::string msg){
  pTxCharacteristic->setValue((byte*)msg.c_str(), msg.size());
  pTxCharacteristic->notify();
}

void ble_send(std::string str)
{
  const size_t linelength = 20;

  size_t count = str.length() / linelength;
  auto start = str.begin();
  std::string sub;

  for (size_t i = 0; i < count; i++)
  {
      size_t startoffset = i * linelength;
      size_t endoffset = startoffset + linelength;
      sub = std::string(start + startoffset, start + endoffset);
      ble_send_chunk(sub);
  }
  if (str.length() % linelength)
  {
      sub = std::string(start + count * linelength, str.end());
      ble_send_chunk(sub);
  }

  String msg = String(str.c_str());
  msg.trim();
  Serial.println("BT TX: " + String(str.c_str()));
}

void on_ble_receive(std::string msg) {

  if (wifi_stage == SCAN_COMPLETE) { // data from phone is SSID
    bt_input = bt_input + String(msg.c_str());
    if(bt_input.indexOf("\r") > -1){
      int client_wifi_ssid_id = bt_input.toInt(); //String(msg.c_str()).toInt();
      client_wifi_ssid = ssids_array[client_wifi_ssid_id];
      ble_send("\n");
      wifi_stage = SSID_ENTERED;
    }
  }

  if (wifi_stage == WAIT_PASS) { // data from phone is PASS
    bt_input = bt_input + String(msg.c_str());
    if(bt_input.indexOf("\r") > -1){
      bt_input.trim();
      client_wifi_password = bt_input;
      ble_send("\n");
      wifi_stage = PASS_ENTERED;
    }
  }

}

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    Serial.println("BT: connected");
    delay(500);
    ble_send("\n\nBluetooth connected\n");
    deviceConnected = true;
    wifi_stage = SCAN_START;
  }
  void onDisconnect(BLEServer *pServer)
  {
      Serial.println("BT: disconnected");
      deviceConnected = false;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        rxValue = pCharacteristic->getValue();
        Serial.print("BT RX: ");
        Serial.println(rxValue.c_str());
        on_ble_receive(rxValue);
    }
};

void init_bt()
{

  BLEDevice::init(bluetooth_name);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  // TX
  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX
  pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();

  Serial.println("BT: started");

}

void disconnect_bluetooth()
{
  delay(1000);
  ble_send("Bluetooth disconnecting...\n");
  delay(1000);
  
  pServer->getAdvertising()->stop();  
  pService->stop();

  Serial.println("BT: stopped");
  delay(1000);
  bluetooth_disconnect = false;
}

bool loop_bt()
{
  if (bluetooth_disconnect)
  {
    disconnect_bluetooth();
  }

  switch (wifi_stage)
  {
    case SCAN_START:
      scan_wifi_networks();
      ble_send("Select WiFi:\r\n");
      bt_input = "";
      wifi_stage = SCAN_COMPLETE;
      break;

    case SSID_ENTERED:
      ble_send("Enter Password:\r\n");
      bt_input = "";
      wifi_stage = WAIT_PASS;
      break;

    case PASS_ENTERED:
      ble_send("Connecting...");
      wifi_stage = WAIT_CONNECT;
      preferences.putString("pref_ssid", client_wifi_ssid);
      preferences.putString("pref_pass", client_wifi_password);
      if (init_wifi()) { // Connected to WiFi
        ble_send("OK\r\n");
        connected_string = "WiFi: IP = " + WiFi.localIP().toString() + "\r\n";
        ble_send(connected_string.c_str());
        bluetooth_disconnect = true;
      } else { // try again
        ble_send("FAILED\r\n");
        wifi_stage = LOGIN_FAILED;
      }
      break;

    case LOGIN_FAILED:
      delay(2000);
      wifi_stage = SCAN_START;
      break;
  }

}

// #################################################################
// ######### WIFI

bool init_wifi() {
  String temp_pref_ssid = preferences.getString("pref_ssid");
  String temp_pref_pass = preferences.getString("pref_pass");
  pref_ssid = temp_pref_ssid.c_str();
  pref_pass = temp_pref_pass.c_str();

  Serial.println("WIFI: connecting to '" + String(pref_ssid) + "' / '" + String(pref_pass) + "'");

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

  start_wifi_millis = millis();
  WiFi.begin(pref_ssid, pref_pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start_wifi_millis > wifi_timeout){
      Serial.println("");
      Serial.println("WIFI: connection timeout");
      WiFi.disconnect(true, true);
      return false;
    }
  }

  Serial.println("");
  Serial.println("WiFi: IP = " + WiFi.localIP().toString());

  return true;
}

void scan_wifi_networks() {
  WiFi.mode(WIFI_STA);
  // WiFi.scanNetworks will return the number of networks found
  ble_send("Scanning WiFi... ");
  int n =  WiFi.scanNetworks();
  if (n == 0){
    ble_send("no networks found\r\n");
  } else {
    network_string = String(n) + " networks found\r\n\r\n";
    ble_send(network_string.c_str());
    for (int i = 0; i < n; ++i) {
      ssids_array[i + 1] = WiFi.SSID(i);
      network_string = "[" + String(i+1) + "]  " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + "dB)\r\n";
      ble_send(network_string.c_str());      
    }
    wifi_stage = SCAN_COMPLETE;
  }
}

// #################################################################
// ######### SETUP

void setup()
{
  Serial.begin(BAUDRATE);
  Serial.println("Booting...");

  preferences.begin("wifi_access", false);

  if (!init_wifi()) { // try to connect to wifi...
    init_bt();
  }

}

// #################################################################
// ######### LOOP

void loop()
{
    loop_bt();
}