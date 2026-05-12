# PicoBLEBridge

## English

PicoBLEBridge is firmware for Raspberry Pi Pico W. It receives BLE advertising from sensors that publish data using the BTHome v2 protocol, aggregates selected measurements, and sends averaged values to [narodmon.ru](https://narodmon.ru).

### Hardware

- Raspberry Pi Pico W.
- BTHome v2 BLE sensors.
- USB cable for flashing and editing the configuration partition.

### Flashing

RP2040 flash is split into a bootloader area, active firmware, firmware copy area, and writable storage. Flash both UF2 files in order:

1. Hold BOOTSEL while connecting Pico W to USB.
2. Copy `BLEBridgeBootloader.uf2` to the mounted USB drive.
3. Re-enter BOOTSEL mode.
4. Copy `BLEBridge.uf2` to the mounted USB drive.

### Default operation

At startup the device detects whether it is connected to a computer or only to a power supply.
If a computer is detected, the device works only as a USB flash drive and exposes the internal disk with the work log and the INI settings file. Edit this file directly when needed. In this mode the device does not use the network and does not process any data.

If the device is connected to a power supply, the main operating mode starts.

On first boot the device creates a Wi-Fi access point. Default settings are written to `config.ini` on the internal flash storage.

The web UI is available over Wi-Fi and contains:

- Dashboard: status and aggregate view.
- BLE Devices: discovered BTHome v2 devices, their selection, and user sensor names.
- Settings: network, narodmon, and averaging settings.
- Narodmon Payload: exact text payload that will be sent to narodmon.ru.

### Network settings

`config.ini` contains:

```ini
[network]
mode=ap
ssid=BLEBridge
hostname=blebridge
channel=3
security=open
password=
```

Network settings:

- `mode`: `ap` creates an access point, `client` connects to an existing Wi-Fi network.
- `ssid`: access point name or client network SSID.
- `hostname`: device name in the UI, DHCP hostname, and narodmon payload header.
- `channel`: Wi-Fi channel for AP mode, `1..13`.
- `security`: `open` or `wpa2`.
- `password`: empty for open mode, at least 8 characters for WPA2.

### Measurement settings

- `Measurement Retention (s)`: maximum measurement storage time, from `5` to `600` seconds.
- `Max Measurements per Sensor`: maximum number of measurements for each sensor, from `0` to `1000`; `0` means unlimited.

Aggregate view and narodmon upload use averaged values from currently stored measurements.
This is especially useful for saving BLE device power: the sensor can transmit measurements less often. For example, if the sensor transmits once per minute and Measurement Retention is set to 600 seconds, data will still be sent to the server even if 5 beacon packets out of 6 are lost. Max Measurements allows flexible averaging control. If it is set to 1, only the latest measurement is sent. If it is set to 0, all values received during 600 seconds are averaged before being sent to the server.

### Sensor names

- Names are UTF-8 strings up to 64 bytes, about 32 characters.
- The BLE page can edit names only for tracked devices.
- Aggregate view shows the user name when configured, otherwise the numeric ID.

### Notes

- Writable flash storage is small; logs and `config.ini` use the same partition.
- The bootloader currently is a minimal stub that jumps to the active firmware image.
- Firmware changes that affect network mode normally require reboot.

## Русский

PicoBLEBridge - прошивка для Raspberry Pi Pico W. Она принимает BLE advertising от датчиков, публикующих данные по протоколу BTHome v2, агрегирует выбранные измерения и отправляет усредненные значения на [narodmon.ru](https://narodmon.ru).

### Железо

- Raspberry Pi Pico W.
- BLE-датчики BTHome v2.
- USB-кабель для прошивки и редактирования конфигурационного раздела.

### Прошивка

Flash RP2040 разделен на область bootloader, активную прошивку, область копии прошивки и writable storage. Нужно прошить оба UF2 файла по порядку:

1. Зажать BOOTSEL при подключении Pico W к USB.
2. Скопировать `BLEBridgeBootloader.uf2` на появившийся USB-диск.
3. Снова войти в режим BOOTSEL.
4. Скопировать `BLEBridge.uf2` на появившийся USB-диск.

### Работа по умолчанию

При старте устройство определяет, подключено ли оно к компьютеру или просто к блоку питания.
Если обнаружен компьютер, то устройство работает только как USB Flash-накопитель и дает доступ к внутреннему диску с логом работы и INI-файлом с настройками. Этот файл можно при необходимости отредактировать. В этом режиме устройство не работает с сетью и не обрабатывает никаких данных.

Если устройство подключено к блоку питания, то включается основной режим.

При первом запуске устройство поднимает Wi-Fi точку доступа. Настройки по умолчанию записываются в `config.ini` во внутреннем flash storage.

Web UI доступен по Wi-Fi и содержит:

- Dashboard: статус и aggregate view.
- BLE Devices: найденные BTHome v2 устройства, их выбор и пользовательские имена датчиков.
- Settings: настройки сети, narodmon и усреднения.
- Narodmon Payload: точный текст payload, который будет отправлен на narodmon.ru.

### Настройки сети

`config.ini` содержит:

```ini
[network]
mode=ap
ssid=BLEBridge
hostname=blebridge
channel=3
security=open
password=
```

Настройки сети:

- `mode`: `ap` поднимает точку доступа, `client` подключается к существующей Wi-Fi сети.
- `ssid`: имя точки доступа или SSID клиентской сети.
- `hostname`: имя устройства в UI, DHCP hostname и заголовок payload для narodmon.
- `channel`: Wi-Fi канал для AP mode, `1..13`.
- `security`: `open` или `wpa2`.
- `password`: пустой для open mode, минимум 8 символов для WPA2.

### Настройки измерений

- `Measurement Retention (s)`: максимальное время хранения измерений, от `5` до `600` секунд.
- `Max Measurements per Sensor`: максимальное количество измерений для каждого сенсора, от `0` до `1000`; `0` означает без ограничения.

Aggregate view и отправка на narodmon используют усредненные значения по текущим сохраненным измерениям.
Это особенно полезно для экономии энергии BLE-устройства: можно задать редкую передачу измерений. Так, если задать передачу раз в минуту и Measurement Retention в 600 секунд, то данные отправятся на сервер, даже если будет потеряно 5 beacon-пакетов из 6. Max Measurements позволяет гибко управлять усреднением. Если задать 1, то будет отправлено только одно последнее измерение. Если задать 0, то все значения, полученные за 600 секунд, усреднятся перед отправкой на сервер.

### Имена датчиков

- Имена - UTF-8 строки до 64 байт (примерно 32 символа).
- На странице BLE имена можно редактировать только для tracked устройств.
- Aggregate view показывает пользовательское имя, если оно задано, иначе цифровой ID.

### Примечания

- Writable flash storage небольшой; логи и `config.ini` используют один раздел.
- Bootloader сейчас является минимальной заглушкой, которая переходит в активный образ прошивки.
- Изменения прошивки, влияющие на режим сети, обычно требуют перезагрузки.
