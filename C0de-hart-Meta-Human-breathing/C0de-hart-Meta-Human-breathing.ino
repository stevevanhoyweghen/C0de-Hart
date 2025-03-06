// !!! Compile with ESP32 Dev Module to prevent onboard led blinking https://esp32.com/viewtopic.php?t=41243
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

#define PROJECT_INFO "C0de Hart Meta Human breathing :: V1.0 - 20250303 :: Steve Van Hoyweghen"
#define WDT_TIMEOUT 10000                  // in ms
#define CONFIG_FREERTOS_NUMBER_OF_CORES 1  // if 1 core doesn't work, try with 2

#define LED_OUTPUT 2

#define NUMBER_OF_BAGS 8
#define MAX_FAN_STEPS 30
#define OSC_KEEP_ALIVE_TIMEOUT 15  // Send every OSC_KEEP_ALIVE_TIMEOUT seconds an OSC keep alive message.

#define duration_t int8_t
#define percentage_t int8_t
#define sequence_step_t uint8_t

struct fanOutput_t {
  uint8_t inflatePWM;
  uint8_t deflatePWM;
};

const fanOutput_t fanOutput[NUMBER_OF_BAGS] = {
  32, 33,  // 1
  26, 27,  // 2
  23, 25,  // 3
  18, 19,  // 4
  16, 17,  // 5
  14, 15,  // 6
  12, 13,  // 7
  4, 5     // 8
};

enum Action {
  SEQUENCE_ALL,
  SEQUENCE,
  RANDOM,
  ALL_BAGS,
  ONE_BAG,
  INTENSITY,
  PACE,
  INFO,
  ACTION_UNDEFINED
};

enum Command {
  INFLATE,
  DEFLATE,
  STOP,
  LOOP
};

struct action_t {
  Action actionId;
  char serialId;
  char description[13];
};

struct sequenceStep_t {
  enum Command command;
  percentage_t percentage;
  duration_t duration;
};
typedef struct sequenceStep_t sequenceSteps_t[NUMBER_OF_BAGS][MAX_FAN_STEPS];

struct sequenceState_t {
  duration_t counter;
  sequence_step_t sequenceStep;
};
typedef struct sequenceState_t sequenceStates_t[NUMBER_OF_BAGS];

struct sequenceSnippet_t {
  int8_t percentage;
  duration_t duration;
};

struct sequence_t {
  const sequenceSnippet_t* sequenceSnippet;
  uint8_t offset;
};

const struct action_t actionLookup[] = {
  { SEQUENCE_ALL, 'x', "Sequence all" },
  { SEQUENCE, 's', "Sequence" },
  { RANDOM, 'r', "Random" },
  { ALL_BAGS, 'a', "All bags" },
  { ONE_BAG, 'o', "One bag" },
  { INTENSITY, 'i', "Intensity" },
  { PACE, 'p', "Pace" },
  { INFO, '?', "Info" },
  { ACTION_UNDEFINED, 'x', "Undefined" }
};

const sequenceSnippet_t sequenceSnippet_stop[] = {
  { STOP, -1 }
};

const sequenceSnippet_t sequenceSnippet_breath[] = {
  { 95, 1 }, { -55, 1 }, { LOOP, -1 }
};

const sequenceSnippet_t sequenceSnippet_left_arm[] = {
  { 100, 2 }, { -100, 2 }, { LOOP, -1 }
};

const sequenceSnippet_t sequenceSnippet_right_arm[] = {
  { -100, 1 }, { 100, 3 }, { LOOP, -1 }
};

const sequenceSnippet_t sequenceSnippet_head[] = {
  { 80, 1 }, { -60, 1 }, { LOOP, -1 }
};

const sequenceSnippet_t sequenceSnippet_deflate[] = {
  { -80, 10 }, { -10, 1 }, { STOP, -1 }
};

const sequenceSnippet_t sequenceSnippet_inflate[] = {
  { 70, 10 }, { 50, 1 }, { STOP, -1 }
};

struct sequence_t sequence0[NUMBER_OF_BAGS] = {
  { sequenceSnippet_breath, 0 }, // Lungs
  { sequenceSnippet_left_arm, 0 }, // Left arm
  { sequenceSnippet_right_arm, 0 }, // Right arm
  { sequenceSnippet_head, 0 }, // Head
  { sequenceSnippet_stop, 0 }, // Unallocated
  { sequenceSnippet_stop, 0 }, // Unallocated
  { sequenceSnippet_stop, 0 }, // Unallocated
  { sequenceSnippet_stop, 0 }  // Unallocated
};

// Wave
const sequenceSnippet_t sequenceSnippet1[] = {
  //  { 50, 3 }, { 60, 1 }, { 80, 1 }, { -60, 2 }, { 80, 1 }, { -60, 1 }, { -50, 2 }, { LOOP, -1 }
  { 50, 1 },
  { 60, 1 },
  { 90, 1 },
  { 100, 2 },
  { -80, 1 },
  { -60, 1 },
  { -50, 1 },
  { -100, 1 },
  { -30, 1 },
  { LOOP, -1 }
};

const struct sequence_t sequence1[NUMBER_OF_BAGS] = {
  { sequenceSnippet1, 0 },
  { sequenceSnippet1, 2 },
  { sequenceSnippet1, 4 },
  { sequenceSnippet1, 6 },
  { sequenceSnippet1, 6 },
  { sequenceSnippet1, 4 },
  { sequenceSnippet1, 2 },
  { sequenceSnippet1, 0 }
};

// In sync
const sequenceSnippet_t sequenceSnippet2[] = {
  { 100, 2 }, { 80, 1 }, { 60, 2 }, { 50, 2 }, { 80, 1 }, { 0, 1 }, { -90, 2 }, { -80, 1 }, { -60, 2 }, { -50, 1 }, { -30, 1 }, { LOOP, -1 }
};
const struct sequence_t sequence2[NUMBER_OF_BAGS] = {
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 },
  { sequenceSnippet2, 0 }
};

// Slooow
const sequenceSnippet_t sequenceSnippet3[] = {
  //{ 44, 10 }, { 100, 3 }, { -55, 2 }, { 0, 2 }, { +55, 2 }, { -66, 8 }, { LOOP, -1 }
  { 50, 10 },
  { -30, 7 },
  { 50, 10 },
  { -30, 7 },
  { 50, 10 },
  { -60, 2 },
  { 50, 2 },
  { -60, 2 },
  { 50, 2 },
  { -60, 2 },
  { 50, 2 },
  { -50, 8 },
  { LOOP, -1 }
};

const struct sequence_t sequence3[NUMBER_OF_BAGS] = {
  { sequenceSnippet3, 0 },
  { sequenceSnippet3, 2 },
  { sequenceSnippet3, 6 },
  { sequenceSnippet3, 8 },
  { sequenceSnippet3, 9 },
  { sequenceSnippet3, 7 },
  { sequenceSnippet3, 5 },
  { sequenceSnippet3, 3 }
};

// Erratic
const sequenceSnippet_t sequenceSnippet4[] = {
  { 100, 5 }, { -100, 5 }, { LOOP, -1 }
};
const struct sequence_t sequence4[NUMBER_OF_BAGS] = {
  { sequenceSnippet4, 0 },
  { sequenceSnippet4, 1 },
  { sequenceSnippet4, 0 },
  { sequenceSnippet4, 1 },
  { sequenceSnippet4, 1 },
  { sequenceSnippet4, 0 },
  { sequenceSnippet4, 1 },
  { sequenceSnippet4, 0 }
};

// Only for testing via serial

const sequenceSnippet_t sequenceSnippetDeflate[] = {
  { -80, 10 }, { -10, 1 }, { STOP, -1 }
};
const struct sequence_t sequenceDeflate[NUMBER_OF_BAGS] = {
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 },
  { sequenceSnippetDeflate, 0 }
};

const sequenceSnippet_t sequenceSnippetInflate[] = {
  { 70, 10 }, { 50, 1 }, { STOP, -1 }
};
const struct sequence_t sequenceInflate[NUMBER_OF_BAGS] = {
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 },
  { sequenceSnippetInflate, 0 }
};

const sequenceSnippet_t sequenceSnippetTest[] = {
  { 75, 10 }, { -60, 10 }, { -60, 10 }, { -60, 10 }, { -60, 10 }, { -60, 10 }, { -60, 10 }, { -60, 10 }, { LOOP, -1 }
};
const struct sequence_t sequenceTest[NUMBER_OF_BAGS] = {
  { sequenceSnippetTest, 0 },
  { sequenceSnippetTest, 7 },
  { sequenceSnippetTest, 6 },
  { sequenceSnippetTest, 5 },
  { sequenceSnippetTest, 4 },
  { sequenceSnippetTest, 3 },
  { sequenceSnippetTest, 2 },
  { sequenceSnippetTest, 1 }
};

const char projectInfo[] = PROJECT_INFO;
const char OscAddressCommandReceived[] = "/command_rcv";
const char OscAddressKeepAlive[] = "/keep_alive";

// Global to monitor esp-now activity remote
static uint8_t oscTimer;

// Globals for timer interrupt function
hw_timer_t* timer = NULL;
volatile bool wdtFlag = false;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
int32_t keep_alive_counter;

WiFiUDP udp;

void ARDUINO_ISR_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  wdtFlag = true;
  portEXIT_CRITICAL_ISR(&timerMux);
  //Semaphore checked in loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

void setupPwmOutput(uint8_t pwmOut) {
  pinMode(pwmOut, OUTPUT_OPEN_DRAIN);
  analogWriteResolution(pwmOut, 8);     // 8 bit
  analogWriteFrequency(pwmOut, 25000);  // Noctua PWM specifications white paper, Target frequency: 25kHz, acceptable range 21kHz to 28kHz
}

void setFanSpeedPercent(uint8_t pwm_out, int percent) {
  int value = ((100 - percent) / 100.0) * 255;  // BS170 inverts signal -> 100 - percent
  analogWrite(pwm_out, value);
}

uint8_t paceTranslate(int8_t duration, int8_t pace) {
  if (pace < 0 || pace > 100) {
    LOG(LOG_WARNING, "pace should be 0..100: %d", pace);
    pace = (pace < 0 ? 0 : 100);
  }
  int result = duration * (1 + (100 - pace) / 25);  // duration * (1..5)
  // limit to 20 seconds
  if (result > 20)
    result = 20;
  return (uint8_t)result;
}

void executeFanSequence(sequenceStates_t& sequenceStates,
                        sequenceSteps_t& sequenceSteps,
                        int8_t intensity, int8_t pace) {
  enum Command command;
  sequence_step_t sequenceStep;
  bool inflateFlag;

  for (int i = 0; i < NUMBER_OF_BAGS; i++) {
    sequenceStep = sequenceStates[i].sequenceStep;
    command = sequenceSteps[i][sequenceStep].command;

    if (command == STOP)
      continue;

    if (command == LOOP) {
      sequenceStates[i].sequenceStep = 0;
      sequenceStates[i].counter = 0;
      continue;
    }

    if (sequenceStates[i].counter == 0) {
      sequenceStates[i].counter++;
      // Was ...
      //percentage_t percentage = constrain((int16_t)sequenceSteps[i][sequenceStep].percentage + (int16_t)intensity, 0, 100);
      //percentage = constrain(percentage + intensity, 0, 100);
      // ...

      // https://math.stackexchange.com/questions/158487/function-that-magnifies-small-changes-and-compresses-large-changes
      // f(percentage) = ((intensity + 1) * percentage) / (intensity * percentage + 1)
      double x = ((double)(sequenceSteps[i][sequenceStep].percentage) * (double)(intensity)) / 10000.0;
      double n = (100.0 - (double)intensity) / 100.0;
      x = ((n + 1.0) * x) / (n * x + 1.0);
      percentage_t percentage = (uint8_t)(x * 100.0);
      inflateFlag = (command == INFLATE);
      setFanSpeedPercent(fanOutput[i].deflatePWM, (inflateFlag ? 0 : abs(percentage)));
      setFanSpeedPercent(fanOutput[i].inflatePWM, (inflateFlag ? percentage : 0));
      LOG(LOG_INFO, "%d %c %d%% %d (%d %d)",
          i, (inflateFlag ? '+' : '-'),
          percentage,
          paceTranslate(sequenceSteps[i][sequenceStep].duration, pace),
          sequenceSteps[i][sequenceStep].duration,
          pace);
    } else if (sequenceStates[i].counter < paceTranslate(sequenceSteps[i][sequenceStep].duration, pace)) {
      sequenceStates[i].counter++;
    } else {
      sequenceStates[i].counter = 0;
      sequenceStates[i].sequenceStep = (sequenceStates[i].sequenceStep + 1) % MAX_FAN_STEPS;
    }
  }
}

void allBags(int8_t constant) {
  bool inflateFlag;
  LOG(LOG_INFO, "%d%%", constant);
  for (int i = 0; i < NUMBER_OF_BAGS; i++) {
    inflateFlag = (constant >= 0);
    setFanSpeedPercent(fanOutput[i].deflatePWM, (inflateFlag ? 0 : abs(constant)));
    setFanSpeedPercent(fanOutput[i].inflatePWM, (inflateFlag ? constant : 0));
  }
}

void oneBag(u_int bag, int8_t constant) {
  bool inflateFlag;

  LOG(LOG_INFO, "%d = %d%", bag, constant);
  inflateFlag = (constant >= 0);
  setFanSpeedPercent(fanOutput[bag].deflatePWM, (inflateFlag ? 0 : abs(constant)));
  setFanSpeedPercent(fanOutput[bag].inflatePWM, (inflateFlag ? constant : 0));
}

void sequenceFans(sequenceStates_t& sequenceStates, sequenceSteps_t& sequenceSteps, const sequence_t* sequence) {
  for (int i = 0; i < NUMBER_OF_BAGS; i++) {
    sequenceStates[i].sequenceStep = 0;
    sequenceStates[i].counter = 0;

    const sequenceSnippet_t* sequenceSnippet = sequence[i].sequenceSnippet;
    uint8_t offset = sequence[i].offset;
    uint8_t length = 0;
    while (sequenceSnippet[length].duration >= 0)  // last duration is -1 to indicate loop or stop
      length++;
    Command finalCommand = (Command)sequenceSnippet[length].percentage;  //!!!
    for (int j = 0; j < length; j++) {
      uint8_t index = (j + offset) % length;
      int8_t percentage = sequenceSnippet[index].percentage;
      int8_t duration = sequenceSnippet[index].duration;

      sequenceSteps[i][j].command = (percentage < 0 ? DEFLATE : INFLATE);
      sequenceSteps[i][j].percentage = abs(percentage);
      sequenceSteps[i][j].duration = duration;
    }
    sequenceSteps[i][length].command = finalCommand;
    sequenceSteps[i][length].percentage = 0;
    sequenceSteps[i][length].duration = 0;
  }
}

void randomFans(sequenceStates_t& sequenceStates, sequenceSteps_t& sequenceSteps) {
  uint32_t r;
  int i, j;

  for (i = 0; i < NUMBER_OF_BAGS; i++) {
    sequenceStates[i].sequenceStep = 0;
    sequenceStates[i].counter = 0;
    for (j = 0; j < MAX_FAN_STEPS - 1; j++) {
      r = esp_random();
      sequenceSteps[i][j].command = (r % 2 ? DEFLATE : INFLATE);
      sequenceSteps[i][j].percentage = (((r % 66) + 35));  // 35..100 * 0..100
      sequenceSteps[i][j].duration = ((r % 3) + 1);        // 1..3
    }
    sequenceSteps[i][j].command = LOOP;
    sequenceSteps[i][j].percentage = 0;
    sequenceSteps[i][j].duration = 0;
  }
}

void userHelpAction(uint8_t intensity, int8_t pace, int8_t constant) {
  LOG(LOG_INFO, "Commands ...");
  int i;
  for (i = 0; i < ((sizeof(actionLookup) / sizeof(actionLookup[0])) - 1); i++)
    LOG(LOG_INFO, "%c: %s", actionLookup[i].serialId, actionLookup[i].description);
  LOG(LOG_INFO, "Intensity: %d, Pace: %d, Constant: %d", intensity, pace, constant);
}

void setup() {
  //static esp_now_peer_info_t peer_info;
  static esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,  // Bitmask of all cores
    .trigger_panic = true,
  };

  Serial.begin(115200);

  // Init blue status led
  pinMode(LED_OUTPUT, OUTPUT);
  digitalWrite(LED_OUTPUT, LOW);

  Serial.begin(115200);
  while (!Serial && !Serial.available()) {
    digitalWrite(LED_OUTPUT, !digitalRead(LED_OUTPUT));  // Fast blink to indicate serial issue
    delay(100);
  }

  // Init all PWM fan outputs
  for (int i = 0; i < NUMBER_OF_BAGS; i++) {
    setupPwmOutput(fanOutput[i].inflatePWM);
    setupPwmOutput(fanOutput[i].deflatePWM);
  }

  // Init wifi
  WiFi.begin(ssid, password);
  // Wait for the connection to establish
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi ...");
  }
  Serial.print("Connected to WiFi, IP address: ");
  Serial.println(WiFi.localIP());

  // Start listening for UDP packets
  udp.begin(localPort);

  // Init timeout esp-now communication with remote
  oscTimer = OSC_KEEP_ALIVE_TIMEOUT;
  keep_alive_counter = 0;

  // Configure timer interrupt
  timerSemaphore = xSemaphoreCreateBinary();
  // Set timer frequency to 1Mhz
  timer = timerBegin(1000000);
  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer);
  // Call onTimer function
  timerAlarm(timer,
             1000000,  // every second, value in microseconds
             true,     // Repeat the alarm
             0         //unlimited count
  );

  // Configure Watchdog task
  esp_task_wdt_deinit();            // wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&twdt_config);  // enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);           // add current thread to WDT watch
  LOG(LOG_INFO, "%s", projectInfo);
}

void sendOSCMessageCommandReceived(char* payload) {
  LOG(LOG_INFO, "%s: %s", OscAddressCommandReceived, payload);
  OSCMessage msg(OscAddressCommandReceived);
  msg.add(payload);
  udp.beginPacket(remoteIp, remotePort);
  msg.send(udp);
  udp.endPacket();
  msg.empty();
}

void sendOSCMessageKeepAlive(uint32_t payload) {
  LOG(LOG_INFO, "%s: %u", OscAddressKeepAlive, payload);
  OSCMessage msg(OscAddressKeepAlive);
  msg.add((int32_t)payload);
  udp.beginPacket(remoteIp, remotePort);
  msg.send(udp);
  udp.endPacket();
  msg.empty();
}

void loop() {
  enum MetaHumanBags {
    LUNGS,
    ARM_LEFT,
    ARM_RIGHT,
    HEAD,
    BAG4,
    BAG5,
    BAG6,
    BAG7
  };

  enum SequenceAction {
    SEQUENCE_0,
    SEQUENCE_1,
    SEQUENCE_2,
    SEQUENCE_3,
    SEQUENCE_4,
    DEFLATE_ALL,
    INFLATE_ALL,
    TEST,
    SEQUENCE_UNDEFINED
  };

  struct sequenceAction_t {
    SequenceAction sequenceActionId;
    char serialId;
    const sequence_t* pointer;
    char description[12];
  };

  const struct sequenceAction_t sequenceLookup[] = {
    { SEQUENCE_0, '0', sequence0, "Sequence 0" },
    { SEQUENCE_1, '1', sequence1, "Sequence 1" },
    { SEQUENCE_2, '2', sequence2, "Sequence 2" },
    { SEQUENCE_3, '3', sequence3, "Sequence 3" },
    { SEQUENCE_4, '4', sequence4, "Sequence 4" },
    { DEFLATE_ALL, 'd', sequenceDeflate, "Deflate all" },
    { INFLATE_ALL, 'i', sequenceInflate, "Inflate all" },
    { TEST, 't', sequenceTest, "Test" },
    { SEQUENCE_UNDEFINED, 'x', 0, "Undefined" }
  };

  struct state_t {
    Action action;
    Action previousAction;
    int8_t value1;
    int8_t previousValue1;
    int8_t value2;
    int8_t previousValue2;
  };
  static state_t state = { SEQUENCE, SEQUENCE, SEQUENCE_0, SEQUENCE_UNDEFINED, 0, 0 };

  static SequenceAction sequenceAction = SEQUENCE_UNDEFINED;
  static int8_t intensity = 100;
  static int8_t pace = 0;
  static int8_t constant = 0;
  static sequenceSteps_t sequenceSteps;
  static sequenceStates_t sequenceStates;
  static uint8_t randomLoopCount = 0;
  static uint8_t sequenceAllLoopCount = 0;
  static uint8_t sequenceAllSelect = 0;

  // Parse serial input, mainly for testing
  if (Serial.available() > 0) {
    String s = Serial.readString();
    s.replace(" ", "");
    s.toLowerCase();
    Action action = ACTION_UNDEFINED;
    int indexAction = 0;
    while (indexAction < sizeof(actionLookup) / sizeof(actionLookup[0])) {
      if (s.charAt(0) == actionLookup[indexAction].serialId) {
        action = actionLookup[indexAction].actionId;
        break;
      }
      indexAction++;
    }

    if (action == INFO || action == ACTION_UNDEFINED)
      userHelpAction(intensity, pace, constant);
    else {
      state.action = action;
      LOG(LOG_INFO, "%s", actionLookup[indexAction].description);

      int i;
      switch (state.action) {
        case SEQUENCE:
          s.remove(0, 1);
          sequenceAction = SEQUENCE_UNDEFINED;

          for (i = 0; i < (sizeof(sequenceLookup) / sizeof(*sequenceLookup)); i++) {
            if (s.charAt(0) == sequenceLookup[i].serialId) {
              sequenceAction = sequenceLookup[i].sequenceActionId;
              break;
            }
          }

          if (sequenceAction == SEQUENCE_UNDEFINED) {
            // Show valid sequences
            LOG(LOG_INFO, "%s", sequenceLookup[i].description);
            for (i = 0; i < (sizeof(sequenceLookup) / sizeof(*sequenceLookup) - 1); i++)
              LOG(LOG_INFO, "%c: %s", sequenceLookup[i].serialId, sequenceLookup[i].description);
            // Default to the first one
            state.value1 = sequenceLookup[0].sequenceActionId;
          } else
            state.value1 = sequenceAction;
          break;

        case ALL_BAGS:
          s.remove(0, 1);
          state.value1 = constrain(s.toInt(), -100, 100);
          break;

        case ONE_BAG:
          s.remove(0, 1);
          i = s.indexOf('=');
          if (i < 0) {
            // Show valid options
            userHelpAction(intensity, pace, constant);
            // Default to 'o0=0'
            state.value1 = state.value2 = 0;
          } else {
            String s1 = s.substring(0, i);
            state.value1 = constrain(s1.toInt(), 0, 7);
            s1 = s.substring(i + 1);
            state.value2 = constrain(s1.toInt(), -100, 100);
          }
          break;

        case INTENSITY:
        case PACE:
          s.remove(0, 1);
          state.value1 = constrain(s.toInt(), 0, 100);
          break;

        case RANDOM:
          break;

        default:
          state.action = SEQUENCE;
          state.value1 = 0;
          LOG(LOG_WARNING, "Should never occur -> start SEQUENCE_0");
          break;
      }
    }
  }

  // Check for received OSC messages
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // Allocate buffer for incoming packet
    uint8_t incomingPacket[packetSize];
    udp.read(incomingPacket, packetSize);

    // Parse the incoming OSC message
    OSCMessage msg;
    msg.fill(incomingPacket, packetSize);

    if (msg.hasError())
      LOG(LOG_ERROR, "OSC Error: %s", msg.getError());
    else {
      // Handle the OSC message
      char address[100];
      char payload[100];
      if (msg.fullMatch("/command")) {
        int arg0 = msg.getString(0, payload);
        LOG(LOG_INFO, "%s: %s", msg.getAddress(), payload);
        sendOSCMessageCommandReceived(payload);
        switch (payload[0]) {
          case '0':
            LOG(LOG_INFO, "Stop all sequence snippets");
            for (int i = 0; i < NUMBER_OF_BAGS; i++) {
              sequence0[i].sequenceSnippet = sequenceSnippet_stop;
            }
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '1':
            LOG(LOG_INFO, "Start breathing");
            sequence0[LUNGS].sequenceSnippet = sequenceSnippet_breath;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '2':
            LOG(LOG_INFO, "Stop breathing");
            sequence0[LUNGS].sequenceSnippet = sequenceSnippet_stop;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '3':
            LOG(LOG_INFO, "Arm left");
            sequence0[ARM_LEFT].sequenceSnippet = sequenceSnippet_left_arm;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '4':
            LOG(LOG_INFO, "Arm right");
            sequence0[ARM_RIGHT].sequenceSnippet = sequenceSnippet_right_arm;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '5':
            LOG(LOG_INFO, "Stop arms");
            sequence0[ARM_LEFT].sequenceSnippet = sequenceSnippet_stop;
            sequence0[ARM_RIGHT].sequenceSnippet = sequenceSnippet_stop;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '6':
            LOG(LOG_INFO, "Head");
            sequence0[HEAD].sequenceSnippet = sequenceSnippet_head;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case '9':
            LOG(LOG_INFO, "All together now!");
            sequence0[HEAD].sequenceSnippet = sequenceSnippet_head;
            sequence0[LUNGS].sequenceSnippet = sequenceSnippet_breath;
            sequence0[ARM_RIGHT].sequenceSnippet = sequenceSnippet_right_arm;
            sequence0[ARM_LEFT].sequenceSnippet = sequenceSnippet_left_arm;
            state.action = SEQUENCE;
            state.value1 = SEQUENCE_0;
            state.previousValue1 = SEQUENCE_UNDEFINED;
            break;

          case 'a':
            LOG(LOG_INFO, "Deflate all sequence");
            state.action = SEQUENCE;
            state.value1 = DEFLATE_ALL;
            break;

          case 'b':
            LOG(LOG_INFO, "Inflate all sequence");
            state.action = SEQUENCE;
            state.value1 = INFLATE_ALL;
            break;

          case 'c':
            LOG(LOG_INFO, "Do nothing, all fans minimal");
            state.action = ALL_BAGS;
            state.value1 = 0;
            break;

          case 'd':
            LOG(LOG_INFO, "All bags maximum deflation");
            state.action = ALL_BAGS;
            state.value1 = -100;
            break;

          case 'e':
            LOG(LOG_INFO, "All bags maximum inflation");
            state.action = ALL_BAGS;
            state.value1 = 100;
            break;

          case 'f':
            LOG(LOG_INFO, "Test sequence");
            state.action = SEQUENCE;
            state.value1 = TEST;
            break;

          default:
            LOG(LOG_WARNING, "Unhandled command");
            break;
        }
      } else if (msg.fullMatch(OscAddressKeepAlive)) {
        LOG(LOG_INFO, "rcv: %s, %i", msg.getAddress(), msg.getInt(0));
      }
    }
  }

  // Check if input changed
  if (state.previousAction != state.action || state.previousValue1 != state.value1 || state.previousValue2 != state.value2) {
    state.previousAction = state.action;
    state.previousValue1 = state.value1;
    state.previousValue2 = state.value2;

    // Handle user input
    switch (state.action) {
      case SEQUENCE_ALL:
        sequenceAllLoopCount = sequenceAllSelect = 0;
        break;

      case SEQUENCE:
        LOG(LOG_INFO, "%s", sequenceLookup[state.value1].description);
        sequenceFans(sequenceStates, sequenceSteps, sequenceLookup[state.value1].pointer);
        break;

      case RANDOM:
        randomLoopCount = 0;
        break;

      case ALL_BAGS:
        allBags(constant = state.value1);
        break;

      case ONE_BAG:
        oneBag(state.value1, constant = state.value2);
        break;

      case INTENSITY:
        intensity = state.value1;
        break;

      case PACE:
        pace = state.value1;
        break;

      default:
        sequenceAllLoopCount = sequenceAllSelect = 0;
        state.action = SEQUENCE_ALL;
        LOG(LOG_ERROR, "Should never occur -> start SEQUENCE_ALL");
        break;
    }
  }

  // Timer event occured
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
    portENTER_CRITICAL(&timerMux);
    bool wdtFlagCopy = wdtFlag;
    wdtFlag = false;
    portEXIT_CRITICAL(&timerMux);
    // reset WDT every 1 second
    if (wdtFlagCopy)
      esp_task_wdt_reset();

    // Heartbeat led toggle
    digitalWrite(LED_OUTPUT, !digitalRead(LED_OUTPUT));

    if (oscTimer > 0) {
      oscTimer--;
      if (oscTimer == 0) {
        sequenceAllLoopCount = sequenceAllSelect = 0;
        sendOSCMessageKeepAlive(keep_alive_counter);
        keep_alive_counter = (keep_alive_counter + 1) % 10000;
        oscTimer = OSC_KEEP_ALIVE_TIMEOUT;
      }
    }

    switch (state.action) {
      case SEQUENCE_ALL:
        if (sequenceAllLoopCount == 0) {
          if (sequenceAllSelect == 0) {
            LOG(LOG_INFO, "Random init SEQUENCE_ALL");
            randomFans(sequenceStates, sequenceSteps);
          } else {
            LOG(LOG_INFO, "%s", sequenceLookup[sequenceAllSelect - 1].description);
            sequenceFans(sequenceStates, sequenceSteps, sequenceLookup[sequenceAllSelect - 1].pointer);
          }
          sequenceAllSelect = (sequenceAllSelect + 1) % 5;  // Random and first 4 sequences
        }
        sequenceAllLoopCount = (sequenceAllLoopCount + 1) % MAX_FAN_STEPS;
        executeFanSequence(sequenceStates, sequenceSteps, intensity, pace);
        break;

      case RANDOM:
        if (randomLoopCount == 0) {
          LOG(LOG_INFO, "Random init RANDOM");
          randomFans(sequenceStates, sequenceSteps);
        }
        randomLoopCount = (randomLoopCount + 1) % MAX_FAN_STEPS;
        executeFanSequence(sequenceStates, sequenceSteps, intensity, pace);
        break;

      case SEQUENCE:
      case INTENSITY:
      case PACE:
        executeFanSequence(sequenceStates, sequenceSteps, intensity, pace);
        break;

      case ALL_BAGS:
      case ONE_BAG:
        break;  // Nothing to do, no sequence ongoing

      case INFO:
      case ACTION_UNDEFINED:
      default:
        // This should never occur so restart!
        LOG(LOG_INFO, "Unexpected Action [%d] -> restart", state.action);
        ESP.restart();
        break;
    }
  }
}
