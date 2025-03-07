// Compile with ESP32 Dev Module board setting
/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org/>
*/
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include "log.h"
#include "networkConfig.h"

#define PROJECT_INFO "C0de Hart pushbuttons :: V1.0 - 20250303 :: Steve Van Hoyweghen"
#define RED_BUTTON_INPUT 4
#define RED_BUTTON_LED_OUTPUT 16
#define GREEN_BUTTON_INPUT 15
#define GREEN_BUTTON_LED_OUTPUT 17

#define BUTTON_PRESSED_RED 0
#define BUTTON_PRESSED_GREEN 1

/*
ESP32 WROOM 30 Pin version is used. https://www.studiopieters.nl/esp32-pinout/

* RED_BUTTON_INPUT and GREEN_BUTTON_INPUT schematic

          10 KOhm   __ Normal open push button
           ____    _||_
+3.3V 0---|____|-+-O  O---| GND
                 |
                 O
            RED_BUTTON_INPUT
                 or
            GREEN_BUTTON_INPUT



* RED_BUTTON_LED_OUTPUT and GREEN_BUTTON_LED_OUTPUT schematic

          470 Ohm  Led
           ____ 
ESP32 0---|____|---|>|----| GND

  RED_BUTTON_LED_OUTPUT
            or
  GREEN_BUTTON_LED_OUTPUT

*/


#define ON_BOARD_BLUE_LED_OUTPUT 2

#define TIMER_ALARM_VALUE 100000  // micro seconds
#define REPEAT_ALARM true
#define INFINITE_COUNT 0

#define MINIMAL_OSC_BUTTON_PAUSE 10  // 1 second between 2 consecutive button messages when users keep pushing the same button
#define OSC_KEEP_ALIVE_TIMEOUT 150   // send every OSC_KEEP_ALIVE_TIMEOUT * 100 ms a /keep_alive message
#define ON_BOARD_LED_TIMEOUT 10

#define BUTTON_LED_IDLE_TIMEOUT 2
#define BUTTON_LED_PRESSED_TIMEOUT 15
#define BUTTON_LED_DIMMED_TIMEOUT 5

#define WDT_TIMEOUT 10000                  // in ms
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1  // if 1 core doesn't work, try with 2


// Globals for timer interrupt function
hw_timer_t* timer = NULL;
volatile bool wdtFlag = false;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
int32_t keep_alive_counter;

void ARDUINO_ISR_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  wdtFlag = true;
  portEXIT_CRITICAL_ISR(&timerMux);
  //Semaphore checked in loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

WiFiUDP udp;

bool debounce(uint8_t input_pin) {
  // Switch debouncing, read 16 times looking for 16 consecutive highs or lows.
  const uint8_t debounceSensitivity = 16;        // Sensitivity in range 1-32
  const uint32_t debounceDontCare = 0xffff0000;  //Don't care mask
  uint32_t pinStatus;

  do {
    pinStatus = 0xffffffff;
    for (uint8_t i = 1; i <= debounceSensitivity; i++)
      pinStatus = (pinStatus << 1) | digitalRead(input_pin);
  } while ((pinStatus != debounceDontCare) && (pinStatus != 0xffffffff));
  return !bool(pinStatus & 0x00000001);
}

void handleOSCMessage(OSCMessage& msg) {
  char s[100];

  if (msg.fullMatch("/command")) {
    msg.getString(0, s);
    LOG(LOG_INFO, "%s, %c", msg.getAddress(), s[0]);
  } else if (msg.fullMatch("/keep_alive"))
    LOG(LOG_INFO, "%s, %i", msg.getAddress(), msg.getInt(0));
}

/*
void sendOscButton(char* payload) {
  const char* address = "/button";

  LOG(LOG_INFO, "%s: %s", address, payload);
  OSCMessage msg(address);
  msg.add(payload);
  udp.beginPacket(remoteIp, remotePort);
  msg.send(udp);
  udp.endPacket();
  msg.empty();
}
*/

void sendOscButton(int32_t payload) {
  const char* address = "/button";

  LOG(LOG_INFO, "%s: %i", address, payload);
  OSCMessage msg(address);
  msg.add(payload);
  udp.beginPacket(remoteIp, remotePort);
  msg.send(udp);
  udp.endPacket();
  msg.empty();
}

void sendOSCMessageKeepAlive() {
  const char* address = "/keep_alive";
  static int32_t keep_alive_counter = 0;

  LOG(LOG_INFO, "%s: %i", address, keep_alive_counter);
  OSCMessage msg(address);
  msg.add(keep_alive_counter);  // Add an integer argument
  udp.beginPacket(remoteIp, remotePort);
  msg.send(udp);  // Send the OSC message
  udp.endPacket();
  msg.empty();  // Free the message buffer
  keep_alive_counter = (keep_alive_counter + 1) % 10000;
}

void setup() {
  //static esp_now_peer_info_t peer_info;
  static esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,  // Bitmask of all cores
    .trigger_panic = true,
  };

  Serial.begin(115200);

  pinMode(RED_BUTTON_INPUT, INPUT);
  pinMode(GREEN_BUTTON_INPUT, INPUT);

  pinMode(RED_BUTTON_LED_OUTPUT, OUTPUT);
  pinMode(GREEN_BUTTON_LED_OUTPUT, OUTPUT);
  analogWrite(RED_BUTTON_LED_OUTPUT, 0);
  analogWrite(GREEN_BUTTON_LED_OUTPUT, 0);

  // Init blue status led
  pinMode(ON_BOARD_BLUE_LED_OUTPUT, OUTPUT);
  digitalWrite(ON_BOARD_BLUE_LED_OUTPUT, LOW);

  WiFi.begin(ssid, password);

  // Wait for the connection to establish
  uint8_t temp = 0;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(ON_BOARD_BLUE_LED_OUTPUT, !digitalRead(ON_BOARD_BLUE_LED_OUTPUT));  // Fast blink to indicate serial issue
    delay(1000);
    // Use button leds to indicate we are waiting for a wifi connection
    temp = (temp == 0 ? UINT8_MAX : 0);
    analogWrite(RED_BUTTON_LED_OUTPUT, temp);
    analogWrite(GREEN_BUTTON_LED_OUTPUT, UINT8_MAX - temp);
    LOG(LOG_INFO, "Connecting to WiFi ...");
  }
  LOG(LOG_INFO, "%s", PROJECT_INFO);
  IPAddress ip = WiFi.localIP();
  LOG(LOG_INFO, "WiFi connected, IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

  // Start listening for UDP packets
  udp.begin(localPort);

  // Configure timer interrupt
  timerSemaphore = xSemaphoreCreateBinary();
  // Set timer frequency to 1Mhz
  timer = timerBegin(1000000);
  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer);
  // Call onTimer function
  timerAlarm(timer, TIMER_ALARM_VALUE, REPEAT_ALARM, INFINITE_COUNT);

  // Configure Watchdog task
  esp_task_wdt_deinit();            // wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&twdt_config);  // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);           // add current thread to WDT watch
}

void loop() {
  static uint8_t oscTimer = OSC_KEEP_ALIVE_TIMEOUT;
  static uint8_t onBoardLedTimer = ON_BOARD_LED_TIMEOUT;
  static uint8_t buttonLedTimer = 0;


  static uint16_t ledValue = 0;
  static uint8_t dutyCycleIndex = 0;
  //const uint8_t dutyCycleInRest[] = { 0, 32, 64, 96, 128, 150, 182, 214, UINT8_MAX, 214, 182, 150, 128, 96, 64, 32 };
  //const uint8_t dutyCycleInRest[] = { 0, 17, 17 * 2, 17 * 4, 17 * 7, 17 * 11, UINT8_MAX, 17 * 15, 17 * 11, 17 * 7, 17 * 4, 17 * 2, 17, 0 };  // UINT8_MAX = 255 = 3*5*17
  const uint8_t dutyCycleStarted[] = { 0, UINT8_MAX };
  enum { stateIdle,
         stateGreenButtonPressedStart,
         stateGreenButtonPressedStop,
         stateRedButtonPressedStart,
         stateRedButtonPressedStop,
         stateLedsDimmed };
  static uint8_t state = stateIdle;
  static uint8_t previousState = UINT8_MAX;

  // Check for incoming data
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // Allocate buffer for incoming packet
    uint8_t incomingPacket[packetSize];
    udp.read(incomingPacket, packetSize);

    // Parse the incoming OSC message
    OSCMessage msg;
    msg.fill(incomingPacket, packetSize);

    if (msg.hasError())
      LOG(LOG_ERROR, "%s", msg.getError());
    else
      // Handle the OSC message
      handleOSCMessage(msg);
  }

  // Check if timer event occured
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
    portENTER_CRITICAL(&timerMux);
    bool wdtFlagCopy = wdtFlag;
    wdtFlag = false;
    portEXIT_CRITICAL(&timerMux);
    // reset WDT every 1 second
    if (wdtFlagCopy)
      esp_task_wdt_reset();

    previousState = state;
    switch (state) {
      case stateIdle:
        if (debounce(GREEN_BUTTON_INPUT))
          state = stateGreenButtonPressedStart;
        else if (debounce(RED_BUTTON_INPUT))
          state = stateRedButtonPressedStart;
        else if (buttonLedTimer-- == 0) {
          buttonLedTimer = BUTTON_LED_IDLE_TIMEOUT;
          uint8_t ledIntensity = (ledValue > UINT8_MAX ? UINT8_MAX - (ledValue - UINT8_MAX) : ledValue);  // 0..255..0
          ledValue = (ledValue + 17) % (UINT8_MAX * 2 + 1);                                               // 0..510
          analogWrite(RED_BUTTON_LED_OUTPUT, ledIntensity);
          analogWrite(GREEN_BUTTON_LED_OUTPUT, UINT8_MAX - ledIntensity);
        }
        break;

      case stateGreenButtonPressedStart:
        buttonLedTimer = BUTTON_LED_PRESSED_TIMEOUT;
        analogWrite(RED_BUTTON_LED_OUTPUT, 0);
        sendOscButton(BUTTON_PRESSED_GREEN);
        dutyCycleIndex = 0;
        state = stateGreenButtonPressedStop;
        break;

      case stateGreenButtonPressedStop:
        if (buttonLedTimer-- == 0) {
          buttonLedTimer = BUTTON_LED_DIMMED_TIMEOUT;
          analogWrite(GREEN_BUTTON_LED_OUTPUT, 0);
          ledValue = 0;  // Start with illuminated green and dimmed red led next stateIdle
          state = stateLedsDimmed;
        } else {
          dutyCycleIndex = (dutyCycleIndex + 1) % sizeof(dutyCycleStarted);
          analogWrite(GREEN_BUTTON_LED_OUTPUT, dutyCycleStarted[dutyCycleIndex]);
        }
        break;

      case stateRedButtonPressedStart:
        buttonLedTimer = BUTTON_LED_PRESSED_TIMEOUT;
        analogWrite(GREEN_BUTTON_LED_OUTPUT, 0);
        sendOscButton(BUTTON_PRESSED_RED);
        dutyCycleIndex = 0;
        state = stateRedButtonPressedStop;
        break;

      case stateRedButtonPressedStop:
        if (buttonLedTimer-- == 0) {
          buttonLedTimer = BUTTON_LED_DIMMED_TIMEOUT;
          analogWrite(RED_BUTTON_LED_OUTPUT, 0);
          ledValue = UINT8_MAX + 1;  // Start with illuminated red and dimmed green led next stateIdle
          state = stateLedsDimmed;
        } else {
          dutyCycleIndex = (dutyCycleIndex + 1) % sizeof(dutyCycleStarted);
          analogWrite(RED_BUTTON_LED_OUTPUT, dutyCycleStarted[dutyCycleIndex]);
        }
        break;

      case stateLedsDimmed:
        if (buttonLedTimer == 0) {
          dutyCycleIndex = 0;
          buttonLedTimer = 0;
          state = stateIdle;
        } else
          buttonLedTimer--;
        break;

      default:
        state = stateIdle;
        LOG(LOG_ERROR, "Unknown state: %i", state);
        break;
    }

    if (state != previousState)
      LOG(LOG_INFO, "State: %i -> %i", previousState, state);

    // Heartbeat led toggle
    if (onBoardLedTimer-- == 0) {
      onBoardLedTimer = ON_BOARD_LED_TIMEOUT;
      digitalWrite(ON_BOARD_BLUE_LED_OUTPUT, !digitalRead(ON_BOARD_BLUE_LED_OUTPUT));
    }

    if (oscTimer-- == 0) {
      oscTimer = OSC_KEEP_ALIVE_TIMEOUT;
      sendOSCMessageKeepAlive();
    }
  }
}
