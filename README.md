# cumulus
A simple ESP32 based AC relay controller for my water heater 

## Quickstart

Be aware that this project is tune to my personal needs. It probably some changes to be useful to anyone else.

ESP-IDF shall be properly installed and configured. You may have to set en IDF environment with something like

```
source .../path/to/esp-idf/export.sh
```

This application is built using the same principals used in most ESP-IDF examples.

Select your ESP32 target with something like 

```
idf.py set-target esp32-xxxx
```

If needed, make some changes to the `*** Project Configuration ***` and other system configurations 
with 

```
idf.py menuconfig
```

Then build the project with 

```
idf.py build
```

Finally, flash it to the device and monitor its behavior.

```
idf.py flash monitor
```





