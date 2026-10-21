#ifndef INPUT_MODULE_H
#define INPUT_MODULE_H

#include "setup.h"

// Inicjalizacja modułu (piny i biblioteki)
void INPUT_init();

// Główna pętla modułu (obsługa timerów, przycisków i bufora)
void INPUT_proc();

// Funkcje do rejestracji akcji (callbacki)
typedef void (*InputCallback)();
// Dodaj to do input_module.h
typedef void (*EncoderCallback)(int delta); // Callback przyjmujący inta

void INPUT_attach_action(InputCallback cb);
void INPUT_attach_long_press(InputCallback cb);
void INPUT_attach_double_click(InputCallback cb);   // opcjonalny; dwuklik opóźnia pojedynczy klik, więc podpinaj tylko gdy potrzebny
void INPUT_attach_enter(InputCallback cb);
void INPUT_attach_back(InputCallback cb);
void INPUT_attach_rotate(EncoderCallback cb);

void INPUT_attach_keyA(InputCallback cb);
void INPUT_attach_keyB(InputCallback cb);
void INPUT_attach_keyC(InputCallback cb);
void INPUT_attach_keyD(InputCallback cb);

// Funkcja synchronizująca parametry z myConfig
void INPUT_update_settings(int timeout);

void INPUT_set_buffer_enabled(bool enabled);

#endif
