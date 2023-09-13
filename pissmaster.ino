
// https://www.arduino.cc/reference/en/libraries/hx711-arduino-library/
// https://github.com/bogde/HX711
#include <HX711.h> // v0.7.5

// Set these to the pins that the load cell is connected to.
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 17

// Used to calibrate the load cell.
// LOADCELL_DIVIDER = average(measurements) / known weight.
#define LOADCELL_DIVIDER 1

// Milliseconds to wait for the load cell to settle.
#define LOADCELL_SETTLE 2000

HX711 cell;

void setup() {
  // Use the LED to indicate that we are booting up.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  while(!Serial) {}

  cell.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  delay(LOADCELL_SETTLE);

  cell.set_scale(LOADCELL_DIVIDER);
  cell.tare();

  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("time,units");
}

void loop() {
  Serial.print(millis());
  Serial.print(",");
  Serial.print(cell.get_units(), 8);
  Serial.println("");
}
