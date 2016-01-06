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
      bool configStored();
      void putWifiAttemptsFailed(int attempts);
      int readWifiAttemptsFailed();
      void clear();
  private:
    void _putString(int startingOffset, const char* string);
    void _readString(int startingOffset, char* string);
};

extern EspPersistentStore PersistentStore;
