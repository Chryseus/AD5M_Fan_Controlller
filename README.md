# AD5M Fan Controlller

This is a work in progress fan controller for the Flashforge AD5M / AD5X 3d printer that provides remote fan control over Wifi as well as chamber monitoring.

## Hardware

This is designed for the ESP32 C3 Super Mini but will work with any arduino compatible device that supports Wifi.

BME280 is used for temperature, pressure and humidity sensing.

ENS160 is used for air quality sensing.

Any PWM driver will work for the fans, I've used DRV8871.

Recirculation fan is a 5015 blower, 24V 8500 RPM max.

Exhaust fan is a 4020 24V.

Power is drawn from the built in 24V supply and regulated down to 5V for the ESP32 using a 7805, the ESP32 3.3V regulator is used to power the sensors.

## 3D Printed Parts

For the recirculation duct [this](https://www.printables.com/model/1243577-flashforge-adventurer-5m-recirculating-air-duct-50) is used for the AD5M, for AD5X users [this](https://www.printables.com/model/1473971-ad5x-internal-circulation-air-filtration-duct-with) or [this](https://www.printables.com/model/1472826-ad5x-recirculation-duct-blower-5015) may work.

For the exhaust duct I have used [this](https://www.printables.com/model/1375541-adventurer-5m-venting-kit-for-the-non-pro-version).

### Roadmap

- [x] Manual fan control

- [x] Sensor display

- [ ] Schematic

- [ ] 3D printed mounting parts

- [ ] Automatic mode based on sensor data

**Possible future additions**

- [ ] Data logging / plotting

- [ ] Send data to external server

- [ ] 3D printer communication

- [ ] Custom PCB
