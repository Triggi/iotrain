#include <SoftwareSerial.h>

#define DEBUG true
#define MOTOR_L 5
#define MOTOR_H 6
#define HEADLIGHT 9

#define AP_NAME "<your-wifi-ssid>"
#define AP_PWD "<your-wifi-password>"

SoftwareSerial esp8266(3, 2);

long lastTime = 0;
int target = 0;
int current = 0;
int lightTarget = 0;
int lightCurrent = 0;

void setupWifi() {
  Serial.write("beginning setup\r\n");
  sendCommand("AT+RST\r\n", 2000); // reset module
  sendCommand("AT+CWMODE=1\r\n", 1000); // configure as access point
  sendCommand("AT+CWJAP=\"" AP_NAME "\",\"" AP_PWD "\"\r\n", 4000); // configure as access point
  sendCommand("AT+CIFSR\r\n", 1000); // get ip address
  sendCommand("AT+CIPMUX=1\r\n", 1000); // configure for multiple connections
  sendCommand("AT+CIPSERVER=1,80\r\n", 1000); // turn on server on port 80
  Serial.write("done setup\r\n");
  digitalWrite(LED_BUILTIN, HIGH);
}

void setupMotor() {
  pinMode(MOTOR_L, OUTPUT);
  pinMode(MOTOR_H, OUTPUT);
  pinMode(HEADLIGHT, OUTPUT);
  digitalWrite(MOTOR_L, LOW);   // turn the LED on (HIGH is the voltage level)
  analogWrite(MOTOR_H, 0);   // turn the LED on (HIGH is the voltage level)
  analogWrite(HEADLIGHT, 10);   // turn the LED on (HIGH is the voltage level)
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(115200);
  esp8266.begin(9600); // your esp's baud rate might be different
  setupWifi();
  setupMotor();
}

void cipSend(int connectionId, String data) {
  int length = data.length();
  char bytes[length];
  data.getBytes(bytes, length);
  cipSend(connectionId, bytes, length);
}

void cipSend(int connectionId, char* data, int length) {
  String command = "AT+CIPSEND=";
  command += connectionId;
  command += ",";
  command += length;
  command += "\r\n";

  sendCommand(command, 1000);
  sendData(data, length);
}

void cipClose(int connectionId) {
  String closeCommand = "AT+CIPCLOSE=";
  closeCommand += connectionId; // append connection id
  closeCommand += "\r\n";

  sendCommand(closeCommand, 3000);
}

void sendHttpResponse(int connectionId, int responseCode, String responseMsg, String headers, char* data, long dataLength) {
  String head = "HTTP/1.0 ";
  head += responseCode;
  head += " ";
  head += responseMsg;
  head += "\r\n";
  if (headers) {
    head += headers;
  }
  head += "\r\n";
  cipSend(connectionId, head);
  if (data && dataLength > 0) {
    cipSend(connectionId, data, dataLength);
  }
  cipClose(connectionId);
}

void sendHttpResponse(int connectionId, int responseCode, String responseMsg, String headers, String data) {
  int dataLength = data.length();
  char dataBytes[dataLength + 1];
  data.toCharArray(dataBytes, dataLength + 1);
  
  sendHttpResponse(connectionId, responseCode, responseMsg, headers, dataBytes, dataLength);
}

void sendHttpResponse(int connectionId, int responseCode, String responseMsg) {
  sendHttpResponse(connectionId, responseCode, responseMsg, "", NULL, 0);
}


void netRequest(int connectionId, Stream* stream) {
      char method[10];
      int count = stream->readBytesUntil(' ', method, sizeof(method) - 1);
      method[count] = 0;
      Serial.println("method: ");
      Serial.println(method);

      char path[100];
      count = stream->readBytesUntil(' ', path, sizeof(path) - 1);
      path[count] = 0;
      Serial.println("path: ");
      Serial.println(path);
      //Skipp till end of headers
      stream->find("\r\n\r\n");

      String pathString = path;
      int lastAssignIdx = pathString.lastIndexOf('=');
      String valueStr = pathString.substring(lastAssignIdx + 1);
      double value = valueStr.toFloat();
      if (value > 100) value = 100;
      Serial.print("Set to value: ");
      Serial.println(value);
      String status = "{\"currentSpeed\": ";
      status += current;
      status += ", \"currentLight\": ";
      status += lightCurrent;
      status += "}";
      String headers = "Content-Type: application/json\r\nAccess-Control-Allow-Origin: http://trainapp.mythingy.net\r\n";
      if (pathString.length() == 1 && pathString.equals("/")) {
          return sendHttpResponse(connectionId, 301, "MOVED",  "Location: http://trainapp.mythingy.net/\r\n", "");        
      }
      else if (pathString.startsWith("/speed")) {
        if (lastAssignIdx == -1) {
          Serial.println("missing value");
          return sendHttpResponse(connectionId, 400, "Need value");
        }
        sendHttpResponse(connectionId, 200, "OK", headers, status);
        target = (int)(value * 2.55);
      } else if (pathString.startsWith("/light")) {
        if (lastAssignIdx == -1) {
          Serial.println("missing value");
          return sendHttpResponse(connectionId, 400, "Need value");
        }
        sendHttpResponse(connectionId, 200, "OK", headers, status);
        lightTarget = (int)(value * 1.50);
      } else {
        sendHttpResponse(connectionId, 404, "unknown");
      }
}

void loop()
{
  //Incomming
  if (esp8266.available()) // check if the esp is sending a message
  {
    //Data packets look like '+IPD,<conId>,<size>:<data>
    if (esp8266.find("+IPD,"))
    {
      int count;
      int connectionId = esp8266.parseInt();
      Serial.println("connectionId: ");
      Serial.println(connectionId);
      int byteCnt = esp8266.parseInt();
      Serial.println("byteCnt: ");
      Serial.println(byteCnt);
      esp8266.find(":");
      netRequest(connectionId, &esp8266);
    }
  }

  //Adjust motor speed and headlight
  long time = millis();
  if (time - lastTime > 10) {
    lastTime = time;
    if (target > current) {
      ++current;
    } else if (target < current) {
      --current;
    }
    analogWrite(MOTOR_H, current);
    if (lightTarget > lightCurrent) {
      ++lightCurrent;
    } else if (lightTarget < lightCurrent) {
      --lightCurrent;
    }
    analogWrite(HEADLIGHT, lightCurrent);
  }
}

const char* sendDataTerm = "SEND OK\r\n";

void sendData(char* data, int length) {
  esp8266.write(data, length);
  const int timeout = length * 10;
  String response = "";
  long int time = millis();
  while ( (time + timeout) > millis())
  {
    while (esp8266.available())
    {
      // The esp has data so display its output to the serial window
      char c = esp8266.read(); // read the next character.
      response += c;
      time = millis();
      if (DEBUG) {
        Serial.write(c);
      }
      if (response.length() > strlen(sendDataTerm) && response.endsWith(sendDataTerm)) {
        Serial.write("Data done\r\n");
        return;
      }
    }
  }
  Serial.write("timeout in sendData!\r\n");
}

String sendCommand(String command, const int timeout)
{
  String response = "";

  esp8266.print(command); // send the read character to the esp8266

  long int time = millis();

  while ( (time + timeout) > millis())
  {
    while (esp8266.available())
    {

      // The esp has data so display its output to the serial window
      char c = esp8266.read(); // read the next character.
      response += c;
      time = millis();
      if (DEBUG) {
        Serial.write(c);
      }
      if (response.length() > 4 && response.endsWith("\r\nOK\r\n")) {
        return response;
      }
    }
  }
  Serial.write("timeout in sendCommand!\r\n");
}

