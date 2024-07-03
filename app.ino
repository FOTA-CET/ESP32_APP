#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

#include <mcp_can.h>
#include <SPI.h>

//Define host and wifi
const char* host = "esp32";
const char* ssid = "Xiaomi 13";
const char* password = "ahihihihi";

WebServer server(80);

//Can define
#define CAN0_INT 4
MCP_CAN CAN0(5);

enum class CONNECT_CMD {
  CONNECT = 65,
  DISCONNECT = 66
};

enum class STATUSCODE {
  ERROR_OK = 65,
  FAILED = 66
};

void receiveSignal(unsigned long& rxId, unsigned char& len, int& signal) {
  unsigned char rxBuf;
  CAN0.readMsgBuf(&rxId, &len, &rxBuf);
  signal = (int)rxBuf;
  Serial.println(rxId);
  Serial.println(signal);
}

bool sendSignal(unsigned long rxId, unsigned char len, int signal) {
  unsigned char data = (unsigned char)signal;
  while (CAN0.sendMsgBuf(rxId, 0, len, &data));
  return true;
}

bool configCan() {
  while (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
    Serial.println("Error Initializing MCP2515...");
  }

  CAN0.setMode(MCP_NORMAL);
  pinMode(CAN0_INT, INPUT);
  Serial.println("MCP2515 Initialized Successfully!");  
  return true;
}

bool connectWifi(IPAddress& ip) {
  WiFi.begin(ssid, password);
  Serial.printf("Connecting wifi");
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < 10000) {
    Serial.print(".");
  };

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi");
    return false;
  }

  Serial.println();
  ip = WiFi.localIP();
  return true;
}

bool configServer(uint32_t& fileSize) {
  /*Get size of file*/
  server.on("/fileSize", HTTP_POST, [&fileSize]() {
    if (server.hasArg("fileSize")) {
      fileSize = server.arg("fileSize").toInt();
      Serial.println(fileSize);
      server.send(200);
      return true;
    } else {
      server.send(400);
      return false;
    }
  });

  /*Handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {}, [&fileSize]() { 
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
        return false;
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      size_t writeSize = Update.write(upload.buf, upload.currentSize);
      if (writeSize == upload.currentSize) {
        int percent = (int)(((double)upload.totalSize / (double)fileSize) * 100);
        Serial.println("%");
        sendSignal(0x100, 1, (int)percent);
        Serial.printf("Writing: %.2f", percent);
        return false;
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        Serial.println("100%");
        sendSignal(0x100, 1, 100);
        server.send(200, "text/plain", "Update Success");
      } else {
        Update.printError(Serial);
        return false;
      }
    }
  });
  server.begin();
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(12, OUTPUT);
  configCan();
}

void blynkLed() {
  digitalWrite(12, HIGH);
  delay(200);
  digitalWrite(12, LOW);
  delay(200);
}

void loop() {
  static uint32_t fileSize;
  long unsigned int rxId;
  unsigned char len;
  int signal;

  flagReturn:
  if (!digitalRead(CAN0_INT)) {
    receiveSignal(rxId, len, signal);
    if (rxId == 0x123) {
      if (signal == (int)CONNECT_CMD::CONNECT) {
        IPAddress ip;
        if (connectWifi(ip)) {
          Serial.println(ip);
          sendSignal(0x100, 1, (int)STATUSCODE::ERROR_OK);
        } else {
          sendSignal(0x100, 1, (int)STATUSCODE::FAILED);
        }
        if(configServer(fileSize)) {
          Serial.println("Successfully to config server");
        } else {
          Serial.println("Failed to config server");
          goto flagReturn;
        }
      }
    }
  }
  server.handleClient();
  blynkLed();
}
