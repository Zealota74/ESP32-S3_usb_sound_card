/*
 * fifo.cpp - implementacja bufora kołowego SPSC (kod w stylu C).
 * Plik można bez zmian przemianować na fifo.c.
 */
#include "fifo.h"
#include <string.h>

int FIFO_Init( FIFO_t * f, uint8_t * storage, uint32_t size, uint32_t frame_bytes ) {
	if ( f == NULL || storage == NULL || frame_bytes == 0 ) return 0;
	if ( size == 0 || ( size & ( size - 1 ) ) != 0 ) return 0;

	f->buf = storage;
	f->size = size;
	f->mask = size - 1;
	f->frame_bytes = frame_bytes;
	f->wr = 0;
	f->rd = 0;
	f->overruns = 0;
	return 1;
}

uint32_t FIFO_Write( FIFO_t * f, const uint8_t * data, uint32_t bytes ) {
	uint32_t wr = f->wr;
	uint32_t rd = f->rd;
	uint32_t room, idx, first;

	FIFO_MEMORY_BARRIER();

	bytes -= bytes % f->frame_bytes;
	if ( bytes == 0 ) return 0;

	room = f->size - ( wr - rd );
	if ( bytes > room ) {
		bytes = room - ( room % f->frame_bytes );
		f->overruns++;
		if ( bytes == 0 ) return 0;
	}

	idx = wr & f->mask;
	first = f->size - idx;
	if ( first > bytes ) first = bytes;

	memcpy( &f->buf[ idx ], data, first );
	if ( bytes > first ) memcpy( f->buf, data + first, bytes - first );

	FIFO_MEMORY_BARRIER(); /* dane muszą być widoczne przed przesunięciem wr */
	f->wr = wr + bytes;
	return bytes;
}

uint32_t FIFO_Read( FIFO_t * f, uint8_t * data, uint32_t bytes ) {
	uint32_t rd = f->rd;
	uint32_t wr = f->wr;
	uint32_t used, idx, first;

	FIFO_MEMORY_BARRIER();

	bytes -= bytes % f->frame_bytes;
	used = wr - rd;
	if ( bytes > used ) bytes = used - ( used % f->frame_bytes );
	if ( bytes == 0 ) return 0;

	idx = rd & f->mask;
	first = f->size - idx;
	if ( first > bytes ) first = bytes;

	memcpy( data, &f->buf[ idx ], first );
	if ( bytes > first ) memcpy( data + first, f->buf, bytes - first );

	FIFO_MEMORY_BARRIER(); /* odczyt musi się zakończyć przed zwolnieniem miejsca */
	f->rd = rd + bytes;
	return bytes;
}

void FIFO_Flush( FIFO_t * f ) {
	uint32_t wr = f->wr;

	FIFO_MEMORY_BARRIER();
	f->rd = wr; /* wr jest wielokrotnością ramki, więc rd też */
}

uint32_t FIFO_Available( const FIFO_t * f ) {
	return f->wr - f->rd;
}

uint32_t FIFO_GetOverruns( const FIFO_t * f ) {
	return f->overruns;
}
