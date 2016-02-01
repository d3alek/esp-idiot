#ifndef ESP_PERSISTENT_STORE_H
#define ESP_PERSISTENT_STORE_H

#define CONFIG_MAX_SIZE 200

#define WIFI_NAME_OFFSET 100
#define WIFI_PASSWORD_OFFSET 150
#define CONFIG_OFFSET 200

#define WIFI_NAME_STORED_BIT 1
#define WIFI_PASSWORD_STORED_BIT 2

class EspPersistentStore {
  public:
      void begin();
      void putWifiName(const char* name);
      void putWifiPassword(const char* password);
      void putConfig(const char* config);
      void readWifiName(char* wifiName);
      void readWifiPassword(char* wifiPassword);
      void readConfig(char* config);
      bool wifiCredentialsStored();
      void clear();
  private:
    void _putString(int startingOffset, const char* string);
    void _readString(int startingOffset, char* string);
    void _readString(int startingOffset, char* string, int maxSize);
};

extern EspPersistentStore PersistentStore;

#endif
