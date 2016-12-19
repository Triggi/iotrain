#include <SoftwareSerial.h>

#define DEBUG true
#define MOTOR_L 5
#define MOTOR_H 6
#define HEADLIGHT 9

#define AP_NAME "dreusnet"
#define AP_PWD "#@bith@#"

String indexPage = "";//<html><head> <meta charset='utf-8'> <meta name='viewport' content='width=device-width, minimum-scale=1, initial-scale=1, user-scalable=yes'> <title>IoTrain</title> <link rel='icon' href='/images/favicon.ico'> <link rel='import' href='http://trainparts.mythingy.net/src/train-app.html'> <style>body {margin: 0;font-family: 'Roboto', 'Noto', sans-serif;line-height: 1.5;min-height: 100vh;background-color: #eeeeee;} </style></head><body> <my-app></my-app></body></html>";

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
  sendCommandEx("AT+CWJAP=\"" AP_NAME "\",\"" AP_PWD "\"\r\n", 4000, "\r\nOK\r\n"); // configure as access point
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
  int remaining = length;
  int sent = 0;
  while (remaining > 64) {
    Serial.println("loop");
    cipSend(connectionId, data + sent, 64);
    remaining -= 64;
    sent += 64;
  }
  String command = "AT+CIPSEND=";
  command += connectionId;
  command += ",";
  command += remaining;
  command += "\r\n";

  sendCommand(command, 1000);
  sendData(data + sent, remaining);
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

double getValue(String pathString) {
    int lastAssignIdx = pathString.lastIndexOf('=');
    if (lastAssignIdx == -1) {
      return -1;
    }
    String valueStr = pathString.substring(lastAssignIdx + 1);
    return valueStr.toFloat();  
}

double speedFactor = 2.55;
double lightFactor = 2.0;

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

  int sendStatus = 0;

  String pathString = path;
  if (pathString.startsWith("/app")) {
      return sendHttpResponse(connectionId, 301, "MOVED",  "Location: http://trainapp.mythingy.net/\r\n", "");        
  }
  else if (pathString.startsWith("/speed")) {
    double value = getValue(pathString);
    if (value < 0) {
      return sendHttpResponse(connectionId, 400, "Need value");
    }
    if (value > 100) value = 100;
    sendStatus = 1;
    target = (int)(value * speedFactor);
  } else if (pathString.startsWith("/light")) {
    double value = getValue(pathString);
    if (value < 0) {
      return sendHttpResponse(connectionId, 400, "Need value");
    }
    if (value > 100) value = 100;
    sendStatus = 1;
    lightTarget = (int)(value * lightFactor);
  } else {
    sendHttpResponse(connectionId, 404, "unknown");
  }
  if (sendStatus) {
    String status = "";
//    status += "{\"currentSpeed\": ";
//    status += ((double)current) / speedFactor;
//    status += ", \"currentLight\": ";
//    status += ((double)lightCurrent) / lightFactor;
//    status += ", \"targetSpeed\": ";
//    status += ((double)target) / speedFactor;
//    status += ", \"targetLight\": ";
//    status += ((double)lightTarget) / lightFactor;
//    status += "}";
    String headers = "";//Content-Type: application/json\r\nAccess-Control-Allow-Origin: http://trainapp.mythingy.net\r\n";        
    sendHttpResponse(connectionId, 200, "OK", headers, status);
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
      int byteCnt = esp8266.parseInt();
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

void sendData_old(char* data, int length) {
  esp8266.write(data, length);
  const int timeout = 50000;
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

void sendData(char* data, int length) {
  esp8266.write(data, length);
  String response = "";
  while (true) if (esp8266.available())
  {
    // The esp has data so display its output to the serial window
    char c = esp8266.read(); // read the next character.
    response += c;
    if (DEBUG) {
      Serial.write(c);
    }
    if (response.length() > strlen(sendDataTerm) && response.endsWith(sendDataTerm)) {
      Serial.write("Data done\r\n");
      return;
    }
  }
}

void slurpRemain() {
  Serial.println("slurpRemain");
  long int time = millis();

  while ( (time + 5000) > millis())
  {
    while (esp8266.available())
    {
      // The esp has data so display its output to the serial window
      char c = esp8266.read(); // read the next character.
      time = millis();
      if (DEBUG) {
        Serial.print(c);
      }
    }
  }
  Serial.println("/slurpRemain");
}
String sendCommandEx(String command, const int timeout, String terminator)
{
  Serial.print("sendCommand_term ");
  Serial.print(command);
  Serial.print(" ");
  Serial.print(terminator);
  Serial.print("\r\n");
  String response = "";

  esp8266.print(command); // send the read character to the esp8266

  long int time = millis();

  while ( (time + timeout) > millis())
  {
    while (esp8266.available())
    {
      Serial.print("byte ");

      // The esp has data so display its output to the serial window
      char c = esp8266.read(); // read the next character.
      response += c;
      time = millis();
      if (DEBUG) {
        Serial.println(c);
      }
      if (response.endsWith(terminator)) {
        Serial.println("done");
        slurpRemain();
        return response;
      }
    }
  }
  Serial.write("timeout in sendCommand!\r\n");
}

String sendCommand(String command, const int timeout)
{
  Serial.print("sendCommand_noterm ");
  sendCommandEx(command, timeout, "\r\nOK\r\n");
}

String sendCommand_nt(String command, const int timeout)
{
  String response = "";

  esp8266.print(command); // send the read character to the esp8266


  while (true) if (esp8266.available())
  {
    // The esp has data so display its output to the serial window
    char c = esp8266.read(); // read the next character.
    response += c;
    if (DEBUG) {
      Serial.write(c);
    }
    if (response.length() > 4 && response.endsWith("\r\nOK\r\n")) {
      return response;
    }
  }
  Serial.println("uhm?");
}
