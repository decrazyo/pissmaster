
// This library may emit warnings about portability during compilation.
// That is expected and can be ignored.
// https://github.com/bogde/HX711
#include <HX711.h> // v0.7.5

// https://github.com/espressif/arduino-esp32
#include <FS.h>

// Obvious WiFi stuff.
#define WIFI_HOSTNAME "pissmaster"
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Comment this out to remove the header from *.csv files.
#define LOG_HEADER

// Window size for moving median and moving mean calculations.
#define SAMPLE_WINDOW 5

// Directory in which to store logged data.
#define LOG_DIR "/pissmaster"

// SD card chip select pin.
// Comment this out to use the chip's internal flash.
#define SD_CS_PIN 5

// Pin connected to momentary switch for switching modes.
#define BUTTON_PIN 0
// Whether or not to use the BUTTON_PIN's internal pullup resistor.
#define BUTTON_PULLUP false

// #define LOADCELL_CALIBRATION_MEASUREMENT 
// #define LOADCELL_CALIBRATION_WEIGHT 
// 1 US cup of water (236.59 grams) measured 96421.87445887446
#define LOADCELL_CALIBRATION_MEASUREMENT 96421.87445887446
#define LOADCELL_CALIBRATION_WEIGHT 236.59

// Pins connected to the load cell analog to digital converter.
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 17

// Milliseconds to wait for the load cell to be ready.
#define LOADCELL_TIMEOUT 2000

// Milliseconds to wait for a serial connection.
#define SERIAL_TIMEOUT 2000


#if defined(LOADCELL_CALIBRATION_MEASUREMENT) && defined(LOADCELL_CALIBRATION_WEIGHT)
#define LOADCELL_DIVIDER LOADCELL_CALIBRATION_MEASUREMENT / LOADCELL_CALIBRATION_WEIGHT
#else
#define LOADCELL_DIVIDER 1
#endif

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
#define WIFI_ENABLED
#endif

#if defined(SD_CS_PIN)
#define LOG_TO_SD
#endif

void fatalError() {
  Serial.println("Fatal error.");
#ifdef LED_BUILTIN
  uint8_t state = !digitalRead(LED_BUILTIN);
#endif
  while(true) {
#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, state);
    state = !state;
#endif
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
  fs::FS& mFileSystem;
  String mLogDir;
  File mLogFile;
  unsigned long mSampleWindow;
  bool mLogHeader;
  unsigned long mRow;

public:
  Logger(fs::FS &fileSystem, String logDir, unsigned long sampleWindow) :
    mFileSystem(fileSystem),
    mLogDir(logDir),
    mSampleWindow(sampleWindow),
    mLogHeader(false),
    mRow(1) {}

  void createLogDir() {
    Serial.println("Checking for log directory '" + mLogDir + "'.");
    if(mFileSystem.exists(mLogDir)) {
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
    Serial.println("Log directory ready.");
  }

  void openLogFile() {
    Serial.println("Locating log files.");
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

    Serial.println("Indexing log files.");
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

    String path = String(mLogDir) + "/" + String(fileNumberMax + 1) + ".csv";
    Serial.println("Creating log file '" + path + "'.");
    File log = mFileSystem.open(path, FILE_WRITE);

    if(!log){
      Serial.println("Error: Failed to open new log file '" + path + "' for writing.");
      fatalError();
    }

    mLogFile = log;
  }

  void writeLogHeader() {
    Serial.println("Writing header.");

    String header = 
      "time\tweight\tweight median\tweight average\t"
      "time delta\tweight delta\tweight median delta\tweight average delta\t"
      "flow\tmedian flow\taverage flow\n";

    if(mLogFile.print(header)){
      mLogHeader = true;
      mRow++;
    }
    else {
      Serial.println("Error: Failed to write header.");
    }
  }


  void writeLogData(unsigned long time, float weight) {
    String rowCurrent = String(mRow);
    String rowNext = String(mRow + 1);
    String rowWindow = String(mRow + mSampleWindow - 1);

    String colA = String(time);
    String colB = String(weight);

    // =MEDIAN(B[row]:B[row+window-1]
    // weight median
    String colC = String(
      "=MEDIAN(B" + rowCurrent + ":B" + rowWindow + ")");
    // weight average
    String colD = String(
      "=AVERAGE(B" + rowCurrent + ":B" + rowWindow + ")");

    // =IF(A[row+1]="","",A[row+1]-A[row])
    // time delta
    String colE = String(
      "=IF(A" + rowNext + "=\"\",\"\",A" + rowNext + "-A" + rowCurrent + ")");
    // weight delta
    String colF = String(
      "=IF(B" + rowNext + "=\"\",\"\",B" + rowNext + "-B" + rowCurrent + ")");
    // weight median delta
    String colG = String(
      "=IF(C" + rowNext + "=\"\",\"\",C" + rowNext + "-C" + rowCurrent + ")");
    // weight average delta
    String colH = String(
      "=IF(D" + rowNext + "=\"\",\"\",D" + rowNext + "-D" + rowCurrent + ")");

    // =IF(E[row]="","",F[row]/E[row])
    // flow
    String colI = String(
      "=IF(E" + rowCurrent + "=\"\",\"\",F" + rowCurrent + "/E" + rowCurrent + ")");
    // median flow
    String colJ = String(
      "=IF(E" + rowCurrent + "=\"\",\"\",G" + rowCurrent + "/E" + rowCurrent + ")");
    // average flow
    String colK = String(
      "=IF(E" + rowCurrent + "=\"\",\"\",H" + rowCurrent + "/E" + rowCurrent + ")");

    String line = colA;
    line += "\t" + colB;
    line += "\t" + colC;
    line += "\t" + colD;
    line += "\t" + colE;
    line += "\t" + colF;
    line += "\t" + colG;
    line += "\t" + colH;
    line += "\t" + colI;
    line += "\t" + colJ;
    line += "\t" + colK;

    if(mLogFile.println(line)){
      mRow++;
    }
    else {
      Serial.println("Error: Failed to write data.");
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
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
class FileBrowser : public RequestHandler {
private:
  fs::FS& mFileSystem;
public:
  FileBrowser(fs::FS& fileSystem) : mFileSystem(fileSystem) {}

  bool canHandle(HTTPMethod method, String uri) {
    return true;
  }

  String normalizeUri(String uri) {
    while(uri.indexOf("//") != -1) {
      uri.replace("//", "/");
    }

    while(uri.endsWith("/") && uri != "/") {
      uri.remove(uri.lastIndexOf("/"));
    }

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

        if(uri != "/") {
          href = uri;
          href.remove(href.lastIndexOf("/"));
          href = normalizeUri(href);
          page += "<a href=\"" + href + "\">..</a></br>\n";
        }

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
#endif

#ifdef LOG_TO_SD
#include <SD.h>
Logger gLogger(SD, LOG_DIR, SAMPLE_WINDOW);
#else
#include <FFat.h>
Logger gLogger(FFat, LOG_DIR, SAMPLE_WINDOW);
#endif

HX711 gCell;
Button gButton;
Clock gClock;
bool gMeasuring = false;


void setup() {
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
#endif

  gButton.begin(BUTTON_PIN, BUTTON_PULLUP);

  Serial.begin(115200);
  auto serialBegin = millis();
  while(!Serial && serialBegin + SERIAL_TIMEOUT > millis()) {}
  Serial.println("Serial ready.");

#ifdef LOG_TO_SD
  Serial.println("Initializing SD card.");
  if(!SD.begin(SD_CS_PIN)) {
    Serial.println("Error: Failed to initialize SD card.");
    fatalError();
  }
  Serial.println("SD card ready.");
#else
  Serial.println("Initializing flash.");
  if(!FFat.begin(true)) {
    Serial.println("Error: Failed to initialize flash.");
    fatalError();
  }
  Serial.println("Flash ready.");
#endif

  Serial.println("Initializing load cell.");
  gCell.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  if(!gCell.wait_ready_timeout(LOADCELL_TIMEOUT)) {
    Serial.println("Error: Failed to initialize load cell.");
    fatalError();
  }
  gCell.set_scale(LOADCELL_DIVIDER);
  Serial.println("Load cell ready.");

#ifdef WIFI_ENABLED
  Serial.println("Initializing WiFi.");
  Serial.println("Connecting to '" + String(WIFI_SSID) + "'.");
  WiFi.mode(WIFI_STA);
#ifdef WIFI_HOSTNAME
  WiFi.setHostname(WIFI_HOSTNAME);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  Serial.println("Connected.");

  Serial.println("Initializing web server.");
#ifdef LOG_TO_SD
  gWebServer.addHandler(new FileBrowser(SD));
#else
  gWebServer.addHandler(new FileBrowser(FFat));
#endif
  gWebServer.begin();
  Serial.println("Web server ready.");
  Serial.println("http://" + WiFi.localIP().toString() + "/");
#endif

  Serial.println("Pissmaster ready!");
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, LOW);
#endif
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
#endif
      gClock.reset();
      gCell.tare();
      Serial.println("Logging started");
#ifdef LED_BUILTIN
      digitalWrite(LED_BUILTIN, HIGH);
#endif
    }

    gLogger.writeLogData(gClock.time(), gCell.get_units());
  }
  else {
    if(buttonPressed) {
      gLogger.closeLogFile();
      Serial.println("Logging finished");
#ifdef LED_BUILTIN
      digitalWrite(LED_BUILTIN, LOW);
#endif
    }

#ifdef WIFI_ENABLED
    gWebServer.handleClient();
#endif
  }
}
