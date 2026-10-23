/*
 * ui_oled.cpp - rysowanie na SH1122 (U8g2, pełny bufor, HW SPI)
 *
 * Kolejność rysowania w każdej klatce (render()):
 *   1) warstwa dolna (STAT albo SPECTRUM),
 *   2) jeśli OSD aktywne: przyciemnienie warstwy dolnej + panel OSD,
 *   3) wysłanie bufora do wyświetlacza.
 *
 * Bufor U8g2 dla SH1122 jest poziomy: wiersz po wierszu, 32 bajty na wiersz (256 px), bit 7 =
 * piksel po lewej (u8g2_ll_hvline_horizontal_right_lsb). Sterownik U8g2 zamienia każdą parę bitów
 * na bajt 4-bitowy (0x00, 0x0F, 0xF0, 0xFF), czyli tylko dwa poziomy szarości. Przy
 * UI_OSD_DIM_MODE 2 wysyłamy dane sami, tą samą sekwencją poleceń co sterownik (adres wiersza
 * 0xB0, adres kolumny, dane), ale z dowolnym poziomem szarości.
 */
#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_oled.h"

static U8G2_SH1122_256X64_F_4W_HW_SPI u8g2( U8G2_R2, CS_OLED, DC_OLED, RESET_OLED );

/* głośność i diagnostyka */
static volatile int s_pct = 0;
static volatile int s_max = 100;
static volatile int s_mute = 0;
static volatile int s_hostDb = 0;
static volatile int s_guard = 0;
static volatile float s_guardDb = 0.0f;
static volatile uint32_t s_dbgBlock = 0, s_dbgFifo = 0, s_dbgUsb = 0, s_dbgFftUs = 0;
static volatile uint32_t s_drawUs = 0;           /* czas ostatniego sendBuffer() */

/* warstwa dolna */
static volatile UI_Screen_t s_screen = UI_SCREEN_DEFAULT;
static uint8_t s_specLevel[ UI_SPEC_MAX_BARS ], s_specPeak[ UI_SPEC_MAX_BARS ];
static int s_specBars = 16;
static volatile int s_dirty = 1;

/* jasność */
static int s_bright = UI_BRIGHT_DEFAULT;

/* bufor warstwy dolnej (kopia bufora U8g2) do składania klatki w trybie szarości */
#define BUF_ROW_BYTES  32                         /* 256 px / 8 */
#define BUF_ROWS       64
#define BUF_BYTES      ( BUF_ROW_BYTES * BUF_ROWS )
#if UI_OSD_DIM_MODE == 2
static uint8_t s_baseBuf[ BUF_BYTES ];
#endif

/* panel OSD: położenie i rozmiar (px, px i pw podzielne przez 8, żeby panel zajmował całe bajty) */
#define OSD_PX  24
#define OSD_PY  6
#define OSD_PW  208
#define OSD_PH  52
#if ( OSD_PX % 8 ) != 0 || ( OSD_PW % 8 ) != 0
  #error "OSD_PX i OSD_PW muszą być podzielne przez 8"
#endif

/* OSD */
static char s_osdTitle[ 20 ] = "";
static char s_osdValue[ 16 ] = "";
static int s_osdPos = 0, s_osdMax = 0;
static int s_osdVolume = 0;                      /* 1 = OSD głośności (dopisek LIMIT) */
static bool s_osdOn = false;
static uint32_t s_osdUntil = 0;

// =============================================================================================
static void applyBrightness( void ) {
	/* krzywa kwadratowa (oko reaguje nieliniowo); poziom 0 = kontrast 4, ostatni = 255 */
	int top = UI_BRIGHT_LEVELS - 1;
	int c = 4 + ( s_bright * s_bright * 251 ) / ( top * top );

	u8g2.setContrast( (uint8_t)c );
}
// ---------------------------------------------------------------------------------------------
static bool s_osdMenu = false;
// ---------------------------------------------------------------------------------------------
static void osdSet( const char * title, const char * value, int pos, int max, int isVolume, int isMenu ) {
	strncpy( s_osdTitle, title, sizeof( s_osdTitle ) - 1 );
	s_osdTitle[ sizeof( s_osdTitle ) - 1 ] = '\0';
	strncpy( s_osdValue, value, sizeof( s_osdValue ) - 1 );
	s_osdValue[ sizeof( s_osdValue ) - 1 ] = '\0';
	s_osdPos = pos;
	s_osdMax = max;
	s_osdVolume = isVolume;

	s_osdMenu = isMenu;

	s_osdUntil = millis() + UI_OSD_MS;
	s_osdOn = true;
	s_dirty = 1;
}
// ---------------------------------------------------------------------------------------------
void UI_ShowMenu( const char * title, const char * value, int pos, int max ) {
	osdSet( title, value, pos, max, 0, 1 );
}
// ---------------------------------------------------------------------------------------------
int UI_MenuOpen( void ) {
	return s_osdOn && s_osdMenu && (int32_t)( millis() - s_osdUntil ) < 0;
}
// ---------------------------------------------------------------------------------------------
static void osdShowVolume( void ) {
	char v[ 16 ];
	if ( s_mute ) snprintf( v, sizeof( v ), "MUTE" );
	else snprintf( v, sizeof( v ), "%d", (int)s_pct );
	osdSet( "VOLUME", v, s_mute ? 0 : (int)s_pct, (int)s_max, 1, 0 );
}
// ---------------------------------------------------------------------------------------------
void UI_Init( void ) {
	SPI.begin( SPI_SCK_OLED, SPI_MISO_OLED, SPI_MOSI_OLED );
	u8g2.begin();
#ifdef OLED_SPI_HZ
  u8g2.setBusClock(OLED_SPI_HZ);
#endif
	applyBrightness();
	s_dirty = 1;
}
// ---------------------------------------------------------------------------------------------
void UI_SetBrightness( int level ) {
	char v[ 16 ];

	if ( level < 0 ) level = 0;
	if ( level > UI_BRIGHT_LEVELS - 1 ) level = UI_BRIGHT_LEVELS - 1;
	if ( level != s_bright ) {
		s_bright = level;
		applyBrightness();
	}
	snprintf( v, sizeof( v ), "%d/%d", s_bright + 1, UI_BRIGHT_LEVELS ); /* OSD także na końcach zakresu */
	osdSet( "BRIGHTNESS", v, s_bright, UI_BRIGHT_LEVELS - 1, 0, 0 );
}
// ---------------------------------------------------------------------------------------------
void UI_BrightnessStep( int steps ) {
	UI_SetBrightness( s_bright + steps );
}
// ---------------------------------------------------------------------------------------------
int UI_GetBrightness( void ) {
	return s_bright;
}
// ---------------------------------------------------------------------------------------------
void UI_SetVolume( int value, int max, int mute ) {
	if ( max < 1 ) max = 1;
	if ( value < 0 ) value = 0;
	if ( value > max ) value = max;
	if ( value != s_pct || max != s_max || mute != s_mute ) { /* każda zmiana pokazuje OSD */
		s_pct = value;
		s_max = max;
		s_mute = mute;
		osdShowVolume();
	}
}
// ---------------------------------------------------------------------------------------------
void UI_SetGuard( int active, float limit_db ) {
	if ( active != s_guard || limit_db != s_guardDb ) {
		s_guard = active;
		s_guardDb = limit_db;
		osdShowVolume();
	}
}
// ---------------------------------------------------------------------------------------------
void UI_SetDebug( uint32_t audioBlock, uint32_t fifo, uint32_t usbPackets, uint32_t fftUs ) {
	s_dbgBlock = audioBlock;
	s_dbgFifo = fifo;
	s_dbgUsb = usbPackets;
	s_dbgFftUs = fftUs;
}
// ---------------------------------------------------------------------------------------------
void UI_SetHostDb( int host_db ) {
	s_hostDb = host_db;
}
// ---------------------------------------------------------------------------------------------
void UI_NextScreen( void ) {
	s_screen = (UI_Screen_t)( ( (int)s_screen + 1 ) % (int)UI_SCREEN_COUNT );
	s_dirty = 1;
}
// ---------------------------------------------------------------------------------------------
UI_Screen_t UI_GetScreen( void ) {
	return s_screen;
}
// ---------------------------------------------------------------------------------------------
void UI_SetSpectrum( const uint8_t * level, const uint8_t * peak, int bars ) {
	if ( bars < 1 ) bars = 1;
	if ( bars > UI_SPEC_MAX_BARS ) bars = UI_SPEC_MAX_BARS;
	memcpy( s_specLevel, level, (size_t)bars );
	memcpy( s_specPeak, peak, (size_t)bars );
	s_specBars = bars;
	s_dirty = 1;
}
// ---------------------------------------------------------------------------------------------
void UI_ShowOsd( const char * title, const char * value, int pos, int max ) {
	osdSet( title, value, pos, max, 0, 0 );
}
// ---------------------------------------------------------------------------------------------
/* sendBuffer() z pomiarem czasu (do ekranu STAT) */
static void flushDisplay( void ) {
	uint32_t t0 = micros();

	u8g2.sendBuffer();
	s_drawUs = micros() - t0;
}
// =============================================================================================
/* ---- warstwa dolna: STAT ---- */
static void drawStat( void ) {
	char txt[ 32 ];

	u8g2.setFont( u8g2_font_6x12_tf );

	snprintf( txt, sizeof( txt ), "BLK %lu", (unsigned long)s_dbgBlock );
	u8g2.drawStr( 0, 12, txt );
	snprintf( txt, sizeof( txt ), "FIFO %lu", (unsigned long)s_dbgFifo );
	u8g2.drawStr( 0, 26, txt );
	snprintf( txt, sizeof( txt ), "USB %lu", (unsigned long)s_dbgUsb );
	u8g2.drawStr( 0, 40, txt );
	snprintf( txt, sizeof( txt ), "VOL %d (host %d dB)%s", (int)s_pct, (int)s_hostDb, s_mute ? " MUTE" : "" );
	u8g2.drawStr( 0, 54, txt );

	snprintf( txt, sizeof( txt ), "FFT %lu us", (unsigned long)s_dbgFftUs );
	u8g2.drawStr( 140, 12, txt );
	snprintf( txt, sizeof( txt ), "OLED %lu us", (unsigned long)s_drawUs );
	u8g2.drawStr( 140, 26, txt );
}
// ---------------------------------------------------------------------------------------------
/* ---- warstwa dolna: SPECTRUM (słupki na całej szerokości 256 px, wysokość 64 px) ---- */
static void drawSpectrum( void ) {
	int n = s_specBars;
	int gap = ( n <= 8 ) ? 4 : ( ( n <= 16 ) ? 3 : ( ( n <= 32 ) ? 2 : 1 ) ); /* 64 słupki: 3 px + 1 px przerwy */
	int bw = ( 256 - ( n - 1 ) * gap ) / n;
	int x0 = ( 256 - ( n * bw + ( n - 1 ) * gap ) ) / 2;

	for ( int b = 0; b < n; b++ ) {
		int x = x0 + b * ( bw + gap );
		int h = (int)s_specLevel[ b ] * 64 / 255;
		int py = 63 - (int)s_specPeak[ b ] * 63 / 255;

		u8g2.drawBox( x, 63, bw, 1 );                  /* linia bazowa: widać miejsce pustych słupków */
		if ( h > 0 ) u8g2.drawBox( x, 64 - h, bw, h );
		if ( s_specPeak[ b ] > 0 ) {
			if ( py > 62 ) py = 62;
			u8g2.drawBox( x, py, bw, 2 );              /* kreska szczytu */
		}
	}
}
// ---------------------------------------------------------------------------------------------
/* Przyciemnienie bufora rastrem szachownicy (zostaje co drugi piksel = ok. 50 % jasności).
 * Bufor poziomy: bit 7 = piksel x%8 == 0. Piksel zostaje, gdy (x + y) jest parzyste:
 * wiersz parzysty -> kolumny parzyste (0xAA), wiersz nieparzysty -> kolumny nieparzyste (0x55). */
static void dimBufferChecker( void ) {
	uint8_t * p = u8g2.getBufferPtr();

	for ( int y = 0; y < BUF_ROWS; y++ ) {
		uint8_t mask = ( y & 1 ) ? 0x55 : 0xAA;

		for ( int bx = 0; bx < BUF_ROW_BYTES; bx++ ) p[ y * BUF_ROW_BYTES + bx ] &= mask;
	}
}
// ---------------------------------------------------------------------------------------------
/* ---- warstwa OSD: panel z tytułem, wartością i paskiem ---- */
static void drawOsd( void ) {
	const int px = OSD_PX, py = OSD_PY, pw = OSD_PW, ph = OSD_PH;
	char title[ 40 ];
	int vw;

	if ( s_osdVolume && s_guard ) snprintf( title, sizeof( title ), "%s  LIMIT %.0f", s_osdTitle, (double)s_guardDb );
	else snprintf( title, sizeof( title ), "%s", s_osdTitle );

	u8g2.setDrawColor( 0 );
	u8g2.drawBox( px, py, pw, ph );                    /* czarne tło panelu */
	u8g2.setDrawColor( 1 );
	u8g2.drawFrame( px, py, pw, ph );

	u8g2.setFont( u8g2_font_6x12_tf );
	u8g2.drawStr( px + 8, py + 14, title );

	u8g2.setFont( u8g2_font_logisoso20_tf );
	vw = u8g2.getStrWidth( s_osdValue );
	if ( s_osdMax > 0 ) {
		int pos = s_osdPos;
		int fill;

		if ( pos < 0 ) pos = 0;
		if ( pos > s_osdMax ) pos = s_osdMax;
		u8g2.drawStr( px + pw - 8 - vw, py + 30, s_osdValue );
		u8g2.drawFrame( px + 8, py + 38, pw - 16, 8 );
		fill = (int)( (long)pos * ( pw - 20 ) / s_osdMax );
		if ( fill > 0 ) u8g2.drawBox( px + 10, py + 40, fill, 4 );
	} else {
		u8g2.drawStr( px + ( pw - vw ) / 2, py + 40, s_osdValue );
	}
}
// ---------------------------------------------------------------------------------------------
#if UI_OSD_DIM_MODE == 2
/* Skład klatki z OSD w 4 bitach na piksel i wysłanie do SH1122 (kopia sekwencji ze sterownika U8g2:
 * u8x8_d_sh1122_common, DRAW_TILE). Poza panelem: warstwa dolna w szarości UI_OSD_DIM_GRAY,
 * w panelu: warstwa OSD w pełnej jasności. Para bitów (lewy, prawy piksel) -> bajt (górny, dolny nibble). */
static void sendComposite( const uint8_t * base, const uint8_t * osd ) {
	u8x8_t * u8x8 = u8g2.getU8x8();
	uint8_t row[ BUF_ROW_BYTES * 4 ];
	uint8_t lutDim[ 4 ], lutFull[ 4 ];
	const uint8_t g = ( UI_OSD_DIM_GRAY < 1 ) ? 1 : ( ( UI_OSD_DIM_GRAY > 15 ) ? 15 : UI_OSD_DIM_GRAY );
	const int bx0 = OSD_PX / 8, bx1 = ( OSD_PX + OSD_PW ) / 8;
	uint32_t t0 = micros();

	lutDim[ 0 ] = 0;  lutDim[ 1 ] = g;  lutDim[ 2 ] = (uint8_t)( g << 4 );  lutDim[ 3 ] = (uint8_t)( ( g << 4 ) | g );
	lutFull[ 0 ] = 0; lutFull[ 1 ] = 0x0F; lutFull[ 2 ] = 0xF0; lutFull[ 3 ] = 0xFF;

	u8x8_cad_StartTransfer( u8x8 );
	for ( int y = 0; y < BUF_ROWS; y++ ) {
		bool panelRow = ( y >= OSD_PY ) && ( y < OSD_PY + OSD_PH );

		for ( int bx = 0; bx < BUF_ROW_BYTES; bx++ ) {
			bool inPanel = panelRow && bx >= bx0 && bx < bx1;
			uint8_t b = inPanel ? osd[ y * BUF_ROW_BYTES + bx ] : base[ y * BUF_ROW_BYTES + bx ];
			const uint8_t * lut = inPanel ? lutFull : lutDim;
			uint8_t * o = &row[ bx * 4 ];

			o[ 0 ] = lut[ ( b >> 6 ) & 3 ];
			o[ 1 ] = lut[ ( b >> 4 ) & 3 ];
			o[ 2 ] = lut[ ( b >> 2 ) & 3 ];
			o[ 3 ] = lut[ b & 3 ];
		}
		u8x8_cad_SendCmd( u8x8, 0x0b0 );                       /* adres wiersza */
		u8x8_cad_SendArg( u8x8, (uint8_t)y );
		u8x8_cad_SendCmd( u8x8, u8x8->x_offset & 15 );         /* adres kolumny, młodsze 4 bity */
		u8x8_cad_SendCmd( u8x8, 0x010 | ( u8x8->x_offset >> 4 ) );
		u8x8_cad_SendData( u8x8, sizeof( row ), row );
	}
	u8x8_cad_EndTransfer( u8x8 );
	s_drawUs = micros() - t0;
}
#endif
// ---------------------------------------------------------------------------------------------
static void render( void ) {
	u8g2.clearBuffer();
	if ( s_screen == UI_SCREEN_STAT ) drawStat();
	else drawSpectrum();

#if UI_OSD_DIM_MODE == 2
	if ( s_osdOn && u8g2.getBufferTileWidth() == BUF_ROW_BYTES && u8g2.getBufferTileHeight() * 8 == BUF_ROWS ) {
		memcpy( s_baseBuf, u8g2.getBufferPtr(), BUF_BYTES );   /* warstwa dolna */
		u8g2.clearBuffer();
		drawOsd();                                             /* warstwa OSD na czystym buforze */
		sendComposite( s_baseBuf, u8g2.getBufferPtr() );
		return;
	}
#endif
	if ( s_osdOn ) {
#if UI_OSD_DIM_MODE == 1 || UI_OSD_DIM_MODE == 2
		dimBufferChecker();    /* tryb 1, a w trybie 2 awaryjnie, gdy układ bufora jest inny niż zakładany */
#endif
		drawOsd();
	}
	flushDisplay();
}
// ---------------------------------------------------------------------------------------------
void UI_Loop( uint32_t now_ms ) {
	static uint32_t tLast = 0;

	if ( s_osdOn && (int32_t)( now_ms - s_osdUntil ) >= 0 ) { /* OSD wygasło: przerysuj samą warstwę dolną */
		s_osdOn = false;
		s_dirty = 1;
	}

	if ( s_screen == UI_SCREEN_STAT ) {
		if ( !s_dirty && now_ms - tLast < 200 ) return; /* liczniki odświeżamy co 200 ms */
		tLast = now_ms;
	} else if ( !s_dirty ) {
		return;                                         /* SPECTRUM: przy każdej nowej ramce widma */
	}
	s_dirty = 0;
	render();
}
// =============================================================================================
