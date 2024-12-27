#ifndef DEBUG_H
#define DEBUG_H

#if defined(DEBUG_OUTPUT)
    #define DEBUG_BEGIN(baud) Serial.begin(baud)
    #define DEBUG_END() Serial.end()
    #define DEBUG_FLUSH() Serial.flush()
    #define DEBUG_LOG(x) Serial.print(x)
    #define DEBUG_LOGF(x, ...) Serial.printf(x, __VA_ARGS__)
    #define DEBUG_DELAY(x) delay(x)
#else
    #define DEBUG_BEGIN(baud)
    #define DEBUG_END()
    #define DEBUG_FLUSH()
    #define DEBUG_LOG(x)
    #define DEBUG_LOGF(x, ...)
    #define DEBUG_DELAY(x)
#endif
