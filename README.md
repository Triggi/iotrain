# iotrain
Cheap christmas train turned IOT
## Parts:
- Cheap battery-operated train
- Arduino Nano
- ES2688 WiFi module
- Motor driver
- Some resistors

## Getting started
Set your WiFi SSID and password in the AP_NAME and AP_PWD defines. Note: My ES8266 was set to a baudrate of 115200 by default, which is too fast for a Soft-uart in Arduino. I had to change it to 9600 first.

## Usages
It will log-in to the specified WiFi network and run a webserver. You can find out the assigned IP by inspecting the console output or by checking your router. You can set the motor speed by calling \<ip\>/speed?\<value-0-100\> or set the light intensity by calling calling \<ip\>/light?\<value-0-100\> 
