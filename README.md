
# Pissmaster
Pissmaster is a rewrite of [this](https://www.dev-mind.blog/uroflowmetry/) uroflowmetry project. The goal of this project is to create a cheep and simple way to gather urine flow data.

## Hardware
- [ESP32 Arduino-compatible microcontroller](https://www.amazon.com/gp/product/B08D5ZD528)¹
- [SD card module](https://www.amazon.com/dp/B07X478BPL)²³
- [HX711 ADC (analog to digital converter)](https://www.amazon.com/gp/product/B00XRRNCOO)
- [5kg load cell](https://www.adafruit.com/product/4541)
- 4x M4 machine screws and washers
- Miscellaneous wood and screws

### Notes
1. AVR microcontrollers, such as the Arduino Uno, are also supported but they will lack WiFi features.
2. You can also [make your own micro SD card module](https://github.com/espressif/arduino-esp32/tree/master/libraries/SD) out of an SD to micro SD adapter.
3. ESP32 microcontrollers can use their built-in flash instead of an SD card but that is not recommended.

## Assembly
### Mechanical
Use machine screws to attach mounting brackets (e.g. 1/2" plywood) to both sides of the load cell.  
Attach one of the mounting brackets to a sturdy base (e.g. 2x6 lumber).  
Attach the other mounting bracket to a lightweight top plate (e.g. 1/4" MDF).  
![Scale](https://github.com/decrazyo/pissmaster/blob/main/img/scale.jpg)

### Electrical
![Schematic](https://github.com/decrazyo/pissmaster/blob/main/img/schematic.svg)
Connect the red and black wires from the load cell to the E+ and E- terminals of the HX711 ADC module respectively.  
Connect the remaining green and white wires from the load cell to the A+ and A- terminals of the HX711 ADC module.¹  
By default, the DT and SCK pins of the HX711 ADC module are expected to be connected to GPIO pins 16 and 17 respectively. Those pins can be re-defined.  
Connect the SD card module to power² and the SPI pins on the microcontroller.  
By default, the built-in LED is used to indicate the Pissmaster status.  
If your microcontroller does have a built-in LED or suffers from a pin conflict then is it recommended that in connect a dedicated status LED.  
By default, a momentary switch is expected to be connected to GPIO pin 0. That pin can be re-defined.  
If your microcontroller does not include a momentary switch then you will need to connect one to a GPIO pin.³  
![Perfboard](https://github.com/decrazyo/pissmaster/blob/main/img/perfboard.jpg)

#### Notes
1. If you're recording negative measurements then swap the wires that are connected to A+ and A-.
2. Your specific SD card module may need to be powered by 3.3v or 5v. Check manufacturer documentation for details.
3. You may need to modify the firmware depending on how you choose to connect a momentary switch.

## IDE Setup
Add ESP32 boards to your Arduino IDE  
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json`  
Then install the following library.  
- [HX711](https://github.com/bogde/HX711)

## Usage
Format your SD card with a FAT32 file system. By default, data will be written to the root of the SD card.  
The status LED will be turned on when power is applied to the Pissmaster. The status LED will turn off once initialization completes.  
If the Pissmaster encounters an error then the status LED will blink rapidly.  
Additional information will be logged over serial.  

### Calibration
Place an empty measuring cup on the Pissmaster and press the momentary switch to start logging data.  
Fill the measuring up with water.  
Press the momentary switch again to finish logging data.  
Retrieve the data from the SD card.  
Define "LOADCELL_CALIBRATION_MEASUREMENT" with the average measured volume of the filled measuring cup.  
Define "LOADCELL_CALIBRATION_WEIGHT" with the weight of that volume water in grams (google it).  
  
For example, I filled 1 US cup with water and graphed the result.  
![Graph](https://github.com/decrazyo/pissmaster/blob/main/img/graph.png)
The section of the graph outlined in red shows the measured value when the cup is full.  
I averaged the outlined values and got a result of 98391, which I can use to define "LOADCELL_CALIBRATION_MEASUREMENT".  
The weight of 1 US cup of water is 236.59 grams, which I can use to define "LOADCELL_CALIBRATION_WEIGHT".  

### Collection
Place a container on the Pissmaster.  
Press the momentary switch to start logging data.  
The built-in LED on the microcontroller will turn on to let you know that the Pissmaster is logging data.  
Piss into the container (obviously). The use of a funnel is optional but recommended to reduce the influence of stream velocity on the flow data.  
Press the momentary switch again to finish logging data.  
The built-in LED on the microcontroller will turn off again.  
Data can be retrieved from the Pissmaster's built-in web server or by removing the SD card.  
