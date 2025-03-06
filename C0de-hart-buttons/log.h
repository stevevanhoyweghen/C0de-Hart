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
char* logTimestamp() {
  // Division constants
  const uint16_t MSECS_PER_SEC = 1000;
  const uint8_t SECS_PER_MIN = 60;
  const uint8_t MINS_PER_HOUR = 60;
  const uint16_t SECS_PER_HOUR = MINS_PER_HOUR * SECS_PER_MIN;
  const uint8_t HOURS_PER_DAY = 24;
  const uint32_t SECS_PER_DAY = HOURS_PER_DAY * SECS_PER_HOUR;

  const unsigned long ms = millis();
  const unsigned long s = ms / MSECS_PER_SEC;

  // Time as string
  static char timestamp[20];
  sprintf(timestamp, "%d,%02d:%02d:%02d.%03d",
                     s / SECS_PER_DAY,                    // days
                     (s / SECS_PER_HOUR) % HOURS_PER_DAY, // hours
                     (s / SECS_PER_MIN) % MINS_PER_HOUR,  // minutes
                     s % SECS_PER_MIN,                    // seconds
                     ms % MSECS_PER_SEC);                 // ms
  return timestamp;
}

#define LOG_INFO 0
#define LOG_WARNING 1
#define LOG_ERROR 2
#define LOG_FATAL 3

#define LOG_ENABLE

static const char* logLvl[] = { "Info", "Warning", "Error", "Fatal" };
#ifdef LOG_ENABLE
#define WINDOWS_LOG '\\'
#define LINUX_LOG '/'
#define LOG(level, message, ...) \
Serial.printf((const char*)F("%s %s %u %s %s: " message "\n"), strrchr(__FILE__, LINUX_LOG) + 1,  __func__, __LINE__, logTimestamp(), logLvl[level], ##__VA_ARGS__)
// Long version including __PRETTY_FUNCTION__ and ESP.getFreeHeap()
//  Serial.printf((const char*)F("%s,%u,%u,%s,%s: " message "\n"), \
//                 strrchr(__FILE__, LINUX_LOG) + 1, __LINE__, ESP.getFreeHeap(), __PRETTY_FUNCTION__, logLvl[level], ##__VA_ARGS__)
#else
#define LOG(level, message, ...)
#endif
