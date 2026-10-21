#ifndef SIMON_ENCODER_H
#define SIMON_ENCODER_H

#include <Arduino.h>
#include "esp_timer.h"

// Callback przyjmuje 'delta' (o ile zmienić wartość), zawsze > 0;
// kierunek wynika z tego, który callback (Right/Left) został wywołany.
typedef void (*EncoderCallback)(int delta);
enum AccelerationMode {
  ACCEL_OFF = 1, ACCEL_LOW = 3,
  ACCEL_MEDIUM = 5, ACCEL_HIGH = 10
};

enum EncoderType { FULL_STEP = 4, HALF_STEP = 2, QUARTER_STEP = 1 };

class SimonEncoder {
public:
    SimonEncoder(uint8_t pinA, uint8_t pinB, EncoderType type = FULL_STEP, AccelerationMode acc = ACCEL_MEDIUM);
    void begin(uint32_t interval_us = 500);
    void eventAnalyze();   // wołać z loop(): wywołuje callbacki

    void attachOnRight(EncoderCallback fn);
    void attachOnLeft(EncoderCallback fn);

    long getCount();
    void resetCount();

    void setAcceleration(AccelerationMode mode);

private:
    static void timerCallback(void* arg);
    void tick();

    uint8_t _pinA, _pinB;
    EncoderType _type;
    AccelerationMode _accMode;
    volatile int8_t _subStepCounter;
    volatile long _totalCount;

    // Zdarzenia: licznik oczekujących "klików" (ze znakiem) zamiast pary flag,
    // żeby szybkie kręcenie nie gubiło zdarzeń między wywołaniami eventAnalyze().
    volatile int32_t _pending;
    volatile uint32_t _lastEventTime;
    volatile uint32_t _thisEventTime;

    uint8_t _old_s;
    EncoderCallback _cbRight;
    EncoderCallback _cbLeft;

    esp_timer_handle_t _timerHandle;
    static const int8_t _enc_states[];
};

#endif
