#include "IdiotWifiServer.h"

IPAddress apIP(192, 168, 1, 1);
IPAddress netMsk(255, 255, 255, 0);

const char password[] = "very!private!";
static const char* updateIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

ESP8266WebServer server(80);

char _uuid[15];
bool started = false;

void handleRoot() {
  String content = "<html><body><form action='/wifi-credentials-entered' method='POST'>";
  content += "Wifi Access Point Name:<input type='text' name='wifiName' placeholder='wifi name'><br>";
  content += "Wifi Password:<input type='password' name='wifiPassword' placeholder='wifi password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form><br>";
  server.send(200, "text/html", content);
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
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
      Update.printError(Serial);
    }
  } else if(upload.status == UPLOAD_FILE_END){
    if(Update.end(true)){ //true to set the size to the current progress
      // update success
    } else {
      Update.printError(Serial);
    }
  }
  yield();
}

void httpUpdateGet() {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", updateIndex);
}

void IdiotWifiServer::start(const char* uuid) {
  started = true;
  strcpy(_uuid, uuid);

  Serial.println("Configuring access point...");
  delay(1000);
  pinMode(BUILTIN_LED, OUTPUT);
  EspControl.turnLedOn();
  
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(_uuid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.println(myIP);

  server.on("/", handleRoot);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, httpUpdateGet);
  server.on("/update", HTTP_POST, httpUpdateAnswer, httpUpdateDo);
  server.begin();
}

void IdiotWifiServer::handleClient() {
  if (!started) {
    return;
  }
  server.handleClient();
  EspControl.toggleLed();
}
