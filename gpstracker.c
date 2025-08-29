/* 
 * ======================================================================
 *  hardware:
 *  1x ESP32 LilyGo SIM7000 Board with GPS, LTE modem, battery
 *  3x DS18B20 1wire temperature sensor (outdoor at best)
 *  1x 4.7k resistor (pullup for 1wire)
 * ----------------------------------------------------------------------
 *  pin mapping:
 *  GPIO32: data of 1wire (all DS18B20 sensors)
 *  GPIO32 -> R4.7k -> 3.3V
 *  3.3V, GND to +, GND of all DS18B20 sensors
 * ----------------------------------------------------------------------
 * initial source: https://randomnerdtutorials.com/lilygo-t-sim7000g-
 *                 esp32-lte-gprs-gps/
 * with plenty of help by Google, several 1wire experts and ChatGPT
 * ======================================================================
 *  about this file:
 *  This firmware is for trackers for boats, campers etc. - objects you
 *  want to be sure where they are and how they're doing. It's based on
 *  the Lilygo SIM7000G board: ESP32, GPS, LTE/CAT/NB-IoT modem,
 *  battery, charge logic (+SD card, not used here).
 *  What it does:
 *  Approx. at 08:00h UTC (=10h European summertime) the device starts
 *  up, opens up a cellular data connection, determines its own GPS
 *  position, date (unused) and time, measures the temperature of three
 *  1wire sensors and sends the whole package to a webhook. Then it goes
 *  back to sleep.
 *  The webhook is necessary because calling the SSL-protected Telegram
 *  API via AT commands is a nightmare; the webhook (more or less) just
 *  translates HTTP to the HTTPS Telegram API URL.
 * ======================================================================
 */

// the GSM/AT command specific code is mostly untouched and originates:
// https://randomnerdtutorials.com/lilygo-t-sim7000g-esp32-lte-gprs-gps/
#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
#define SerialAT Serial1

// ----------------------------------------------------------------------
// MODEM
// ----------------------------------------------------------------------

// set your SIM card's PIN, if any; "" for unprotected SIM
#define GSM_PIN ""

// set to your carrier's APN, get it from your carrier's website
// const char apn[]  = "iot.1nce.net"; // 1NCE, German Dt. Telekom branch
const char apn[]  = "YOUR_CARRIERS_APN_GOES_HERE";
const char gprsUser[] = "";
const char gprsPass[] = "";

// ----------------------------------------------------------------------
// INCLUDES
// ----------------------------------------------------------------------

#include <TinyGsmClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// See all AT commands on serial monitor if wanted; for bugfixing only
// #define DUMP_AT_COMMANDS

#ifdef DUMP_AT_COMMANDS  // if enabled it requires the streamDebugger lib
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, Serial);
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif

// ----------------------------------------------------------------------
// CONSTANTS & CONFIGURATION
// ----------------------------------------------------------------------

#define uS_TO_S_FACTOR 1000000ULL  // micro seconds to seconds
#define TIME_TO_SLEEP  86400       // 24h between wakeups (in seconds)

#define CORR_FACTOR    2000        // if your personal ESP32 is too fast
                                   // (like mine), add the time [in sec]
                                   // it wakes up too early. Be generous.

#define UART_BAUD   115200
#define PIN_DTR     25
#define PIN_TX      27
#define PIN_RX      26
#define PWR_PIN     4
#define LED_PIN     12

const int oneWireBus = 32;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);     

// ----------------------------------------------------------------------
// FUNCTIONS
// ----------------------------------------------------------------------

void sendTelegramMessage(float lat, float lon, 
                         float t1, float t2, float t3) {

  // URL of HTTP-2-HTTPS script (e.g. PHP; to be provided separately!)
  String url = "YOUR_WEBHOOK_URL_GOES_HERE";     // adjust to your URL
  url += "?b=BOTID&k=API_KEY&r=RECIPIENT";       // basic security

  url += "&lat=" + String(lat, 6);
  url += "&lon=" + String(lon, 6);

  if (!isnan(t1))      url += "&t1=" + String(t1, 1);
  if (!isnan(t2))      url += "&t2=" + String(t2, 1);
  if (!isnan(t3))      url += "&t3=" + String(t3, 1);

  // HTTP using AT commands - the old school way ;-)
  modem.sendAT("+HTTPTERM");
  modem.waitResponse(5000);
  modem.sendAT("+HTTPINIT");
  modem.waitResponse(5000);
  modem.sendAT("+HTTPPARA=\"CID\",1");
  modem.waitResponse(2000);
  modem.sendAT("+HTTPPARA=\"URL\",\"" + url + "\"");
  modem.waitResponse(2000);
  modem.sendAT("+HTTPACTION=0");  // 0 = GET

  if (modem.waitResponse("+HTTPACTION:") == 1) {
    String response = SerialAT.readStringUntil('\n');

    int codeIndex = response.indexOf(',');
    int statusCode = response.substring(codeIndex + 1,
      response.indexOf(',', codeIndex + 1)).toInt();
    Serial.println("HTTP status ..: " + String(statusCode));

    if (statusCode != 200) {
      Serial.println("⚠️ HTTP GET failed");
    }

  } else {
    Serial.println("❌ HTTPACTION Timeout");
  }

  modem.sendAT("+HTTPTERM");
  modem.waitResponse(2000);
}



// ----------------------------------------------------------------------
// SETUP
// ----------------------------------------------------------------------

void setup(){
  // Set console baud rate
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n-=========================================-\n");
  
  // Set LED OFF
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  // Power off/on
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(300);
  digitalWrite(PWR_PIN, LOW);
  delay(1000);

  // start 1wire
  sensors.begin();

  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);

  // Restart takes quite some time ...
  Serial.println("restart() modem ... ");
  if (!modem.restart()) {
    Serial.println("Failed to restart() modem, attempting to continue.");
  }
}

// ----------------------------------------------------------------------
// LOOP
// ----------------------------------------------------------------------

void loop(){
  // ... to skip it, call init() instead of restart()
  Serial.println("init() modem ... ");
  if (!modem.init()) {
    Serial.println("Failed to init() modem, attempting to continue.");
  }

  String name = modem.getModemName();
  delay(500);
  Serial.println("Modem Name ...: " + name);

  String modemInfo = modem.getModemInfo();
  delay(500);
  Serial.println("Modem Info ...: " + modemInfo);

  // Unlock your SIM card with a PIN if needed
  if (String(GSM_PIN).length() > 0 && modem.getSimStatus() != 3) {
    modem.simUnlock(GSM_PIN);
  }
  modem.sendAT("+CFUN=0 ");
  if (modem.waitResponse(10000L) != 1) {
    DBG(" +CFUN=0  false ");
  }
  delay(200);

  String res;
  // change network mode if needed
  res = modem.setNetworkMode(2); //     2 Automatic
  if (res != "1") {
    DBG("setNetworkMode  false ");
    return ;
  }
  delay(200);

  // change preferred network mode if needed
  res = modem.setPreferredMode(3); //  3 CAT-M and NB-IoT
  if (res != "1") {
    DBG("setPreferredMode  false ");
    return ;
  }
  delay(200);

  modem.sendAT("+CFUN=1 ");
  if (modem.waitResponse(10000L) != 1) {
    DBG(" +CFUN=1  false ");
  }
  delay(200);
  
  Serial.print("Waiting for network ... ");
  if (!modem.waitForNetwork()) {
    delay(10000);
    return;
  }

  if (modem.isNetworkConnected()) {
    Serial.println("Network connected");
  }
  
  Serial.println("Connecting to : " + String(apn));
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    delay(10000);
    return;
  }

  Serial.print("GPRS status .: ");
  if (modem.isGprsConnected()) {
    Serial.println("connected");
  } else {
    Serial.println("not connected");
  }

  // candidates for deletion in production environment
  String cop = modem.getOperator();
  Serial.println("Operator .....: " + cop);
  int csq = modem.getSignalQuality();
  Serial.println("Signal quality: " + String(csq));

  // starting GPS
  Serial.print("acquiring GPS : ");
  modem.sendAT("+SGPIO=0,4,1,1");
  if (modem.waitResponse(10000L) != 1) {
    DBG(" SGPIO=0,4,1,1 false ");
  }
  modem.enableGPS();
  float lat, lon, speed, alt, accuracy;
  int   vsat, usat, year, month, day, hour, min, sec;
  int gpsTries = 0;
  
  // tries up to 50x 5sec (>4min!) to acquire GPS signal
  while (gpsTries < 50) {
    if (modem.getGPS(&lat, &lon, &speed, &alt, &vsat, &usat, &accuracy,
                     &year, &month, &day, &hour, &min, &sec)) {
      break;
    }
    delay(5000);
    gpsTries++;
  }
  if (gpsTries == 50) {
    Serial.println("GPS timeout – no fix");
  } else {
    Serial.println("GPS position fixed");
  }
  modem.disableGPS();

  // get 1wire temperatures
  sensors.requestTemperatures(); 
  float tC1 = sensors.getTempCByIndex(0); // indoor
  delay(1000);
  float tC2 = sensors.getTempCByIndex(1); // short outdoor
  delay(1000);
  float tC3 = sensors.getTempCByIndex(2); // long outdoor

  // --------------------------------------------------------------------
  // FINALLY: send Telegram message with position and others
  // adjust order here or in PHP webhook as you like
  sendTelegramMessage(lat, lon, tC1, tC3, tC2);

  Serial.println("Shutdown .....: deep sleep till 08:00h UTC initiated.");

  modem.sendAT("+SGPIO=0,4,1,0");
  if (modem.waitResponse(10000L) != 1) {
    DBG(" SGPIO=0,4,1,0 false ");
  }

  modem.gprsDisconnect();

  modem.sendAT("+CPOWD=1");
  if (modem.waitResponse(10000L) != 1) {
    DBG("+CPOWD=1");
  }

  // go to sleep till 10h CEST (GPS/UTC: -2 = 8h)
  int time_till_ten = ((10 - 2) * 3600 - hour * 3600 - 
                       min * 60 - sec + 86400) % 86400;
  esp_sleep_enable_timer_wakeup((time_till_ten + CORR_FACTOR) *
                                uS_TO_S_FACTOR);
  delay(200);
  esp_deep_sleep_start();

  // Do nothing forevermore
  while (true) {
      modem.maintain();
}
  }
