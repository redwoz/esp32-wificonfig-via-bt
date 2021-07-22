# ESP32 / WiFi Client config via Bluetooth

##  Project Demos This:
* Hardware: ESP32
* Bluetooth library: https://github.com/nkolban/ESP32_BLE_Arduino
* Web Bluetooth API (via Chrome) terminal client: https://wiki.makerdiary.com/web-device-cli/
* Attemps to connect to WiFi
* If WiFi connection fails, creates BLE terminal server allowing for wireless configuration of WiFi SSID/password

##  Overcomes Challenges:
* Unsuccessful use of Web Bluetooth API (via Chrome) terminal client due to Bluetooth scan never showing device: https://loginov-rocks.github.io/Web-Bluetooth-Terminal/
* Unsuccessful use of standard Arduino BluetoothSerial module, as per https://github.com/loginov-rocks/Web-Bluetooth-Terminal/issues/20
