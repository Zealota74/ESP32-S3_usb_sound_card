#include "SimonEncoder.h"

const int8_t SimonEncoder::_enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

SimonEncoder::SimonEncoder(uint8_t pinA, uint8_t pinB, EncoderType type, AccelerationMode acc)
    : _pinA(pinA), _pinB(pinB), _type(type), _accMode(acc) {
    _subStepCounter = 0; _totalCount = 0;
    _pending = 0;
    _lastEventTime = 0; _thisEventTime = 0;
    _old_s = 0;
    _cbRight = nullptr; _cbLeft = nullptr;
    _timerHandle = nullptr;
}

void SimonEncoder::timerCallback(void* arg) {
    reinterpret_cast<SimonEncoder*>(arg)->tick();
}

void SimonEncoder::begin(uint32_t interval_us) {
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    _old_s = (digitalRead(_pinA) ? 1 : 0) | (digitalRead(_pinB) ? 2 : 0);

    const esp_timer_create_args_t timer_args = {
        .callback = &SimonEncoder::timerCallback,
        .arg = this,
        .name = "simon_enc_timer"
    };
    if (esp_timer_create(&timer_args, &_timerHandle) == ESP_OK) {
        esp_timer_start_periodic(_timerHandle, interval_us);
    }
}

// Działa w tasku esp_timer (rdzeń 0), a eventAnalyze() w loop() (rdzeń 1),
// dlatego wspólny licznik _pending jest modyfikowany operacjami atomowymi.
void SimonEncoder::tick() {
    uint8_t s = (digitalRead(_pinA) ? 1 : 0) | (digitalRead(_pinB) ? 2 : 0);
    if (s != _old_s) {
        int8_t move = _enc_states[(_old_s << 2) | s];
        if (move > 0 && _subStepCounter < 0) _subStepCounter = 0;
        if (move < 0 && _subStepCounter > 0) _subStepCounter = 0;
        _subStepCounter += move;

        if (abs(_subStepCounter) >= (int8_t)_type) {
            int32_t dir = (_subStepCounter > 0) ? 1 : -1;
            _thisEventTime = millis();
            _totalCount += dir;
            __atomic_fetch_add(&_pending, dir, __ATOMIC_ACQ_REL);
            _subStepCounter = 0;
        }
        _old_s = s;
    }
}

void SimonEncoder::eventAnalyze() {
    int32_t n = __atomic_exchange_n(&_pending, (int32_t)0, __ATOMIC_ACQ_REL);
    if (n == 0) return;

    uint32_t t = _thisEventTime;
    uint32_t dt = t - _lastEventTime;
    _lastEventTime = t;

    int mult = 1;
    if (_accMode != ACCEL_OFF) {
        if (dt < 40)       mult = (int)_accMode * 2;
        else if (dt < 100) mult = (int)_accMode / 2;
        if (mult < 1) mult = 1;
    }

    if (n > 0) {
        if (_cbRight) _cbRight((int)n * mult);
    } else {
        if (_cbLeft) _cbLeft((int)(-n) * mult);
    }
}

void SimonEncoder::setAcceleration(AccelerationMode mode) { _accMode = mode; }
void SimonEncoder::attachOnRight(EncoderCallback fn) { if (fn) _cbRight = fn; }
void SimonEncoder::attachOnLeft(EncoderCallback fn) { if (fn) _cbLeft = fn; }
long SimonEncoder::getCount() { return _totalCount; }
void SimonEncoder::resetCount() { _totalCount = 0; }
