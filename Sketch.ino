// Configurations
#include "./Configuration/Blynk.h"
#include "./Configuration/Wifi.h"

// Libraries
#include <BlynkSimpleEsp32.h>  // Part of Blynk by Volodymyr Shymanskyy
#include <WiFi.h>              // Part of WiFi Built-In by Arduino
#include <WiFiClient.h>        // Part of WiFi Built-In by Arduino
#include <math.h>

// GPIO pins
const unsigned short int microphoneAdcPin = 19;     // TODO: assign correct pin
const unsigned short int openerMosfetGatePin = 16;  // TODO: assign correct pin

// Limits
const unsigned int wifiHandlerThreadStackSize = 10000;
const unsigned int blynkHandlerThreadStackSize = 10000;
const unsigned int ringSensorThreadStackSize = 10000;

// Timeouts
const unsigned int wifiConnectionTimeout = 10000;
const unsigned int blynkConnectionTimeout = 10000;
const unsigned int blynkConnectionStabilizerTimeout = 5000;
const unsigned short cycleDelayInMilliSeconds = 100;

// Measured Times
const unsigned int cycleTimeInMs = 2000;          // time one cycle should take (door opens for x seconds after releasing the button)
const unsigned int openTimeInMs = 1000;           // duration, the button should be pressed and held for, then released after openTimeInMs passed
const unsigned int entranceBellDuration = 3000;   // Duration of the entrance bell is audible
const unsigned int frontDoorBellDuration = 3000;  // Duration of the front door bell is audible

// Task Handles
TaskHandle_t wifiConnectionHandlerThreadFunctionHandle;
TaskHandle_t blynkConnectionHandlerThreadFunctionHandle;
TaskHandle_t ringSensorThreadFunctionHandle;

// Global State
int isRinging = 0;       // 0 = false, 1 = true
int autoOpenDoorOnRing;  // 0 = false, 1 = true

// ----------------------------------------------------------------------------
// SETUP
// ----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(microphoneAdcPin, INPUT);
  pinMode(openerMosfetGatePin, OUTPUT);

  xTaskCreatePinnedToCore(wifiConnectionHandlerThreadFunction, "Wifi Connection Handling Thread", wifiHandlerThreadStackSize, NULL, 20, &wifiConnectionHandlerThreadFunctionHandle, 1);
  xTaskCreatePinnedToCore(blynkConnectionHandlerThreadFunction, "Blynk Connection Handling Thread", blynkHandlerThreadStackSize, NULL, 20, &blynkConnectionHandlerThreadFunctionHandle, 1);

  xTaskCreatePinnedToCore(ringSensorThreadFunction, "Ring Sensor Thread", ringSensorThreadStackSize, NULL, 20, &ringSensorThreadFunctionHandle, 1);
}

// ----------------------------------------------------------------------------
// MAIN LOOP
// ----------------------------------------------------------------------------

void loop() { Blynk.run(); }

// ----------------------------------------------------------------------------
// FUNCTIONS
// ----------------------------------------------------------------------------

// Blynk Functions

BLYNK_CONNECTED() {  // Update client pin states to server-states
  Blynk.syncAll();
}

BLYNK_WRITE(V1) {  // Open door for X seconds on ui button push
  int pinValue = param.asInt();
  if (pinValue == 1) {
    Serial.printf("Opening door now for 10000 ms");
    openDoorForGivenMs(10000);
    Blynk.virtualWrite(V1, 0);
  }
}

BLYNK_WRITE(V2) {  // open door, as long as the switch is on
  int pinValue = param.asInt();
  if (pinValue == 0) {
    digitalWrite(openerMosfetGatePin, LOW);
  } else {
    digitalWrite(openerMosfetGatePin, HIGH);
  }
}

BLYNK_WRITE(V3) {  // indicator button in ui to show current ringing-state
  isRinging = param.asInt();
  serial.printf("isRinging is now %d", isRinging);
}

BLYNK_WRITE(V4) {  // switch to enable / disable auto-open function
  autoOpenDoorOnRing = param.asInt();
  serial.printf("autoOpenDoorOnRing is now %d", autoOpenDoorOnRing);
}

// General functions

void openDoorForGivenMs(unsigned int timeToOpenInMs) {
  unsigned int timePassed = 0;

  while (timePassed <= timeToOpenInMs) {
    digitalWrite(openerMosfetGatePin, HIGH);
    delay(openTimeInMs);
    digitalWrite(openerMosfetGatePin, LOW);
    delay(cycleTimeInMs - openTimeInMs);
    timePassed += cycleTimeInMs;
  }
}

void ringSensorThreadFunction(void* param) {
  while (true) {
    if (digitalRead(microphoneAdcPin) == HIGH) {
      isRinging = true;
      Blynk.virtualWrite(V3, 1);
      if (autoOpenDoorOnRing == 1) {
        Blynk.digitalWrite(V1, 1);
      }
      delay(entranceBellDuration);
      isRinging = false;
      Blynk.virtualWrite(V3, 0);
    }
    delay(100);  // Cycle pause between each check
  }
}

int percentToValue(int percent, int maxValue) { return 0 <= percent <= 100 ? round((maxValue / 100) * percent) : 1023; }

// Connection Handler Functions

void WaitForWifi(uint cycleDelayInMilliSeconds) {
  while (WiFi.status() != WL_CONNECTED) {
    delay(cycleDelayInMilliSeconds);
  }
}

void WaitForBlynk(int cycleDelayInMilliSeconds) {
  while (!Blynk.connected()) {
    delay(cycleDelayInMilliSeconds);
  }
}

void wifiConnectionHandlerThreadFunction(void* params) {
  uint time;
  while (true) {
    if (!WiFi.isConnected()) {
      try {
        Serial.printf("Connecting to Wifi: %s\n", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PW);  // initial begin as workaround to some espressif library bug
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PW);
        WiFi.setHostname("Desklight (ESP32, Blynk)");
        time = 0;
        while (WiFi.status() != WL_CONNECTED) {
          if (time >= wifiConnectionTimeout || WiFi.isConnected()) break;
          delay(cycleDelayInMilliSeconds);
          time += cycleDelayInMilliSeconds;
        }
      } catch (const std::exception e) {
        Serial.printf("Error occured: %s\n", e.what());
      }
      if (WiFi.isConnected()) {
        Serial.printf("Connected to Wifi: %s\n", WIFI_SSID);
        wifiReconnectCounter = 0;
        flashLights(2, 50, 50);
      }
    }
    delay(1000);
    Serial.printf("Wifi Connection Handler Thread current stack size: %d , current Time: %d\n", wifiHandlerThreadStackSize - uxTaskGetStackHighWaterMark(NULL), xTaskGetTickCount());
  };
}

void blynkConnectionHandlerThreadFunction(void* params) {
  uint time;
  while (true) {
    if (!Blynk.connected()) {
      Serial.printf("Connecting to Blynk: %s\n", BLYNK_USE_LOCAL_SERVER == true ? BLYNK_SERVER : "Blynk Cloud Server");
      if (BLYNK_USE_LOCAL_SERVER)
        Blynk.config(BLYNK_AUTH, BLYNK_SERVER, BLYNK_PORT);
      else
        Blynk.config(BLYNK_AUTH);
      Blynk.connect();  // Connects using the chosen Blynk.config
      uint time = 0;
      while (!Blynk.connected()) {
        if (time >= blynkConnectionTimeout || Blynk.connected()) break;
        delay(cycleDelayInMilliSeconds);
        time += cycleDelayInMilliSeconds;
      }
      if (Blynk.connected()) {
        Serial.printf("Connected to Blynk: %s\n", BLYNK_USE_LOCAL_SERVER ? BLYNK_SERVER : "Blynk Cloud Server");
        delay(blynkConnectionStabilizerTimeout);
      }
    }
    delay(1000);
    Serial.printf("Blynk Connection Handler Thread current stack size: %d , current Time: %d\n", blynkHandlerThreadStackSize - uxTaskGetStackHighWaterMark(NULL), xTaskGetTickCount());
  }
}
