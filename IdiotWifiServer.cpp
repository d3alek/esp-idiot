#include "IdiotWifiServer.h"

IPAddress apIP(192, 168, 1, 1);
IPAddress netMsk(255, 255, 255, 0);

const char password[] = "very!private!";
static const char* updateIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

ESP8266WebServer server(80);


char _localPublishFile[20];        
IdiotLogger _logger;
char _uuid[15];

void handleRoot() {
  String content = "<html><body><form action='/wifi-credentials-entered' method='POST'>";
  content += "Wifi Access Point Name:<input type='text' name='wifiName' placeholder='wifi name'><br>";
  content += "Wifi Password:<input type='password' name='wifiPassword' placeholder='wifi password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form><br>";
  server.send(200, "text/html", content);
}

void serveLocalPublish() {
  File localPublishFile = SPIFFS.open(_localPublishFile, "r");
  int fileSize = localPublishFile.size();
  
  server.sendHeader("Content-Length", String(fileSize));
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "");
  WiFiClient client = server.client();

  int sentSize = client.write(localPublishFile, HTTP_DOWNLOAD_UNIT_SIZE);
  if (sentSize != fileSize) {
    _logger.println("Sent different data length than expected");
    _logger.print("Expected: "); _logger.println(fileSize);
    _logger.print("Actual: "); _logger.println(sentSize);
  }
}

void handleWifiCredentialsEntered() {
  if (server.hasArg("wifiName") && server.hasArg("wifiPassword")) {
    PersistentStore.putWifiName(server.arg("wifiName").c_str());
    PersistentStore.putWifiPassword(server.arg("wifiPassword").c_str());
    server.send(200, "text/plain", "Thank you, going to try to connect using the given credentials now.");
    EspControl.restart();
  }
  else {
    server.send(200, "text/plain", "No wifi credentials found in POST request.");
  }
}

void format() {
  SPIFFS.format();
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "OK");
}


void httpUpdateAnswer() {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
  EspControl.restart();
}

void httpUpdateDo() {
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    WiFiUDP::stopAll();
    
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if(!Update.begin(maxSketchSpace)){//start with max available size
      Update.printError(_logger);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
      Update.printError(_logger);
    }
  } else if(upload.status == UPLOAD_FILE_END){
    if(Update.end(true)){ //true to set the size to the current progress
      // update success
    } else {
      Update.printError(_logger);
    }
  }
  yield();
}

void httpUpdateGet() {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", updateIndex);
}

void serveLogs() {
  _logger.close();
  File logFile = SPIFFS.open(LOG_FILE, "r");
  int logFileSize = logFile.size();
  logFile.flush();
  logFile.seek(0, SeekSet);
  server.sendHeader("Content-Length", String(logFileSize));
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "");
  WiFiClient client = server.client();

  int sentSize = client.write(logFile, HTTP_DOWNLOAD_UNIT_SIZE);
  
  logFile.close();
  _logger.begin(115200);
  if (sentSize != logFileSize) {
    _logger.println("Sent different data length than expected");
    _logger.print("Expected: "); _logger.println(logFileSize);
    _logger.print("Actual: "); _logger.println(sentSize);
  }
}

void IdiotWifiServer::start(const char* uuid, const char* localPublishFile, IdiotLogger logger) {
  strcpy(_uuid, uuid);
  strcpy(_localPublishFile, localPublishFile);
  _logger = logger;

  _logger.println("Configuring access point...");
  delay(1000);
  pinMode(BUILTIN_LED, OUTPUT);
  EspControl.turnLedOn();
  
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(_uuid, password);

  IPAddress myIP = WiFi.softAPIP();
  _logger.println(myIP);

  server.on("/", handleRoot);
  server.on("/format", format);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, httpUpdateGet);
  server.on("/update", HTTP_POST, httpUpdateAnswer, httpUpdateDo);
  server.on("/logs", serveLogs);
  server.on("/local-publish", serveLocalPublish);
  server.begin();
}

void IdiotWifiServer::handleClient() {
  server.handleClient();
  EspControl.toggleLed();
}
