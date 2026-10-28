# Karta dźwiękowa USB na ESP32-S3: architektura

Dokument opisuje budowę oprogramowania w stanie po przebudowie podsystemu audio (etapy 1, 2a, 2b, 2c oraz części 2: stereo na ekranie, szczyty, okno FFT). Ma leżeć razem z kodem i być jedynym miejscem, gdzie spisane są warstwy, nazewnictwo i reguły forka. Jeśli kod i dokument się rozjadą, wygrywa kod, a dokument trzeba poprawić.

Opis obejmuje wszystkie moduły projektu: szkic `.ino`, `audio_manage.*`, `AUDIO/`, `HARDWARE/`, `DSP/`, `dsp_manage.*`, warstwę UI oraz `controls.*`, `input.*` i `SimonEncoder`. Środowisko budowania jest w sekcji 12, a to, czego dokument nie obejmuje, w ostatniej sekcji.

---

## 1. Cel i priorytety

Urządzenie jest kartą dźwiękową USB (UAC 1.0) z przetwornikiem PCM5102 po I2S. Dodatkami są wyświetlacz OLED z menu, analizator widma, sterowanie enkoderem, klawiszami i pilotem IR.

Priorytety, w tej kolejności (kolejność rozstrzyga każdy spór o projekt):

1. **Dźwięk bez artefaktów.** Tor USB, FIFO, korekta dryfu i I2S nie mogą być zakłócane przez nic innego. Jedyną obróbką próbek w torze jest korekta dryfu (resampler z interpolacją liniową, maks. ±0,2%, niesłyszalna) i mnożnik głośności z rampą w obrębie bloku.
2. **Stabilność USB i kolejki audio.**
3. **Wizualizacja.** To dodatek („music visualizer"), nie przyrząd pomiarowy. Gdy brakuje czasu, wizualizacja traci, audio nie.

Konsekwencje wpisane w kod:

- Funkcja `tap` w wątku wyjścia audio tylko kopiuje ramki do kolejki. Nie blokuje, nie liczy, nie drukuje.
- Analiza widma działa tylko na ekranie SPECTRUM (`DSP_SpectrumEnable`). Poza nim tap nic nie kopiuje.
- Gdy `loop()` spóźni się za bardzo, analizator **porzuca zaległość** (luka) zamiast opóźniać cokolwiek w torze audio.

## 2. Warstwy i kierunek zależności

```
 UI i sterowanie        ui_oled, ui_bars, menu_oled, audio_ui, controls, input
 (katalog szkicu)                     |
                                      v
 Zarządcy (src/)        dsp_manage.*  ---- tap ---->  audio_manage.*
                              |                              |
                              v                              v
 Moduły (src/)             DSP/                           AUDIO/
                      spectrum*.cpp              fifo, drift, kontrakty HAL
                                                             ^
                                                             | implementują kontrakty
                                                         HARDWARE/
                                          usb_soundcard, audio_out_i2s_esp32, audio_clock_esp32
```

**Reguły zależności (obowiązują zawsze):**

1. Zależności idą w dół: UI → zarządcy → moduły. Warstwa niższa nie zna wyższej.
2. `audio_manage.*` i `AUDIO/` nie znają USB, I2S, Arduino, DSP ani UI. Sprzęt widzą wyłącznie przez trzy kontrakty (`audio_source.h`, `audio_out.h`, `audio_clock.h`). Implementacje leżą w `HARDWARE/` i same dołączają nagłówki z `AUDIO/` (odwrócenie zależności). Konkretne źródło wybiera `AUDIO_SOURCE` w `audio_config.h`, a wyjście wyznacza plik `audio_out_*.cpp` w `HARDWARE/`.
3. Tor audio udostępnia DSP jeden punkt zaczepienia: `AUDIO_SetTap()`. `DSP/` nie wie nic o UI ani o wyświetlaczu, zna tylko próbki i czas (`AUDIO_TimeUs`).
4. `ui_bars.*` nie zna DSP, wyświetlacza ani Arduino. Dostaje tablicę mocy i czas, oddaje tablicę wysokości.
5. Kod platformowy (Arduino, ESP-IDF, I2S, SPI, USB, timery) jest tylko w: `HARDWARE/`, `input.cpp` (timer sprzętowy, IRMP), `ui_oled.cpp` (U8g2, SPI) i w szkicu `.ino`. Przenośne bez zmian są: `AUDIO/`, `audio_manage.cpp`, `DSP/`, `dsp_manage.cpp`, `ui_bars.*` oraz logika `menu_oled.*` i `controls.*`.

### Podział odpowiedzialności

| Warstwa / plik | Rola | Czego nie robi |
|---|---|---|
| `HARDWARE/usb_soundcard.*` | źródło audio USB (UAC 1.0, `USBAudioCard`, HID Consumer Control): odbiór PCM, głośność i mute od hosta, klawisze głośności i multimedialne, wybór częstotliwości przez hosta | nie zna FIFO, I2S ani DAC |
| `HARDWARE/audio_out_i2s_esp32.cpp` | wyjście: I2S (`i2s_std`) do PCM5102, zadanie FreeRTOS wołające `fill()`, zmiana częstotliwości | nie zna FIFO ani źródła |
| `HARDWARE/audio_clock_esp32.cpp` | zegar mikrosekundowy (`micros()`) | |
| `AUDIO/fifo.*` | bufor kołowy SPSC (jeden producent, jeden konsument, bez blokad); tylko `<stdint.h>` i `<string.h>` | nie zna USB ani I2S |
| `AUDIO/drift.*` | korekta dryfu zegarów: resampler z interpolacją liniową, regulator utrzymuje poziom FIFO, korekta ograniczona do ±0,2% | tylko fifo.h |
| `AUDIO/audio_*.h` | kontrakty: `audio_source.h`, `audio_out.h`, `audio_clock.h`, typy (`audio_types.h`), parametry (`audio_config.h`) | |
| `audio_manage.*` | publiczne API toru: `AUDIO_Init`, `AUDIO_Poll`, głośność, mute, klawisze multimedialne, limit startowy, statystyki, `AUDIO_SetTap`, `AUDIO_TimeUs`, `AUDIO_GetSampleRate`; łączy źródło, FIFO, drift, tap, mnożnik głośności i wyjście | nie liczy widma |
| `DSP/` | analiza widma: bufor kołowy, pasma logarytmiczne, silniki FFT, IIR, IIR-MR | nie wie o wyglądzie słupków |
| `dsp_manage.*` | łączy tor audio z analizatorem: kolejka z tapa, polityka zaległości, pomiar kosztu | nie rysuje |
| `ui_bars.*` | z mocy pasm robi wygląd słupków: dB, narastanie, opadanie, szczyty | nie zna DSP |
| `ui_oled.*` | rysowanie na SH1122, warstwy ekranu i OSD | nie liczy widma, nie zmienia ustawień audio |
| `menu_oled.*` | okienka menu, zmiana ustawień przez DSP i UI | nie rysuje (rysuje `ui_oled`) |
| `audio_ui.*` | most audio → UI: głośność, limit startowy, widmo, diagnostyka | nie woła silników, tylko `dsp_manage` |
| `input.*` | sprzęt wejść: piny, timer, debounce, enkoder, IR | nie zna reszty systemu |
| `controls.*` | przypisanie wejść do akcji w audio, UI i menu | |

### Kontrakty sprzętowe (HAL)

| Kontrakt | Co wymaga od implementacji |
|---|---|
| `audio_source.h` (`AUDIO_Source_t`) | `name`, `init`, `poll`; opcjonalnie: `get_volume`, `volume_step`, `mute_toggle`, `media_key`, `get_stats`, `get_sample_rate`. Źródło wpisuje PCM funkcją `push` i jest **jedynym producentem FIFO** |
| `audio_out.h` | `AUDIO_OUT_Init`, `AUDIO_OUT_Start( fill )`, `AUDIO_OUT_SetSampleRate`, `AUDIO_OUT_GetErrors`. `fill()` zawsze wypełnia cały blok (w razie braku danych ciszą) i nie blokuje; zmiana częstotliwości jest jedynym dozwolonym wyjątkiem i wolno ją zlecać tylko z `fill()` |
| `audio_clock.h` | `AUDIO_CLOCK_TimeUs()`: monotoniczny czas w µs (zawija po ok. 71 min, liczyć różnice `uint32_t`) |

## 3. Przepływ danych

```
Windows ──USB──> usb_soundcard (AUDIO_SRC_USB) ──AUDIO_Push──> FIFO ──> drift ──┐
                                                                                │
        ┌───────────────────────────────────────────────────────────────────────┘
        │
        ├── tap (po drifcie, przed głośnością): kopia ramek stereo ──> kolejka (dsp_manage)
        │                                                                  │ loop: DSP_Poll()
        │                                                                  v
        │                                       SPECTRUM_Feed -> bufory kołowe -> silnik
        │                                                                  │ SPECTRUM_Process()
        │                                                                  v
        │                                       moc pasm [kanał * bands + pasmo], skala liniowa
        │                                                                  │ DSP_GetBandPower()
        │                                                                  v
        │                                       ui_bars: dB -> 0..255, opadanie, szczyty
        │                                                                  │ BARS_Get()
        │                                                                  v
        │                                       ui_oled: słupki na OLED (256 x 64)
        v
   mnożnik głośności (z rampą w bloku) ──> wyjście I2S ──> PCM5102
```

Szkic `.ino` ma taką kolejność (istotną: `AUDIO_Init` przed `DSP_Init`, `CONTROLS_Init` po `AUDIO_Init`):

```
setup():
  Serial.begin( 115200 ); delay( 1000 );    // log diagnostyczny na UART0
  UI_Init();
  INPUT_init();
  AUDIO_Init( &cfg );                       // cfg.startup_vol_db = STARTUP_VOL_DB (-20 dB); przy błędzie: komunikat i pętla
  DSP_Init();                               // rejestruje tap
  AUDIOUI_Init( STARTUP_VOL_DB );
  CONTROLS_Init();                          // po AUDIO_Init(): dwuklik zależy od limitu startowego

loop():
  INPUT_proc();          // enkoder, klawisze, IR: tu wołane są callbacki z controls
  AUDIO_Poll( now );     // źródło, FIFO, głośność, limit startowy
  DSP_Poll();            // widmo
  AUDIOUI_Sync( now );   // głośność, widmo -> UI
  UI_Loop( now );        // rysowanie, tylko gdy trzeba
  diagPrint( now );      // co sekundę statystyki audio na Serial (AUDIO_DIAG_SERIAL)
  delay( 1 );
```

`loop()` kończy się `delay( 1 )`, więc jego okres jest nie krótszy niż 1 ms, a dłuższy, gdy trwa rysowanie ekranu. Całość musi działać w jednym wątku `loop()`.

Widmo jest liczone **po** korekcie dryfu i **przed** regulacją głośności, więc nie zależy od poziomu ustawionego na urządzeniu ani na hoście.

### Parametry toru (`AUDIO/audio_config.h`)

| Parametr | Wartość |
|---|---|
| format | 16 bit stereo |
| częstotliwość | 44100 (start) lub 48000 Hz, wybiera host (Windows); sufit dla USB UAC1 z rdzeniem arduino-esp32 3.3.x to 48 kHz |
| blok przetwarzania | 256 ramek |
| FIFO | 32768 B (potęga dwójki), punkt pracy 46 ms (liczony z bieżącej częstotliwości), start odtwarzania dopiero po napełnieniu do punktu pracy |
| niedobór (underrun) | wygaszenie ogona, cisza i ponowny prefill; strumień na wyjściu nigdy się nie urywa |

### Źródło USB (`HARDWARE/usb_soundcard.*`)

Dla reszty projektu to jeden obiekt `AUDIO_SRC_USB` (`audio_source.h`); funkcje `USB_*` i `VOL_*` są szczegółem implementacji.

- **Urządzenie:** VID 0x303A, PID 0x4001; dwa interfejsy: UAC 1.0 (głośnik stereo 16 bit, bez mikrofonu) oraz HID Consumer Control (klawisze głośności, mute i utworów).
- **Odbiór danych:** callback `usbRxCallback` działa w kontekście TinyUSB i tylko wpisuje pakiet do FIFO (`push`) oraz zwiększa liczniki. Bez `printf` i bez blokowania.
- **Zdarzenia hosta** (głośność, mute, częstotliwość) obsługuje callback w zadaniu pętli zdarzeń ESP-IDF; musi być bardzo szybki (wolny callback zapycha kolejkę zdarzeń, a przepełnienie może zresetować urządzenie). Zdarzenie częstotliwości tylko zapisuje wybraną wartość, którą `AUDIO_Poll` odczytuje przez `get_sample_rate`; zdarzenia głośności i mute zwiększają licznik i trafiają do małej kolejki diagnostycznej (opróżnianej w `loop()` przez `USB_DebugPoll`).
- **Start:** głośność początkowa jest ustawiana lokalnie przed `USB.begin()`, a poprawność sprawdzana odczytem zwrotnym (wynik decyduje, czy źródło „gwarantuje” głośność startową, czy rdzeń ma założyć limit).
- **Częstotliwości:** lista ogłaszana hostowi pochodzi z lokalnej, zmodyfikowanej kopii biblioteki `USBAudioCard` (`USB_UAC1_RATES` w `USBAudioCard.cpp`) i **musi zgadzać się z `AUDIO_SAMPLE_RATE_LIST`**.

### Głośność i limit startowy

Głośnością steruje host USB, a urządzenie jej tylko żąda. Enkoder zmienia **poziom docelowy** (`USB_VolStep`), a regulator w pętli zamkniętej (`USB_VolPoll`, wołany z `AUDIO_Poll` przez `srcPoll`) dochodzi do niego klawiszami HID:

1. wysyła jeden klawisz `VOLUME_INCREMENT` albo `VOLUME_DECREMENT`,
2. czeka na zdarzenie od hosta (lub `VOL_ACK_TIMEOUT_MS` = 50 ms),
3. odczytuje, jakie dB ustawił Windows, i powtarza do osiągnięcia celu.

Zabezpieczenia regulatora: koniec, gdy host przeskoczy cel (nie cofa się, żeby nie oscylować); przerwanie po `VOL_MAX_NOCHANGE` (12) klawiszach bez zmiany dB (host nie odpowiada); jeśli urządzenie jest wyciszone przyciskiem, a suwak nie jest na dole, najpierw jednorazowo wysyła mute.

- **Poziomy:** 32 poziomy (`VOL_LEVELS`) z tabeli zmierzonej na Windows (każdy poziom to dB, które Windows faktycznie wysyła do karty klawiszami ±2%): od −50 dB (poziom 0 = suwak 0%, mute) do 0 dB (100%); procenty w tabeli `s_lvlPct` służą do wyświetlania. Pozycje suwaka dające to samo dB są nierozróżnialne i pominięte. Jeden tick enkodera = jeden poziom. Zakres zgłaszany hostowi: −50…0 dB, krok 1 dB.
- **Mnożnik:** z dB od hosta powstaje jeden mnożnik amplitudy (`VOL_HostToGain`: ≤ −50 dB daje 0, ≥ 0 dB daje 1, między nimi `10^(dB/20)`), stosowany w `audio_manage` po tapie, z rampą w obrębie bloku (bez trzasków). Mute zeruje sygnał. Stan mute dla UI: w trakcie ruchu regulatora „poziom docelowy = 0”, poza ruchem stan mute hosta.
- **Limit startowy:** źródło próbuje ustawić głośność startową (`startup_vol_db`, w szkicu `STARTUP_VOL_DB` = −20 dB), żeby po włączeniu nie grało na 0 dB. Gdy odczyt zwrotny się nie zgadza, rdzeń tłumi sygnał, dopóki host się nie odezwie albo użytkownik nie zwolni limitu (`AUDIO_GuardRelease`, dwuklik enkodera). OSD głośności pokazuje wtedy dopisek LIMIT.
- **Klawisze multimedialne:** następny i poprzedni utwór (`AUDIO_MediaKey` → HID Consumer Control, kody Scan Next / Previous Track) są wysyłane do hosta tym samym kanałem co klawisze głośności.

### Wątki i czas

| Wątek | Co wykonuje | Zasada |
|---|---|---|
| wyjście audio | zadanie FreeRTOS w `audio_out_i2s_esp32.cpp` (rdzeń 1), wołające `fill()`: FIFO → drift → tap → głośność | tylko kopiowanie w tapie, bez blokowania i bez `Serial` |
| zdarzenia USB | callbacki biblioteki `USBAudioCard`: dane do FIFO (`push`), głośność, mute, wybór częstotliwości | źródło jest jedynym producentem FIFO |
| `loop()` | `AUDIO_Poll`, `DSP_Poll`, UI, obsługa wejść | tu mieszkają rzeczy, które mogą się spóźnić |
| przerwanie timera | próbkowanie IR, enkodera i poziomów klawiszy (`input.cpp`) | tylko próbkowanie, logika w `INPUT_proc()` |

Analizator (`DSP/`) jest **jednowątkowy**: wszystkie funkcje `SPECTRUM_*` woła tylko `loop()`. Przekazanie próbek z wątku audio i politykę zaległości prowadzi wyłącznie `dsp_manage`.

**Kolejka tap → loop.** Bufor kołowy ramek stereo (`DSP_QUEUE_FRAMES` = 4096, potęga dwójki). Zapisuje tylko wątek audio, czyta tylko `loop()`. Zapis nigdy nie czeka. `loop()` kopiuje porcję, a potem sprawdza, czy zapis jej nie dogonił (jak seqlock). Jeśli tak, albo zaległość przekracza próg `DSP_QUEUE_FRAMES - DSP_QUEUE_GUARD_FRAMES`, cała zaległość jest porzucana, a analizator resetowany (`SPECTRUM_Resync`). Tolerancja spóźnienia `loop()` to ok. 81 ms przy 44,1 kHz. Liczbę luk pokazuje `DSP_GetStats()`.

Jedno `DSP_Poll()` przekazuje analizatorowi najwyżej `DSP_POLL_MAX_HOPS` hopów (2), żeby czas jednego wywołania był ograniczony. Reszta czeka w kolejce.

## 4. Nazewnictwo: bin, band, bar

Trzy pojęcia, które łatwo pomylić i które celowo są rozdzielone między warstwami:

| Pojęcie | Znaczenie | Gdzie wolno go używać |
|---|---|---|
| **bin** | komórka transformaty FFT (amplituda/moc dla jednej częstotliwości) | **tylko wewnątrz** `spectrum_fft.cpp` |
| **band** (pasmo) | wyjście DSP: moc w pasmie logarytmicznym, wspólna dla FFT, IIR i przyszłego CQT | `DSP/`, `dsp_manage.*`, `audio_ui.*` (na styku) |
| **bar** (słupek) | element wyświetlacza | **tylko** warstwa UI (`ui_bars`, `ui_oled`, `menu_oled`) |

Reguły:

1. W DSP nie ma słowa „bar". Moduł DSP nie wie, że słupki istnieją.
2. W UI nie ma słowa „bin". UI nie wie, że istnieje transformata.
3. Przy przejściu z DSP do UI jedno pasmo odpowiada jednemu słupkowi (`BARS_Update( moc, ile, czas )`).

### Pozostałe terminy

| Termin | Znaczenie |
|---|---|
| kanał | 1 = miks mono (L+R)/2, 2 = L i R osobno |
| okno (window, `n`) | ile ostatnich próbek trafia do jednej FFT: 1024, 2048 lub 4096 |
| `SPECTRUM_FFT_N` | **maksymalne** okno: rozmiar buforów i tablic, nie ustawienie |
| hop (`SPECTRUM_HOP`) | liczba nowych próbek między kolejnymi ramkami widma |
| ramka | jeden wynik analizy: moc wszystkich pasm wszystkich kanałów |
| ring | bufor kołowy próbek, wspólny dla silników, rozmiar 2 × `SPECTRUM_FFT_N` |
| tap | punkt zaczepienia w torze audio, z którego DSP dostaje kopię strumienia |

### Prefiksy

| Prefiks | Moduł |
|---|---|
| `AUDIO_` | `audio_manage.*` i tor audio; `AUDIO_OUT_*` (wyjście) i `AUDIO_CLOCK_*` (zegar) to kontrakty sprzętowe |
| `FIFO_`, `DRIFT_` | moduły toru audio (styl C, bez klas) |
| `USB_`, `VOL_` | źródło USB w `HARDWARE/usb_soundcard.*` (detale implementacji źródła) |
| `DSP_` | `dsp_manage.*` |
| `SPECTRUM_` | analizator widma w `DSP/` (nie „SPEC_": skrót kojarzył się ze specyfikacją) |
| `BARS_` | `ui_bars.*` |
| `UI_` | `ui_oled.*` |
| `MENU_` | `menu_oled.*` |
| `AUDIOUI_` | `audio_ui.*` |
| `INPUT_` | moduł wejść (enkoder, klawisze, IR), `input.*` |
| `CONTROLS_` | przypisanie wejść do akcji, `controls.*` |

Kod jest w stylu C (funkcje i singletony z prefiksem, typy kończące się na `_t`), nawet w plikach `.cpp`. Moduły w `DSP/` to **statyczne singletony**, a nie obiekty.

## 5. Analizator widma (DSP)

### 5.1 Kontrakt wyjścia

`DSP_GetBandPower( DSP_Bands_t * )` oddaje:

- `channels`: 1 lub 2,
- `bands`: pasma **na kanał** (7, 16, 32 albo 64),
- `power[]`: moc pasma, układ `[kanał * bands + pasmo]`, skala **liniowa**.

Skala: sinus pełnej skali w środku pasma = 1,0 (0 dB). Wygląd (dB, opadanie, szczyty) robi UI.

**Wspólny limit:** `kanały × pasma ≤ 64`. Stała 64 występuje w czterech miejscach i musi być zawsze taka sama: `SPECTRUM_MAX_BANDS`, `DSP_SPECTRUM_MAX_BANDS`, `BARS_MAX`, `UI_SPEC_MAX_BARS`. Pilnują tego `static_assert` w `dsp_manage.cpp` i `audio_ui.cpp`. Przy 2 kanałach największa liczba pasm to 32. Zmiana liczby kanałów zmniejsza liczbę pasm, jeśli się nie mieści, a powrót do 1 kanału jej nie przywraca.

### 5.2 Pasma

Pasma są logarytmiczne od `SPECTRUM_F_LOW` (30 Hz) do `SPECTRUM_F_HIGH` (20 kHz); górna krawędź jest ograniczona do 0,49 fs. Krawędzie (−3 dB) i środek geometryczny `f0` liczy `spectrum.cpp` i przekazuje wszystkim silnikom. Każdy silnik dostaje te same pasma, więc można je zamieniać w locie.

### 5.3 Silniki

Przełączane w czasie pracy (`SPECTRUM_SetEngine`). Zmiana zeruje stan analizy. Domyślny: IIR-MR (`SPECTRUM_ENGINE_DEFAULT`).

| Silnik | Zasada | Mocne strony | Uwagi |
|---|---|---|---|
| FFT | jedna FFT, pasmo = suma mocy binów | stały koszt niezależny od liczby pasm | słaba rozdzielczość w basie; pasmo węższe niż bin jest interpolowane |
| IIR | bank pasmowych biquadów o stałym Q (domyślnie 2 sekcje na pasmo) | równa rozdzielczość w całym paśmie | koszt rośnie z liczbą pasm |
| IIR-MR | ten sam bank z decymacją oktawową | kilka razy tańszy przy 32 i 64 pasmach | |

#### Silniki IIR (`spectrum_iir.cpp`, `spectrum_iir_mr.cpp`)

Wspólne klocki leżą w `spectrum_biquad.*` (tylko `<math.h>`, wszystko w `float`, forma transponowana II, współczynniki projektowane w `double`):

- `SpectrumBp_*`: sekcja pasmowa o 3 współczynnikach (`b1 = 0`, `b2 = −b0`). Projekt: analogowy prototyp `H(s) = BW·s / (s² + BW·s + W0²)` z pre-warpingiem krawędzi (`W = tan( π·f / fs )`) i transformacją dwuliniową, wzmocnienie 1,0 w środku pasma. `SpectrumBp_CascadeScale` poszerza sekcje tak, żeby krawędzie −3 dB **kaskady** wypadły dokładnie w `f_lo` i `f_hi`, i podaje stosunek szerokości szumowej do pasma −3 dB (tablica dla 1..4 sekcji), z którego silniki liczą skalę wyjścia.
- `SpectrumBq_Run`: sekcja ogólna (5 współczynników), używana przez decymator w IIR-MR.
- `SpectrumEnv_Run`: detektor energii `e = beta·e + alpha·y²`.
- Zabezpieczenie przed liczbami zdenormalizowanymi: stany poniżej 1e-15 są zerowane po każdym przebiegu (w ciszy zanikają wykładniczo).
- Plik ma `#pragma GCC optimize( "O3" )`, bo Arduino buduje z `-Os`, a pętle filtrów są gorące. Przy przeniesieniu na inną platformę wystarczy zadbać o optymalizację tych trzech pętli (`SpectrumBp_Run`, `SpectrumBq_Run`, `SpectrumEnv_Run`).

- **Pasmo** to kaskada `SPECTRUM_IIR_SECTIONS` (1..4, domyślnie 2) identycznych pasmowych biquadów (transformacja dwuliniowa z pre-warpingiem krawędzi pasma). Współczynniki liczone są w `double` przy konfiguracji, wykonanie jest w `float`.
- **Detektor energii:** `y²` wygładzone filtrem jednobiegunowym o stałej czasowej `tau = max( SPECTRUM_IIR_TAU_MIN_MS, SPECTRUM_IIR_TAU_CYCLES / f0 )`, czyli co najmniej 10 ms, a dla pasm poniżej 100 Hz dłuższej.
- **Rozdzielczość** jest stała względnie (stałe Q) i niezależna od długości okna: bas ma tyle samo rozdzielczości co soprany. Ceną jest opóźnienie rosnące ku basowi (ok. Q / (π · f0)).
- **Skala:** wzmocnienie dobrane tak, żeby szum biały dawał ten sam poziom co suma mocy binów FFT (uwzględnia ENBW okna Hanna 1,5, czyli +1,76 dB). Sinus w środku pasma wypada przy 2 sekcjach ok. 0,9 dB poniżej FFT. `SPECTRUM_IIR_TRIM_DB` koryguje poziom całego silnika.
- **Każda próbka jest potrzebna** (filtry mają stan). Gdy zaległość przekracza pół bufora kołowego (`SPECTRUM_RING_SIZE / 2`, czyli 4096 próbek) albo producent nadpisał dane w trakcie liczenia, stan jest zerowany, a ramka pomijana.
- **Kanały:** współczynniki wspólne, stan osobny dla każdego kanału; koszt rośnie z łączną liczbą pasm.
- **IIR-MR (decymacja oktawowa):** te same pasma i filtry, ale pasmo liczone jest na strumieniu zdecymowanym tak głęboko, jak pozwala jego górna krawędź (stopień `k` pracuje z `fs / 2^k`; pasmo trafia do najgłębszego stopnia, w którym `f_hi ≤ 0,40 · fs_k`, maks. 10 stopni). Przejście ze stopnia na stopień to filtr eliptyczny 6. rzędu (3 sekcje, przepuszcza do 0,2 `fs_k`, tłumi powyżej 55 dB od 0,3 `fs_k`) i odrzucenie co drugiej próbki (faza decymacji jest pamiętana między wywołaniami). Koszt pasma maleje 2 × na każdy stopień w dół, a filtr basowy pracuje przy rozsądnym stosunku `f0 / fs_k`, co poprawia numerykę `float`.

Zmierzone czasy (µs na hop, 1 kanał, wersje sprzed zmian stereo): FFT ok. 3800; IIR: 7 pasm 1645, 16 pasm 3591, 32 pasma 7484, 64 pasma 14795; IIR-MR: 7 pasm 1150, 16 pasm 1724, 32 pasma 2625, 64 pasma 4699. Budżet jednego hopa to hop/fs (ok. 21,3 ms przy 48 kHz). Czas pokazuje ekran STAT (`DSP_SpectrumGetUs()`), uśredniony w oknie 0,5 s i znormalizowany do 1024 próbek.

W trybie 2 kanałów koszt rośnie proporcjonalnie do liczby kanałów. Tryby L i R na ekranie liczą **oba** kanały, bo DSP nie liczy pojedynczego kanału stereo; tryb wybiera tylko to, co jest rysowane.

### 5.4 Okno FFT i hop

Okno FFT jest ustawieniem czasu pracy: 1024, 2048 lub 4096 próbek (`SPECTRUM_SetFftWindow`). `SPECTRUM_FFT_N` (4096) zostaje oknem maksymalnym: od niego zależą rozmiary buforów, a tablice sin/cos liczone dla 4096 punktów obsługują krótsze okna z krokiem N/n (pamięć się nie zwiększa). Okno dotyczy tylko silnika FFT.

Każda ramka to FFT z **pełnych `n` ostatnich próbek** (okno Hanna), a nowa ramka powstaje co hop nowych próbek, czyli kolejne okna nakładają się. Hop jest przypisany do okna:

| Okno | Czas @ 44,1 kHz | Szerokość binu | Hop | Nakładanie |
|---|---|---|---|---|
| 4096 | 93 ms | 10,8 Hz | 1024 | 75% |
| 2048 | 46 ms | 21,5 Hz | 1024 | 50% |
| 1024 | 23 ms | 43 Hz | 512 | 50% |

Hop wynosi `min( SPECTRUM_HOP, okno / 2 )` dla silnika FFT i `SPECTRUM_HOP` dla pozostałych silników.

Nakładanie okien **nie jest uśrednianiem**. Każda ramka jest liczona od zera. Wygładzenie w czasie pochodzi z długości okna (każdy bin to ważona suma całego okna) oraz z balistyki w `ui_bars`. Hop wpływa na płynność, a okno na rozdzielczość w częstotliwości i rozmycie w czasie.

Przy krótkim oknie najniższe pasma (30–60 Hz) są węższe niż jeden bin, więc ich wartości są zbliżone. Dlatego krótkie okno ma sens z mniejszą liczbą pasm. Nie jest to wymuszone w kodzie.

### 5.5 Znane własności (nie błędy)

- FFT sumuje moc binów w paśmie, więc czysty ton w środku pasma wychodzi ok. +1,8 dB względem 0 dB sinusa (suma mocy binów okna Hanna wynosi 1,5 mocy największego z nich). Silnik IIR jest wyskalowany tak, żeby szum biały dawał ten sam poziom co FFT.
- Silnik FFT ma ostre krawędzie pasm, więc przy materiale bez energii w górnych pasmach (np. powyżej 16 kHz) ostatnie słupki są puste, a silniki IIR, mające zbocza, pokazują tam trochę energii z sąsiednich pasm. Sweep i szum biały zapalają wszystkie słupki w każdym silniku.
- Skala słupka to −75…−15 dB względem sinusa pełnej skali, więc sygnał cichszy niż −75 dB w paśmie daje słupek zerowy.

## 6. Warstwa UI

### 6.1 `ui_bars`: wygląd słupków

Z mocy pasm (liniowo) robi: dB → skala 0..255 (`BARS_DB_TOP` = −15 dB, `BARS_DB_FLOOR` = −75 dB), natychmiastowe narastanie, opadanie słupka i kreskę szczytu. Opadanie i podtrzymanie liczone są z **rzeczywistego czasu** między ramkami, więc wygląd nie zależy od hopa ani od częstotliwości pętli. `BARS_MAX_DT_MS` (500 ms) ogranicza opadanie po długiej przerwie.

Dwa niezależne parametry, oba z listy wartości i bez zawijania:

| Parametr | Lista (ms z pełnej skali do zera) | Domyślnie |
|---|---|---|
| opadanie słupków (`BARS_FallStep`) | 1500, 1000, 700, 500, 350, 250, 150, 100 | 500 ms |
| opadanie szczytów (`BARS_PeakStep`) | **0 = brak szczytów**, 300, 500, 700, 1000, 1500, 2000, 3000 | 1500 ms |

Podtrzymanie szczytu to stała `BARS_PEAK_HOLD_MS` (460 ms). Przy 0 szczyty nie są liczone ani rysowane. Po każdej zmianie układu pasm (liczba pasm, kanały, silnik, okno) UI wywołuje `BARS_Reset()`.

### 6.2 `ui_oled`: ekran i warstwy

Wyświetlacz: SH1122 256×64, HW SPI (U8g2, pełny bufor), zegar SPI 8 MHz. Ekran składa się z dwóch warstw:

- **warstwa dolna:** STAT (liczniki i czasy, odświeżane co 200 ms) albo SPECTRUM (przy każdej nowej ramce widma); przełączane długim przytrzymaniem enkodera,
- **warstwa OSD:** panel z tytułem, wartością i paskiem (głośność, jasność, menu), znika po `UI_OSD_MS` (2000 ms), a okienko menu po `UI_MENU_MS` (5000 ms). W czasie OSD warstwa dolna jest przyciemniona: w trybie 2 prawdziwą szarością (4 bity na piksel, `UI_OSD_DIM_GRAY`), w trybie 1 rastrem szachownicy.

**Układ widma:**

| Tryb | Rysowanie |
|---|---|
| MONO | jedno widmo (miks), 64 px wysokości |
| L | lewy kanał, 64 px |
| R | prawy kanał, 64 px |
| L+R | dwa widma nad sobą: L w wierszach 0–29, R w 34–63, przerwa 4 px (`UI_SPEC_ROW_H` = 30) |

Słupki wypełniają całą szerokość 256 px; odstęp zależy od liczby słupków (4, 3, 2 lub 1 px).

### 6.3 `menu_oled`: ustawienia

Okienka na górnej warstwie przełączane klawiszami Up i Down (pierwsze naciśnięcie tylko otwiera ostatnio używane). Wartość zmienia enkoder. Gdy okienko jest zamknięte, enkoder zmienia głośność.

| # | Okienko | Wartości | Zawijanie |
|---|---|---|---|
| 1 | SPECTRUM BARS | 7 / 16 / 32 / 64 (tylko mieszczące się w limicie) | tak |
| 2 | CHANNELS | MONO / L / R / L+R | tak |
| 3 | BRIGHTNESS | 16 poziomów | nie |
| 4 | ANALYSIS | FFT / IIR / IIR-MR | tak |
| 5 | FFT WINDOW | 1024 / 2048 / 4096 (próbki i czas) | nie |
| 6 | SPECTRUM FALL | lista z 6.1 | nie |
| 7 | PEAK FALL | lista z 6.1, OFF = 0 | nie |

Kierunek enkodera w pozycjach 5–7: w prawo = większa wartość (dłuższe okno, wolniejsze opadanie), w lewo = mniejsza. Pasek rośnie razem z wartością.

Tryb kanałów żyje w `ui_oled` (`UI_SetSpecMode`), a liczbę kanałów DSP ustawia menu (`DSP_SpectrumSetChannels`). `audio_ui` dodatkowo pilnuje zgodności (MONO → 1 kanał, pozostałe → 2), więc stan jest poprawny także zaraz po starcie.

### 6.4 `audio_ui`: most

Co cykl `loop()` przekazuje do UI głośność (`UI_SetVolume`), limit startowy (`UI_SetGuard`), widmo (`BARS_Update` → `BARS_Get` → `UI_SetSpectrum`) i diagnostykę (`UI_SetDebug`). Widmo jest liczone tylko wtedy, gdy wyświetlany jest ekran SPECTRUM.

## 7. Wejścia i sterowanie

### 7.1 `input.*`: sprzęt wejść

Moduł jest czysto sprzętowy (piny, timer, debounce, IR) i nie zna reszty systemu. Jeden wspólny timer sprzętowy (przerwanie) próbkuje wszystko, a logikę wykonuje `INPUT_proc()` z `loop()`:

- co tick (tempo `F_INTERRUPTS` z IRMP): `irmp_ISR()`, czyli próbkowanie odbiornika IR,
- co `INPUT_ENC_DIV` ticków (ok. 2 kHz): `SimonEncoder::tick()`,
- co `INPUT_KEY_DIV` ticków (ok. 1 kHz): odczyt poziomów przycisków do jednej zmiennej bitowej.

`INPUT_proc()` obsługuje zdarzenia enkodera (`eventAnalyze`), wywołuje `tick()` obiektów OneButton i odbiera kody IR (IRMP, tylko protokół NEC). Dzięki temu wszystkie callbacki działają w wątku `loop()` i mogą bezpiecznie używać audio, OLED i USB.

| Element | Piny / parametry |
|---|---|
| enkoder | A = 5 (DT), B = 4 (CLK), HALF_STEP, bez akceleracji |
| przycisk enkodera | pin 6 |
| klawisz A (Right) | pin 16 |
| klawisz B (Up) | pin 17 |
| klawisz C (Left) | pin 7 |
| klawisz D (Down) | pin 15 |
| odbiornik IR | pin 41 (NEC) |

Klawisze są aktywne stanem niskim z włączonym podciąganiem.

**Enkoder (`SimonEncoder`, biblioteka lokalna):** dekoder kwadraturowy bez własnego timera. `tick()` jest wołane z przerwania (ok. 2 kHz): zmienia stan pinów A i B, a przejścia czyta z tablicy 16 stanów. Licznik podkroków zlicza przejścia do progu typu enkodera (`HALF_STEP` = 2 przejścia na zdarzenie) i zeruje się przy zmianie kierunku. Zdarzenia trafiają do licznika `_pending` modyfikowanego operacjami atomowymi, więc szybkie kręcenie nie gubi kroków między wywołaniami. `eventAnalyze()` (z `loop()`) pobiera ten licznik i wywołuje callback `OnRight` albo `OnLeft` z wartością zawsze dodatnią. Akceleracja (`ACCEL_LOW/MEDIUM/HIGH`) zależy od odstępu między zdarzeniami (poniżej 40 ms mnożnik 2 × tryb, poniżej 100 ms tryb / 2); w projekcie jest wyłączona (`ACCEL_OFF`), więc jeden obrót = jeden krok. Pin ustawiony na −1 wyłącza klawisz.

Pilot IR: każdy kod (`IR_CMD_A`…`IR_CMD_D` i warianty `_LONG`) wywołuje ten sam callback co odpowiedni klawisz. Kody w `input.h` są przykładowe: prawdziwe odczytuje się z logu na Serial (`IR_DEBUG_SERIAL`). `IR_REPEAT_x` = 1 pozwala trzymanemu klawiszowi pilota powtarzać akcję.

### 7.2 `controls.*`: przypisanie akcji

`CONTROLS_Init()` rejestruje callbacki w `input.*` (wołać po `INPUT_init()` i `AUDIO_Init()`).

| Wejście | Akcja |
|---|---|
| obrót enkodera | gdy okienko menu jest otwarte: zmiana wartości w okienku (`MENU_Rotate`); inaczej głośność (`AUDIO_VolumeStep`) |
| klik enkodera | mute urządzenia (`AUDIO_MuteToggle`) |
| długie przytrzymanie enkodera | ekran STAT ↔ SPECTRUM (`UI_NextScreen`) |
| dwuklik enkodera | zwolnienie limitu startowego; rejestrowany tylko przy aktywnym limicie, bo dwuklik opóźnia pojedynczy klik |
| klawisz A (Right) i pilot | następny utwór (`AUDIO_MEDIA_NEXT`) |
| klawisz C (Left) i pilot | poprzedni utwór (`AUDIO_MEDIA_PREV`) |
| klawisz B (Up) i pilot | poprzednie okienko menu (`MENU_Up`); gdy zamknięte, otwiera ostatnio używane |
| klawisz D (Down) i pilot | następne okienko menu (`MENU_Down`) |

Długie naciśnięcia klawiszy A–D są obsługiwane przez `input.*`, ale `controls.cpp` ich dziś nie podłącza (druga funkcja klawiszy jest wolna).

## 8. Fork „tylko karta dźwiękowa”

Wersja o mniejszych zasobach, która przenosi dźwięk z USB do DAC po I2S bez analizatora widma i bez interfejsu UI. Zostają w niej enkoder, przyciski i IRMP (sterowanie głośnością).

**Reguły:**

1. **Fork to niezależna kopia czystej wersji.** Nie jest synchronizowany z głównym projektem. Poprawki do niego wprowadza się osobno, w razie potrzeby.
2. **Warianty budowania robi się przez przeniesienie katalogów poza `src/`**, a nie przez `#if` wokół modułów. Przenoszenie katalogów robi użytkownik; dzięki temu kod jest czytelniejszy i nie ma dodatkowych zagnieżdżeń.
3. **Dlatego kierunek zależności z sekcji 2 jest twardą regułą:** `audio_manage.*`, `AUDIO/` i `HARDWARE/` nigdy nie mogą zależeć od `DSP/` ani od UI. Zaczepem jest tylko `AUDIO_SetTap()`.
4. `input.*` nie zna reszty systemu, więc zostaje w forku bez zmian.
5. Główną gałąź projektu zamierza się kiedyś przemianować na nazwę adekwatną do projektu.

**Co zdjąć przy usuwaniu DSP i UI:**

| Miejsce | Zmiana w forku |
|---|---|
| katalog `src/DSP/`, `src/dsp_manage.*` | usunąć |
| `ui_oled.*`, `ui_bars.*`, `menu_oled.*`, `audio_ui.*` (katalog szkicu) | usunąć |
| szkic `.ino` | zdjąć dołączenia `src/dsp_manage.h`, `ui_oled.h`, `audio_ui.h` oraz wywołania `UI_Init`, `DSP_Init`, `AUDIOUI_Init`, `DSP_Poll`, `AUDIOUI_Sync`, `UI_Loop`; zostają `INPUT_*`, `AUDIO_*`, `CONTROLS_Init` i diagnostyka |
| `controls.cpp` | usunąć `#include` dla `dsp_manage.h`, `ui_oled.h`, `menu_oled.h`; obrót enkodera wołać wprost `AUDIO_VolumeStep`; długie przytrzymanie enkodera i klawisze B, D bez akcji (lub nowe przypisanie); `UI_SetGuard` po dwukliku usunąć (limit nadal zwalnia `AUDIO_GuardRelease`) |

Git: polecenia podawać ze ścieżkami względem korzenia repozytorium (w korzeniu jest `.git`, `ESP32-S3_usb_sound_card/` z projektem, `docs/` i `libraries/`).

## 9. Przeniesienie na inną platformę (np. STM32H723)

Co przenosi się bez zmian (zgodnie z regułami z sekcji 2): `AUDIO/fifo.*` i `AUDIO/drift.*` (tylko `<stdint.h>`, `<string.h>`; bariera pamięci `FIFO_MEMORY_BARRIER` domyślnie używa `__sync_synchronize()` z GCC/Clang, na innym kompilatorze trzeba ją zdefiniować), `audio_manage.cpp` (matematyka i `<string.h>`, bez peryferiów), `DSP/` (statyczne singletony, tylko `<math.h>` i `<stdint.h>`), `dsp_manage.cpp` (wymaga `AUDIO_SetTap`, `AUDIO_GetSampleRate`, `AUDIO_TimeUs`), `ui_bars.*`, logika `menu_oled.*` i `controls.*`.

Co trzeba napisać od nowa lub dostosować:

| Element | Uwaga |
|---|---|
| `HARDWARE/audio_out_*.cpp` | nowy plik z tymi samymi funkcjami (`AUDIO_OUT_Init`, `Start`, `SetSampleRate`, `GetErrors`); sam decyduje, jak pompuje dane (zadanie RTOS, DMA, przerwanie); `fill()` nie blokuje i zawsze wypełnia blok |
| `HARDWARE/audio_clock_*.cpp` | `AUDIO_CLOCK_TimeUs()` |
| źródło USB | nowy obiekt `AUDIO_Source_t` na stosie USB audio platformy (głośność, mute i klawisze multimedialne są opcjonalne: `NULL` = brak obsługi) |
| `input.cpp` | timer sprzętowy i ISR (`hw_timer_t`, `IRAM_ATTR`), IRMP, piny |
| `ui_oled.cpp` | U8g2 i SPI zamienić na sterownik platformy; przyciemnianie w trybie 2 korzysta z sekwencji poleceń SH1122 |
| `loop()` i model wykonania | tap ma nadal tylko kopiować; kolejka tap → loop jest jednowątkowa po stronie odczytu |

Do rozważenia przy mocniejszym sprzęcie: silnik CQT jako czwarty silnik implementujący tylko `SpectrumEngine_t` (kontrakt w `spectrum_engine.h`), CMSIS-DSP dla FFT.

## 10. Rozszerzenia: gdzie co dopisać

| Zadanie | Miejsce |
|---|---|
| nowy silnik analizy (np. CQT) | nowy plik w `DSP/`, struktura `SpectrumEngine_t` (`name`, `init`, `configure`, `reset`, `process`), wpis w `kEngine[]` i `SPECTRUM_ENGINE_COUNT` w `spectrum.cpp` / `spectrum.h` |
| nowy ekran dolnej warstwy | `UI_Screen_t` w `ui_oled.h`, funkcja rysująca w `ui_oled.cpp` |
| nowe okienko menu | `Menu_t` w `menu_oled.cpp` plus przypadki w `menuShow()` i `menuChange()`, opis w `menu_oled.h` |
| zmiana wyglądu słupków | tylko `ui_bars.*` i `ui_oled.cpp` (DSP bez zmian) |
| zmiana limitu pasm | cztery stałe z sekcji 5.1 jednocześnie |
| nowe źródło audio (np. Wi-Fi) | nowy plik w `HARDWARE/` z obiektem `AUDIO_Source_t` (jak `AUDIO_SRC_USB`), wybór przez `AUDIO_SOURCE` w `audio_config.h`; źródło dostaje `AUDIO_Push` w `init()` i jest jedynym producentem FIFO |
| nowa częstotliwość próbkowania | `AUDIO_SAMPLE_RATE_LIST` i `AUDIO_SAMPLE_RATE_MAX` w `audio_config.h` (dla USB UAC1 sufit to 48 kHz) |
| nowa akcja wejścia | callback w `controls.cpp` i rejestracja w `CONTROLS_Init()`; kody pilota w `input.h` |

## 11. Stan prac

| Etap | Zakres | Stan |
|---|---|---|
| 1 | podział na AUDIO / DSP / HARDWARE, `audio_manage` + `dsp_manage`, zegar jako kontrakt, `SPECTRUM_` | wgrany, działa |
| 2a | kolejka tap → loop, jednowątkowy analizator, polityka zaległości | wgrany, działa |
| 2b | kanały w analizatorze (1 = mono, 2 = L i R) | wgrany, działa |
| 2c | balistyka w `ui_bars`, `DSP_GetBandPower`, bars → bands | wgrany, działa |
| 3a | tryb kanałów MONO / L / R / L+R na ekranie, opadanie szczytów (PEAK FALL) | wgrany, działa |
| 3b | okno FFT jako ustawienie (1024 / 2048 / 4096), hop zależny od okna | sprawdzony testem na komputerze; test na sprzęcie do potwierdzenia |

Planowane: poprawki kosmetyczne po dogłębnym zrozumieniu kodu, potem przeniesienie na STM32H723.

---

## 12. Środowisko budowania

Wersje i ustawienia, dla których projekt jest sprawdzony (na podstawie zrzutu ustawień Arduino IDE):

| Element | Wartość |
|---|---|
| Arduino IDE | 2.3.6, wersja portable |
| rdzeń ESP32 (arduino-esp32) | 3.3.12 (USB UAC1 z sufitem 48 kHz, patrz sekcja 3) |
| płytka (ustawienie IDE) | ESP32S3 Dev Module |
| sprzęt | ESP32-S3, 16 MB Flash, 8 MB PSRAM, PCM5102 (I2S), OLED SH1122 |
| CPU | 240 MHz (WiFi) |
| Flash | QIO 80 MHz, 16 MB |
| partycje | 16M Flash (3 MB APP / 9,9 MB FATFS) |
| PSRAM | **Disabled** w ustawieniach IDE: projekt jej nie używa (cały RAM to SRAM wewnętrzny) |
| rdzenie | Arduino Runs On: Core 1, Events Run On: Core 1; zadanie wyjścia I2S też na rdzeniu 1 (`AUDIO_I2S_TASK_CORE`) |
| USB CDC On Boot | Disabled; `Serial` (115200) to UART0 |
| USB Mode (wg zrzutu) | Hardware CDC and JTAG |
| Core Debug Level | None |
| wgrywanie | UART0 / Hardware CDC, 921600 |

Biblioteki: U8g2 (wyświetlacz), OneButton (przyciski), IRMP (pilot), `USBHIDConsumerControl` (z rdzenia), **`USBAudioCard` w lokalnej, zmodyfikowanej kopii** (rozszerzona lista częstotliwości w deskryptorze UAC1, patrz sekcja 3) i `SimonEncoder` (lokalna, w katalogu szkicu).

Uwagi:

- Wszystko, co działa w czasie rzeczywistym (`loop()`, zdarzenia, wyjście I2S), jest na rdzeniu 1, więc o tym, kto kiedy zdąży, rozstrzygają priorytety zadań FreeRTOS. Rdzeń 0 pozostaje wolny dla stosu systemowego.
- Zmiana rdzenia arduino-esp32 na inną wersję wymaga sprawdzenia lokalnej kopii `USBAudioCard`: jest zmodyfikowana względem oryginału, więc nie zaktualizuje się sama razem z rdzeniem.

## Zakres dokumentu

Dokument opisuje moduły na podstawie ich kodu i nagłówków. Nie obejmuje:

- bibliotek zewnętrznych, w tym lokalnej kopii `USBAudioCard` (U8g2, OneButton, IRMP),
- obiektu `NatywneUSB`, którego szkic używa do komunikatu „System ready” (nie jest zdefiniowany w dostarczonych plikach; pochodzi z rdzenia lub z wariantu płytki).
