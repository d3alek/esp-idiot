First install as per ../docs/how-to-install

sudo cp /idiot/esp-idiot/esp_logger/esp_logger.service /lib/systemd/system/
Copy otselo-id-rsa and .pub in /idiot/esp-idiot/esp_logger/

sudo systemctl daemon-reload
sudo systemctl enable esp_logger
sudo systemctl restart esp_logger

