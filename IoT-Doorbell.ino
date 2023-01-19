// Configurations
#include "./Configuration/Blynk.h"
#include "./Configuration/Wifi.h"

// Libraries
#include <Arduino.h>
#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <math.h>

// GPIO pins
const ushort microphoneAdcPin = 33;
const ushort openerMosfetGatePin = 21;

// Limits
const uint wifiHandlerThreadStackSize = 5000;
const uint blynkHandlerThreadStackSize = 10000;
const uint ringSensorThreadStackSize = 5000;
const uint doorOpenerThreadStackSize = 5000;
const uint wifiReconnectCountLimit = 5;  // Wifi connection attempts. If count is reached, esp32 will reboot
const uint blynkReconnectCountLimit = 10;

// Timeouts
const uint wifiConnectionTimeout = 10000;
const uint blynkConnectionTimeout = 10000;
const uint blynkConnectionStabilizerTimeout = 5000;
const ushort cycleDelayInMilliSeconds = 100;

// Time constants
uint cycleTimeInMs = 2000;                // time one cycle should take (door opens for x seconds after releasing the button)
uint openTimeInMs = 1000;                 // duration, the button should be pressed and held for, then released after openTimeInMs passed
const uint entranceBellDuration = 3000;   // Duration of the entrance bell is audible
const uint frontDoorBellDuration = 3000;  // Duration of the front door bell is audible

// Task Handles
TaskHandle_t wifiConnectionHandlerThreadFunctionHandle;
TaskHandle_t blynkConnectionHandlerThreadFunctionHandle;
TaskHandle_t ringSensorThreadFunctionHandle;
TaskHandle_t doorOpenerThreadFunctionHandle;

// Global State
uint isRinging = 0;           // 0 = false, 1 = true
uint openDoor = 0;            // 0 = false, 1 = true
uint autoOpenDoorOnRing = 1;  // 0 = false, 1 = true

// Counters
uint wifiReconnectCounter = 0;
uint blynkReconnectCounter = 0;

void setup() {
  Serial.begin(115200);

  pinMode(microphoneAdcPin, INPUT);
  pinMode(openerMosfetGatePin, OUTPUT);

  xTaskCreatePinnedToCore(wifiConnectionHandlerThreadFunction, "Wifi Connection Handling Thread", wifiHandlerThreadStackSize, NULL, 20, &wifiConnectionHandlerThreadFunctionHandle, 1);
  xTaskCreatePinnedToCore(blynkConnectionHandlerThreadFunction, "Blynk Connection Handling Thread", blynkHandlerThreadStackSize, NULL, 20, &blynkConnectionHandlerThreadFunctionHandle, 1);

  xTaskCreatePinnedToCore(ringSensorThreadFunction, "Ring Sensor Thread", ringSensorThreadStackSize, NULL, 20, &ringSensorThreadFunctionHandle, 1);
  xTaskCreatePinnedToCore(doorOpenerThreadFunction, "Door Opener Thread", doorOpenerThreadStackSize, NULL, 20, &doorOpenerThreadFunctionHandle, 1);
}

void loop() { Blynk.run(); }

BLYNK_CONNECTED() {  // Update client pin states to server-states
  Blynk.syncAll();
}

BLYNK_WRITE(V1) {  // Open door for X seconds on ui button push
  int pinValue = param.asInt();
  Serial.printf("Virtual Pin V1 triggerd to: %d\n", pinValue);
  if (pinValue == 1) {
    openDoor = 1;
    delay(10000);  // open door for 10 seconds
    openDoor = 0;
  }
}

BLYNK_WRITE(V2) {  // open door, as long as the switch is on
  int pinValue = param.asInt();
  if (pinValue == 0) {
    openDoor = 0;
  } else {
    openDoor = 1;
  }
}

BLYNK_WRITE(V3) {  // indicator button in ui to show current ringing-state
  Serial.printf("isRinging is now %d\n", isRinging);
  if (isRinging == 0) {
    Blynk.virtualWrite(V3, 0);
  }
}

BLYNK_WRITE(V4) {  // switch to enable / disable auto-open function
  autoOpenDoorOnRing = param.asInt();
  Serial.printf("autoOpenDoorOnRing is now %d\n", autoOpenDoorOnRing);
}

BLYNK_WRITE(V5) {
  openTimeInMs = param.asInt();
  Serial.printf("Open Time is now %d ms\n", openTimeInMs);
}

BLYNK_WRITE(V6) {
  cycleTimeInMs = param.asInt();
  Serial.printf("Open Cycle Time is now %d ms\n", cycleTimeInMs);
}

void ringSensorThreadFunction(void* params) {
  while (true) {
    if (digitalRead(microphoneAdcPin) == LOW) {
      Blynk.notify("Doorbell rang.\n");
      Blynk.virtualWrite(V3, 1);
      if (autoOpenDoorOnRing == 1) {
        openDoor = 1;
        delay(9000);  // 9s
        openDoor = 0;
      }
      Blynk.virtualWrite(V3, 0);
    }
    delay(10);  // sleep time between each check, you know, to let the microcontroller relax a bit
  }
}

void doorOpenerThreadFunction(void* params) {
  while (true) {
    if (openDoor == 1) {
      while (openDoor == 1) {
        Serial.printf("Opening door...\n");
        digitalWrite(openerMosfetGatePin, HIGH);
        delay(openTimeInMs);  // duration of "opener button being pressed"
        Serial.printf("Releasing\n");
        digitalWrite(openerMosfetGatePin, LOW);
        delay(cycleTimeInMs - openTimeInMs);  // duration of "opener button release"
      }
      digitalWrite(openerMosfetGatePin, LOW);
      Serial.printf("Stopped opening door.\n");
    }
    delay(100);
  }
}

void WaitForWifiConnection(uint cycleDelayInMilliSeconds) {
  while (WiFi.status() != WL_CONNECTED) {
    delay(cycleDelayInMilliSeconds);
  }
}

void WaitForBlynkConnection(uint cycleDelayInMilliSeconds) {
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
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PW);
        WiFi.setHostname("Doorbell (ESP32, Blynk)");
        time = 0;
        while (!WiFi.isConnected()) {
          if (time >= wifiConnectionTimeout) {
            wifiReconnectCounter++;
            break;
          }
          delay(cycleDelayInMilliSeconds);
          time += cycleDelayInMilliSeconds;
        }
      } catch (const std::exception e) {
        Serial.printf("Error occured: %s\n", e.what());
      }
      if (WiFi.isConnected()) {
        Serial.printf("Connected to Wifi: %s\n", WIFI_SSID);
        wifiReconnectCounter = 0;
      }
      if (wifiReconnectCounter >= wifiReconnectCountLimit) {
        Serial.printf("Restarting esp, since wifi reconnect count reached limit of %d\n", wifiReconnectCountLimit);
        ESP.restart();
      }
    }
    delay(1000);
    // Serial.printf("Wifi Connection Handler Thread current stack size: %d , current Time: %d\n", wifiHandlerThreadStackSize - uxTaskGetStackHighWaterMark(NULL), xTaskGetTickCount());
  }
}

void blynkConnectionHandlerThreadFunction(void* params) {
  uint time;
  bool blynkConnectionSuccess;
  while (true) {
    if (!Blynk.connected()) {
      WaitForWifiConnection(100);
      Serial.printf("Connecting to Blynk: %s\n", BLYNK_USE_LOCAL_SERVER == true ? BLYNK_SERVER : "Blynk Cloud Server");
      if (BLYNK_USE_LOCAL_SERVER)
        Blynk.config(BLYNK_AUTH, BLYNK_SERVER, BLYNK_PORT);
      else
        Blynk.config(BLYNK_AUTH);
      Blynk.connect(blynkConnectionTimeout);  // Connects using the chosen Blynk.config
      blynkReconnectCounter++;
      time = 0;
      while (!Blynk.connected()) {
        if (time >= blynkConnectionTimeout || Blynk.connected()) break;
        delay(cycleDelayInMilliSeconds);
        time += cycleDelayInMilliSeconds;
      }
      if (Blynk.connected()) {
        Serial.printf("Connected to Blynk: %s\n", BLYNK_USE_LOCAL_SERVER ? BLYNK_SERVER : "Blynk Cloud Server");
        blynkReconnectCounter = 0;
        delay(blynkConnectionStabilizerTimeout);
      }
      if (blynkReconnectCounter >= blynkReconnectCountLimit) {
        Serial.printf("Restarting esp, since blynk reconnect count reached limit of %d\n", blynkReconnectCountLimit);
        ESP.restart();
      }
    }
    delay(1000);
    Serial.printf("Blynk Connection Handler Thread current stack size: %d , current Time: %d\n", blynkHandlerThreadStackSize - uxTaskGetStackHighWaterMark(NULL), xTaskGetTickCount());
  }
}
