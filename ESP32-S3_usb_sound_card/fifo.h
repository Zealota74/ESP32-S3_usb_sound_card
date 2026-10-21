/*
 * fifo.h - bufor kołowy bajtów typu SPSC (jeden producent, jeden konsument)
 *
 * Moduł jest niezależny od sprzętu i od USB/I2S. Wymaga tylko <stdint.h>
 * i <string.h>, więc można go przenieść na inną architekturę.
 *
 * Zasady użycia:
 *  - rozmiar bufora musi być potęgą dwójki,
 *  - FIFO_Write() wywołuje wyłącznie producent (np. callback USB),
 *  - FIFO_Read() wywołuje wyłącznie konsument (np. task I2S),
 *  - producent i konsument mogą działać w różnych wątkach, przerwaniach
 *    lub na różnych rdzeniach bez blokad (indeksy wr/rd są zapisywane
 *    każdy przez jedną stronę),
 *  - wszystkie transfery są zaokrąglane w dół do pełnych ramek audio.
 *
 * Uwaga przenośność: FIFO_MEMORY_BARRIER() domyślnie używa
 * __sync_synchronize() (GCC/Clang). Na innym kompilatorze zdefiniuj
 * własną barierę pamięci przed dołączeniem tego pliku.
 */
#ifndef FIFO_H
#define FIFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FIFO_MEMORY_BARRIER
  #if defined(__GNUC__)
    #define FIFO_MEMORY_BARRIER() __sync_synchronize()
  #else
    #define FIFO_MEMORY_BARRIER() ((void)0)
  #endif
#endif

typedef struct {
  uint8_t *buf;
  uint32_t size;          /* potęga dwójki */
  uint32_t mask;          /* size - 1 */
  uint32_t frame_bytes;   /* rozmiar jednej ramki audio w bajtach */
  volatile uint32_t wr;   /* licznik zapisanych bajtów (zapętla się swobodnie) */
  volatile uint32_t rd;   /* licznik odczytanych bajtów (zapętla się swobodnie) */
  volatile uint32_t overruns; /* ile razy zapis został skrócony z braku miejsca */
} FIFO_t;

/* Zwraca 1 przy sukcesie, 0 gdy parametry są błędne (size nie jest potęgą 2). */
int      FIFO_Init(FIFO_t *f, uint8_t *storage, uint32_t size, uint32_t frame_bytes);

/* Producent. Zwraca liczbę zapisanych bajtów (może być mniej niż bytes). */
uint32_t FIFO_Write(FIFO_t *f, const uint8_t *data, uint32_t bytes);

/* Konsument. Zwraca liczbę odczytanych bajtów (może być mniej niż bytes). */
uint32_t FIFO_Read(FIFO_t *f, uint8_t *data, uint32_t bytes);

uint32_t FIFO_Available(const FIFO_t *f);   /* bajty do odczytu */
uint32_t FIFO_GetOverruns(const FIFO_t *f);

#ifdef __cplusplus
}
#endif

#endif /* FIFO_H */
