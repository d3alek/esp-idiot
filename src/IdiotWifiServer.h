#ifndef IDIOT_WIFI_SERVER_H
#define IDIOT_WIFI_SERVER_H

#include "EspControl.h"

// these must be included in .ino file because http://stackoverflow.com/a/6506611/5799810
#include "ESP8266WiFi.h"
#include "ESP8266WebServer.h" 
#include "EspPersistentStore.h"
#include "ESP8266HTTPClient.h"
#include "ESP8266httpUpdate.h"

class IdiotWifiServer {
    public:
        void start(const char*);
        void handleClient();
    private:
      
      friend void handleRoot();
      friend void handleWifiCredentialsEntered();
      friend void httpUpdateGet();
      friend void httpUpdateAnswer();
      friend void httpUpdateDo();
};
#endif
