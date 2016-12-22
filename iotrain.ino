#include <SoftwareSerial.h>

#define DEBUG false
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

const char* OkRsp = "\r\nOK\r\n";
String command;

void setupWifi() {
  traceln("beginning setup");
  //  sendCommand("AT+RST\r\n", 2000); // Do not use - inpredictable output, will connect to WiFi if it was set-up before
  //  sendCommand("AT+ATE0\r\n", 2000);
  sendCommand("AT+CWMODE=1\r\n", 1000, OkRsp); // configure as client
  sendCommand("AT+CWJAP=\"" AP_NAME "\",\"" AP_PWD "\"\r\n", 4000, OkRsp); // configure as access point
  sendCommand("AT+CIFSR\r\n", 1000, OkRsp); // get ip address
  sendCommand("AT+CIPMUX=1\r\n", 1000, OkRsp); // configure for multiple connections
  sendCommand("AT+CIPSERVER=1,80\r\n", 1000, OkRsp); // turn on server on port 80
  traceln("done setup");
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
  command.reserve(20);
}

const char headTemplate[] = "HTTP/1.1 xxx\r\n";
void sendHttpResponse(int connectionId, const char* responseCode, const char* responseMsg, const char* headers, const char* data, long dataLength) {
  char head[strlen(headTemplate) + strlen(headers) + strlen("\r\n")];
  char* wrPtr = head;
  wrPtr = addToBuff(wrPtr, "HTTP/1.1 ");
  wrPtr = addToBuff(wrPtr, responseCode);
  wrPtr = addToBuff(wrPtr, "\r\n");
  wrPtr = addToBuff(wrPtr, headers);
  wrPtr = addToBuff(wrPtr, "\r\n");

  cipSend(connectionId, head, strlen(head));
  if (data && dataLength > 0) {
    cipSend(connectionId, data, dataLength);
  }
  cipClose(connectionId);
}

void sendHttpResponse(int connectionId, const char* responseCode, const char* responseMsg, const char* headers, String data) {
  int dataLength = data.length();
  char dataBytes[dataLength + 1];
  data.toCharArray(dataBytes, dataLength + 1);

  sendHttpResponse(connectionId, responseCode, responseMsg, headers, dataBytes, dataLength);
}

void sendHttpResponse(int connectionId, const char* responseCode, const char* responseMsg) {
  sendHttpResponse(connectionId, responseCode, responseMsg, "", NULL, 0);
}

const char defaultHeaders[] = "Content-Type: application/json\r\nAccess-Control-Allow-Origin: http://trainapp.mythingy.net\r\n";
const char appRedirHeader[] = "Location: http://trainapp.mythingy.net/\r\n";
//const char statusResponseTemplate[] PROGMEM = "{\"currentSpeed\": xxx, ", \"currentLight\": xxx }";

void handleHttpRequest(int connectionId, const char* path, Stream* stream) {
  if (startsWith(path, "/app")) {
    return sendHttpResponse(connectionId, "301", "MOVED",  appRedirHeader, "");
  } else {
    String status = "{\"currentSpeed\": ";
    status += (int) (0.5 + ((float)current) / 2.55);
    status += ", \"currentLight\": ";
    status += (int) (0.5 + ((float) lightCurrent) / 2.55);
    status += "}";
  
    float value = lastValueFromPath(path);
    if (value > 100) value = 100;

    traceKeyVal("Set to value: ", value);
    trace("Set to value: ");

    if (startsWith(path, "/speed")) {
      if (value < 0) {
        traceln("missing value");
        return sendHttpResponse(connectionId, "400", "Need value");
      }
      sendHttpResponse(connectionId, "200", "OK", defaultHeaders, status);
      target = (int)(value * 2.55);
    } else if (startsWith(path, "/light")) {
      if (value < 0) {
        traceln("missing value");
        return sendHttpResponse(connectionId, "400", "Need value");
      }
      sendHttpResponse(connectionId, "200", "OK", defaultHeaders, status);
      lightTarget = (int)(value * 2.55);
    } else {
      sendHttpResponse(connectionId, "404", "unknown");
    }
  }
}

void handleNetRequest(int connectionId, Stream* stream) {
  char method[10];
  int count = stream->readBytesUntil(' ', method, sizeof(method) - 1);
  method[count] = 0;
  traceKeyVal("method: ", method);

  char path[20];
  count = stream->readBytesUntil(' ', path, sizeof(path) - 1);
  path[count] = 0;
  traceKeyVal("path: ", path);

  stream->find("\r\n\r\n");

  handleHttpRequest(connectionId, path, stream);
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
      handleNetRequest(connectionId, &esp8266);
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

/**
   CIP commands
*/
const int maxSend = 64;
void cipSend(int connectionId, char* data, int length) {
  int remaining = length;
  int sent = 0;
  command = "AT+CIPSEND=";
  command += connectionId;
  command += ",64\r\n";

  while (remaining > maxSend) {
    sendCommand(command, 1000, "> ");
    sendData(data + sent, maxSend);

    remaining -= maxSend;
    sent += maxSend;
  }
  if (remaining > 0) {
    command = "AT+CIPSEND=";
    command += connectionId;
    command += ",";
    command += remaining;
    command += "\r\n";

    sendCommand(command, 1000, "> ");
    sendData(data + sent, remaining);
  }
}

void cipClose(int connectionId) {
  String closeCommand = "AT+CIPCLOSE=";
  closeCommand += connectionId; // append connection id
  closeCommand += "\r\n";

  sendCommand(closeCommand, 1000, OkRsp);
}


void sendData(char* data, int length) {
  esp8266.write(data, length);
  if (awaitResponse(4000, "SEND OK\r\n")) {
    traceln("data done");
  } else {
    traceln("data timeout");
  }
}

void sendCommand(String command, const int timeout, const char* term)
{
  esp8266.print(command); // send the read character to the esp8266

  if (awaitResponse(timeout, term)) {
    traceln("command done");
  } else {
    traceln("command timeout");
  }
}

/**
   Wait until either a terminator string is seen or longer than <timout> has passed after receiving the last byte.
*/
bool awaitResponse(const int timeout, const char* term) {
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
      if (response.endsWith(term)) {
        return true;
      }
    }
  }
  return false;
}

/**
   Utility functions
*/
bool startsWith(const char* buf, const char* with) {
  return strncmp(buf, with, strlen(with)) == 0;
}

int lastValueFromPath(const char* pathStr) {
  const char* eq = strrchr(pathStr, '=');
  if (!eq) {
    return -1;
  }
  return atoi(eq + 1);
}

char* addToBuff(const char* buff, const char* add) {
  int len = strlen(add);
  strcpy(buff, add);
  return buff + len;
}

void trace(const char* line) {
  if (DEBUG) {
    Serial.print(line);
  }
}

void traceln(const char* line) {
  if (DEBUG) {
    Serial.println(line);
  }
}

void traceKeyVal(const char* key, const char* value) {
  if (DEBUG) {
    Serial.print(key);
    Serial.println(value);
  }
}

void traceKeyVal(const char* key, int value) {
  if (DEBUG) {
    Serial.print(key);
    Serial.println(value);
  }
}

