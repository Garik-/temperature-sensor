# DIY WiFi Temperature sensor for smart home

I used [Tabler](https://tabler.io/), JS and SSE for dynamic updating of readings.

![preview](docs/preview.png)

## Install server
1. Build server
```sh
cd server
make build
```
2. Deploy to Raspberry Pi 64-bit
```
scp bin/temperature-sensor-arm64 pi@raspberrypi.local:/home/pi/bin/temperature-sensor
scp temperature-sensor.service pi@raspberrypi.local:/home/pi/bin/
```
3. Launch service on Raspberry Pi   
```sh
sudo -s
mv temperature-sensor.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable temperature-sensor
systemctl start temperature-sensor
```
check
```sh
systemctl status temperature-sensor
journalctl -u temperature-sensor -f
```

## List of components
- [ESP32-C3 Super Mini](https://github.com/sidharthmohannair/Tutorial-ESP32-C3-Super-Mini)
- [BME280](https://sl.aliexpress.ru/p?key=8LKXGPY) absolute pressure, temperature and humidity sensor
- [TP4056](https://sl.aliexpress.ru/p?key=zPKXG7G) charger for 18650 Li-on, Li-po batteries
- [TPS63020](https://sl.aliexpress.ru/p?key=fyKXGnz) lithium battery USB automatic boost buck step up down module
