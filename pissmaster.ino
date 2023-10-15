
// ========================
// Begin user configuration
// ========================

// Define the following to enable WiFi features.
// Note that WiFi features are temporally disabled while data is actively being logged.
// These settings only apply to ESP32 microcontrollers.
// 
// #define WIFI_SSID "<ENTER SSID>"
// #define WIFI_PASSWORD "<ENTER PASSWORD>"
// #define WIFI_HOSTNAME "pissmaster"

// Define the following to calibrate the load cell.
// See the "Calibration" section of "README.md" for instructions.
// 
// #define LOADCELL_CALIBRATION_WEIGHT <known weight of an object in grams>
// #define LOADCELL_CALIBRATION_MEASUREMENT <measured value of the object with known weight>

// Pins connected to the load cell analog to digital converter.
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 17

// Milliseconds to wait for the load cell to be ready.
#define LOADCELL_TIMEOUT 2000

// Milliseconds to wait for a serial connection.
#define SERIAL_TIMEOUT 2000

// Comment this out to remove the header from *.csv files.
#define LOG_HEADER

// Directory in which to store logged data.
#define LOG_DIR "/"

// Window size for moving mean/median calculations.
#define WINDOW_SAMPLES 5

// Function used to smooth data samples.
#define WINDOW_FUNCTION "AVERAGE"
// #define WINDOW_FUNCTION "MEDIAN"

// SD card chip select pin.
// The SPI SS pin of your chosen microcontroller will be used by default.
// Any GPIO pin can be specified instead.
// This can be undefined on ESP32 microcontrollers to use the chip's internal flash.
#define SD_CS_PIN

// Pin connected to a momentary switch.
// This is used to start and finish logging data.
#define BUTTON_PIN 0

// Whether or not to use the BUTTON_PIN's internal pullup resistor.
#define BUTTON_PULLUP false

// Pin connected to an LED to indicate the system's status.
// If this is left undefined then LED_BUILTIN will be used.
// 
// #define STATUS_LED_PIN 2

// ======================
// End user configuration
// ======================

// Check that we are building for a supported architecture.
// #if !defined(ESP32) && !defined(AVR)
// #error Unknown architecture.
// #endif

#if defined(LOADCELL_CALIBRATION_MEASUREMENT) && defined(LOADCELL_CALIBRATION_WEIGHT)
#define LOADCELL_DIVIDER LOADCELL_CALIBRATION_MEASUREMENT / LOADCELL_CALIBRATION_WEIGHT
#else
#define LOADCELL_DIVIDER 1
#endif

#if !defined(STATUS_LED_PIN) && defined(LED_BUILTIN)
#define STATUS_LED_PIN LED_BUILTIN
#endif

// This library may emit warnings about portability during compilation.
// That is expected and can be ignored.
// https://github.com/bogde/HX711
#include <HX711.h> // designed with v0.7.5

#ifdef ESP32
// https://github.com/espressif/arduino-esp32/tree/master/libraries/FS
#include <FS.h>
#ifdef SD_CS_PIN
// https://github.com/espressif/arduino-esp32/tree/master/libraries/SD
#include <SD.h>
#else
// Use internal flash.
// https://github.com/espressif/arduino-esp32/tree/master/libraries/FFat
#include <FFat.h>
#endif // ifdef SD_CS_PIN
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
#define WIFI_ENABLED
// https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
#include <WiFi.h>
// https://github.com/espressif/arduino-esp32/tree/master/libraries/WebServer
#include <WebServer.h>
// https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient
#include <HTTPClient.h>
#endif // if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
#else
// AVR
// https://github.com/arduino-libraries/SD
#include <SPI.h>
#include <SD.h>
#ifndef SD_CS_PIN
#define SD_CS_PIN
#endif // ifndef SD_CS_PIN
#endif // ifdef ESP32



// Called in the event of an unrecoverable error.
// This is usually caused by a hardware failure such as a wire coming unplugged.
void fatalError() {
  Serial.println(F("Fatal error."));
#ifdef STATUS_LED_PIN
  uint8_t state = !digitalRead(STATUS_LED_PIN);
#endif // ifdef STATUS_LED_PIN
  // Blink an LED to indicate an error.
  while(true) {
#ifdef STATUS_LED_PIN
    digitalWrite(STATUS_LED_PIN, state);
    state = !state;
#endif // ifdef STATUS_LED_PIN
    delay(200);
  }
}



class Button {
private:
  int mButtonPin;
  int mButtonState;
  int mButtonpDefault;
public:
  void begin(int buttonPin, bool pullUp) {
    mButtonPin = buttonPin;
    pinMode(mButtonPin, pullUp ? INPUT_PULLUP : INPUT);
    mButtonState = digitalRead(mButtonPin);
    mButtonpDefault = mButtonState;
  }

  bool pressed() {
    bool buttonPressed = false;
    uint8_t newState = digitalRead(mButtonPin);

    if(newState != mButtonState && newState != mButtonpDefault) {
      buttonPressed = true;
    }

    mButtonState = newState;

    return buttonPressed;
  }
};



class Clock {
private:
  unsigned long mReset;
public:
  void reset() {
    mReset = millis();
  }

  unsigned long time() {
    unsigned long now = millis();

    if(now < mReset) {
      // millis() has overflowed.
      unsigned long diff = ((unsigned long) -1) - mReset;
      return now + diff;
    }
    else {
      return now - mReset;
    }
  }
};



class Logger {
private:
#ifdef ESP32
  fs::FS& mFileSystem;
#else // AVR
  SDClass& mFileSystem;
#endif // ifdef ESP32
  String mLogDir;
  File mLogFile;
  unsigned long mWindowSamples;
  String mWindowFunction;
  bool mLogHeader;
  unsigned long mRow;

public:
#ifdef ESP32
  Logger(fs::FS& fileSystem, String logDir, unsigned long windowSamples, String windowFunction) :
#else // AVR
  Logger(SDClass& fileSystem, String logDir, unsigned long windowSamples, String windowFunction) :
#endif // ifdef ESP32
    mFileSystem(fileSystem),
    mLogDir(logDir),
    mWindowSamples(windowSamples),
    mWindowFunction(windowFunction),
    mLogHeader(false),
    mRow(1) {}

  void createLogDir() {
    Serial.println("Checking for log directory '" + mLogDir + "'.");

    if(mLogDir == "/") {
      Serial.println(F("Logging to file system root"));
    }
    else if(mFileSystem.exists(mLogDir)) {
      File dir = mFileSystem.open(mLogDir);

      if(!dir.isDirectory()) {
        Serial.println("Error: '" + mLogDir + "' is not a directory.");
        fatalError();
      }
    }
    else {
      Serial.println("Creating log directory '" + mLogDir + "'.");

      if(!mFileSystem.mkdir(mLogDir)) {
        Serial.println("Error: Failed to create log directory '" + mLogDir + "'.");
        fatalError();
      }
    }
    Serial.println(F("Log directory ready."));
  }

  void openLogFile() {
    Serial.println(F("Locating log files."));
    File dir = mFileSystem.open(mLogDir);

    if(!dir){
      Serial.println("Error: Failed to open log directory '" + mLogDir + "'");
      fatalError();
    }

    if(!dir.isDirectory()) {
      Serial.println("Error: '" + mLogDir + "' is not a directory.");
      fatalError();
    }

    long fileNumberMax = 0;

    Serial.println(F("Indexing log files."));
    // Find the file/dir with the largest number as it's name.
    for(File file = dir.openNextFile(); file; file = dir.openNextFile()) {
      String name = String(file.name());

      // Strip off the file extension if it exists.
      auto index = name.lastIndexOf('.');
      if(index != -1) {
        name.remove(index);
      }

      long fileNumber = name.toInt();

      if(fileNumber > fileNumberMax) {
        fileNumberMax = fileNumber;
      }

      file.close();
    }

    dir.close();

    String path;

    if(mLogDir.endsWith("/")) {
      path = mLogDir + String(fileNumberMax + 1) + ".csv";
    }
    else {
      path = mLogDir + "/" + String(fileNumberMax + 1) + ".csv";
    }

    Serial.println("Creating log file '" + path + "'.");
    File log = mFileSystem.open(path, FILE_WRITE);

    if(!log){
      Serial.println("Error: Failed to open new log file '" + path + "' for writing.");
      fatalError();
    }

    mLogFile = log;
  }

  void writeLogHeader() {
    Serial.println(F("Writing header."));

    String header = 
      "time(ms)\t"
      "time(s)\t"
      "time delta(s)\t"
      "volume(ml)\t"
      "volume average(ml)\t"
      "volume average delta(ml)\t"
      "flow(ml/s)\n";

    if(mLogFile.print(header)){
      mLogHeader = true;
      mRow++;
    }
    else {
      Serial.println(F("Error: Failed to write header."));
    }
  }

  void writeLogData(unsigned long time, float volume) {
    String rowCurrent = String(mRow);
    String rowNext = String(mRow + 1);
    String rowWindow = String(mRow + mWindowSamples - 1);

    // time(ms)
    String colA = String(time);
    // time(s)
    // =A[i]/1000
    String colB = String("=A" + rowCurrent + "/1000");
    // time delta(s)
    // =B[i+1]-B[i]
    String colC = String("=B" + rowNext + "-B" + rowCurrent);

    // volume(ml)
    String colD = String(volume);
    // volume average(ml)
    // =AVERAGE(D[i]:D[i+j])
    String colE = String("=" + mWindowFunction + "(D" + rowCurrent + ":D" + rowWindow + ")");
    // volume average delta(ml)
    // =E[i+1]-E[i]
    String colF = String("=E" + rowNext + "-E" + rowCurrent);

    // flow(ml/s)
    // =F[i]/C[i]
    String colG = String("=F" + rowCurrent + "/C" + rowCurrent);

    String line = colA;
    line += "\t" + colB;
    line += "\t" + colC;
    line += "\t" + colD;
    line += "\t" + colE;
    line += "\t" + colF;
    line += "\t" + colG;

    if(mLogFile.println(line)){
      mRow++;
    }
    else {
      Serial.println(F("Error: Failed to write data."));
    }
  }

  void closeLogFile() {
    if(mLogFile) {
        mLogFile.close();
    }
    mRow = 1;
  }
};



#ifdef WIFI_ENABLED
// ESP32 exclusive feature
class FileBrowser : public RequestHandler {
private:
  fs::FS& mFileSystem;
public:
  FileBrowser(fs::FS& fileSystem) : mFileSystem(fileSystem) {}

  bool canHandle(HTTPMethod method, String uri) {
    return true;
  }

  String normalizeUri(String uri) {
    // Reduce repeated slashes to a single slash.
    while(uri.indexOf("//") != -1) {
      uri.replace("//", "/");
    }

    // Remove trailing slashes unless we are at the root.
    // The filesystem doesn't like trailing slashes.
    while(uri.endsWith("/") && uri != "/") {
      uri.remove(uri.lastIndexOf("/"));
    }

    // Ensure that the path starts with a slash.
    if(!uri.startsWith("/")) {
      uri = "/" + uri;
    }

    return uri;
  }

  bool handle(WebServer& server, HTTPMethod method, String uri) {
    uri = normalizeUri(server.urlDecode(uri));

    Serial.println("Handling '" + uri + "'");

    if(!mFileSystem.exists(uri)) {
      server.send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found.");
    }

    if(method == HTTP_GET) {
      File file = mFileSystem.open(uri);

      if(file.isDirectory()) {
        String page;
        String href;

        // Link to parent directory.
        if(uri != "/") {
          href = uri;
          href.remove(href.lastIndexOf("/"));
          href = normalizeUri(href);
          page += "<a href=\"" + href + "\">..</a></br>\n";
        }

        // Link to directory contents.
        for(File next = file.openNextFile(); next; next = file.openNextFile()) {
          href = normalizeUri(uri + "/" + next.name());
          page += "<a href=\"" + href + "\">" + next.name() + "</a></br>\n";
        }

        server.send(HTTP_CODE_OK, "text/html", "<html><body>" + page + "</body></html>");
      }
      else {
        server.streamFile(file, "application/octet-stream");
      }
    }
    else {
      server.send(HTTP_CODE_METHOD_NOT_ALLOWED, "text/plain", "Method not allowed.");
    }

    return true;
  }
};

WebServer gWebServer;
#endif // ifdef WIFI_ENABLED

#ifdef SD_CS_PIN
Logger gLogger(SD, LOG_DIR, WINDOW_SAMPLES, WINDOW_FUNCTION);
#else
Logger gLogger(FFat, LOG_DIR, WINDOW_SAMPLES, WINDOW_FUNCTION);
#endif // ifdef SD_CS_PIN

HX711 gCell;
Button gButton;
Clock gClock;
bool gMeasuring = false;



void setup() {
#ifdef STATUS_LED_PIN
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH);
#endif // ifdef STATUS_LED_PIN

  gButton.begin(BUTTON_PIN, BUTTON_PULLUP);

  Serial.begin(115200);
  auto serialBegin = millis();
  while(!Serial && serialBegin + SERIAL_TIMEOUT > millis()) {}
  Serial.println(F("Serial ready."));

#ifdef SD_CS_PIN
  Serial.println(F("Initializing SD card."));
  if(!SD.begin(SD_CS_PIN)) {
    Serial.println(F("Error: Failed to initialize SD card."));
    fatalError();
  }
  Serial.println(F("SD card ready."));
#else
  Serial.println(F("Initializing flash."));
  if(!FFat.begin(true)) {
    Serial.println(F("Error: Failed to initialize flash."));
    fatalError();
  }
  Serial.println(F("Flash ready."));
#endif // ifdef SD_CS_PIN

  Serial.println(F("Initializing load cell."));
  gCell.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  if(!gCell.wait_ready_timeout(LOADCELL_TIMEOUT)) {
    Serial.println(F("Error: Failed to initialize load cell."));
    fatalError();
  }
  gCell.set_scale(LOADCELL_DIVIDER);
  Serial.println(F("Load cell ready."));

#ifdef WIFI_ENABLED
  Serial.println(F("Initializing WiFi."));
  Serial.println("Connecting to '" + String(WIFI_SSID) + "'.");
  WiFi.mode(WIFI_STA);
#ifdef WIFI_HOSTNAME
  WiFi.setHostname(WIFI_HOSTNAME);
#endif // ifdef WIFI_HOSTNAME
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  Serial.println(F("Connected."));

  Serial.println(F("Initializing web server."));
#ifdef SD_CS_PIN
  gWebServer.addHandler(new FileBrowser(SD));
#else
  gWebServer.addHandler(new FileBrowser(FFat));
#endif // ifdef SD_CS_PIN
  gWebServer.begin();
  Serial.println(F("Web server ready."));
  Serial.println("http://" + WiFi.localIP().toString() + "/");
#endif // ifdef WIFI_ENABLED

  Serial.println(F("Pissmaster ready!"));
#ifdef STATUS_LED_PIN
  digitalWrite(STATUS_LED_PIN, LOW);
#endif // ifdef STATUS_LED_PIN
}



void loop() {
  // Toggle between measuring flow and serving files over WiFi when the button is pressed.
  bool buttonPressed = gButton.pressed();
  if(buttonPressed) {
    gMeasuring = !gMeasuring;
  }

  if(gMeasuring) {
    if(buttonPressed) {
      gLogger.createLogDir();
      gLogger.openLogFile();
#ifdef LOG_HEADER
      gLogger.writeLogHeader();
#endif // ifdef LOG_HEADER
      gCell.tare();
      gClock.reset();
      Serial.println(F("Logging started"));
#ifdef STATUS_LED_PIN
      digitalWrite(STATUS_LED_PIN, HIGH);
#endif // ifdef STATUS_LED_PIN
    }

    gLogger.writeLogData(gClock.time(), gCell.get_units());
  }
  else {
    if(buttonPressed) {
      gLogger.closeLogFile();
      Serial.println(F("Logging finished"));
#ifdef STATUS_LED_PIN
      digitalWrite(STATUS_LED_PIN, LOW);
#endif // ifdef STATUS_LED_PIN
    }

#ifdef WIFI_ENABLED
    gWebServer.handleClient();
#endif // ifdef WIFI_ENABLED
  }
}
