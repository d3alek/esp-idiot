class EspPersistentStore {
  public:
      void begin();
      void putWifiName(const char* name);
      void putWifiPassword(const char* password);
      void readWifiName(char* wifiName);
      void readWifiPassword(char* wifiPassword);
      bool wifiCredentialsStored();
      void clear();
  private:
    void _putString(int startingOffset, const char* string);
    void _readString(int startingOffset, char* string);
};

extern EspPersistentStore PersistentStore;
