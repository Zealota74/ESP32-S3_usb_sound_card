/*
 * controls.h - przypisanie wejść (enkoder, klawisze A..D, pilot IR) do akcji w audio, UI i menu.
 *
 * input.* jest czystym modułem sprzętowym (piny, timer, debounce, IR) i nie zna reszty systemu.
 * Tu leżą callbacki i ich rejestracja:
 *   enkoder: obrót = głośność (gdy okienko menu otwarte: zmiana wartości w okienku),
 *            klik = mute, dwuklik = zwolnienie limitu startowego, długie = ekran STAT <-> SPECTRUM,
 *   A (Right) / C (Left) = następny / poprzedni utwór,
 *   B (Up) / D (Down)    = poprzednie / następne okienko menu (menu_oled.*).
 * Klawisze A..D i pilot IR wywołują te same funkcje (mapowanie kodów IR w input.h).
 */
#ifndef CONTROLS_H
#define CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Wołać po INPUT_init() i AUDIO_Init(): dwuklik rejestrowany jest tylko przy aktywnym limicie startowym
 * (dwuklik opóźnia pojedynczy klik, więc bez limitu go nie włączamy). */
void CONTROLS_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLS_H */
