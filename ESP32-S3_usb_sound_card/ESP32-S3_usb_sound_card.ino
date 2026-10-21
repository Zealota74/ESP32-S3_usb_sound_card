/***************************************************************************
 *      ESP32-S3 USB Audio DAC
 *
 *      USB Audio Class 1.0
 *      TinyUSB
 *      PCM5102
 *
 ***************************************************************************/

#include <Arduino.h>
#include "USBAudioCard.h"

#include "USB.h"
#include "tusb.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "esp_timer.h"

#include "simon_defines.h"

/***************************************************************************
 *                          AUDIO CONFIG
 ***************************************************************************/
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint8_t CHANNELS = 2;
constexpr uint8_t BITS = 16;
constexpr uint16_t BYTES_PER_FRAME = CHANNELS * (BITS / 8);

/***************************************************************************
 *                          I2S GPIO
 ***************************************************************************/
constexpr gpio_num_t PIN_BCLK = GPIO_NUM_12;
constexpr gpio_num_t PIN_WS = GPIO_NUM_14;
constexpr gpio_num_t PIN_DOUT = GPIO_NUM_13;

/***************************************************************************
 *                          DMA
 ***************************************************************************/
// constexpr uint16_t DMA_FRAME_NUM = 256;
constexpr uint16_t DMA_FRAME_NUM = 384;
constexpr uint8_t DMA_DESC_NUM = 8;

/***************************************************************************
 *                      AUDIO FIFO
 ***************************************************************************/
constexpr uint32_t FIFO_SIZE = 65536;
constexpr uint32_t FIFO_LOW = FIFO_SIZE * 35 / 100;
constexpr uint32_t FIFO_TARGET = FIFO_SIZE * 50 / 100;
constexpr uint32_t FIFO_HIGH = FIFO_SIZE * 65 / 100;

struct AudioStats {
	volatile uint32_t usbPackets = 0;
	volatile uint32_t usbBytes = 0;
	volatile uint32_t fifoOverrun = 0;
	volatile uint32_t fifoUnderrun = 0;
	volatile uint32_t duplicatedFrames = 0;
	volatile uint32_t droppedFrames = 0;
};

// Dodajemy zmienną przechowującą aktualne przesunięcie zegara
float currentSampleRate = SAMPLE_RATE;

// Upewnij się, że masz zdefiniowane to globalnie:
i2s_chan_handle_t tx_chan;

void adjustClock(float targetRate) {
	i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(targetRate);
	clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

	// Użyj tej funkcji, jeśli poprzednia nie istnieje:
	i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
}

/***************************************************************************
 *                         USB AUDIO DEVICE
 ***************************************************************************/
USBAudioCard uac(SAMPLE_RATE, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE);

portMUX_TYPE fifoMux = portMUX_INITIALIZER_UNLOCKED;
AudioStats stats;

i2s_chan_handle_t txHandle = nullptr;
TaskHandle_t audioTaskHandle = nullptr;

/***************************************************************************
 *                         AUDIO FIFO
 ***************************************************************************/
class AudioFIFO {
private:
	uint8_t buffer[FIFO_SIZE];
	volatile uint32_t writePos = 0;
	volatile uint32_t readPos = 0;
	volatile uint32_t usedBytes = 0;

public:
	uint32_t available() {
		return usedBytes;
	}

	uint32_t freeSpace() {
		return FIFO_SIZE - usedBytes;
	}

	uint8_t fillPercent() {
		return (usedBytes * 100UL) / FIFO_SIZE;
	}

	void clear() {
		portENTER_CRITICAL(&fifoMux);

		writePos = 0;
		readPos = 0;
		usedBytes = 0;

		portEXIT_CRITICAL(&fifoMux);
	}

	uint32_t write(const uint8_t *data, uint32_t bytes) {
		// tylko pełne ramki audio
		bytes -= bytes % BYTES_PER_FRAME;

		if (bytes == 0) return 0;

		portENTER_CRITICAL(&fifoMux);

		uint32_t free = FIFO_SIZE - usedBytes;

		if (bytes > free) {
			bytes = free;
			bytes -= bytes % BYTES_PER_FRAME;
			stats.fifoOverrun++;
		}
		uint32_t first = min(bytes, FIFO_SIZE - writePos);

		memcpy(
		  &buffer[writePos],
		  data,
		  first);

		if (bytes > first) {
			memcpy(buffer, data + first, bytes - first);
		}

		writePos = (writePos + bytes) % FIFO_SIZE;
		usedBytes += bytes;

		portEXIT_CRITICAL(&fifoMux);

		return bytes;
	}

	uint32_t read(uint8_t *data, uint32_t bytes) {
		bytes -= bytes % BYTES_PER_FRAME;

		if (bytes == 0)
			return 0;

		portENTER_CRITICAL(&fifoMux);

		if (bytes > usedBytes) {
			bytes = usedBytes;
			bytes -= bytes % BYTES_PER_FRAME;
		}

		uint32_t first =
		  min(bytes, FIFO_SIZE - readPos);
		memcpy(data, &buffer[readPos], first);

		if (bytes > first) {
			memcpy(
			  data + first,
			  buffer,
			  bytes - first);
		}
		readPos = (readPos + bytes) % FIFO_SIZE;

		usedBytes -= bytes;
		portEXIT_CRITICAL(&fifoMux);
		return bytes;
	}
};

AudioFIFO audioFIFO;

/***************************************************************************
 *                         USB AUDIO RX
 ***************************************************************************/
uint8_t usbAudioBuffer[512];
volatile bool usbReady = false;
bool audioLocked = false;

bool audioStartupReady() {
	return audioFIFO.available() > 4096;
}

/***************************************************************************
 *                         I2S AUDIO ENGINE
 ***************************************************************************/
// uint8_t i2sDMABuffer[1024];
alignas(4) uint8_t i2sDMABuffer[1024];

static uint32_t lastUsbPacket = 0;
volatile uint32_t lastUsbPacketTime = 0;

bool i2sRunning = false;

bool initI2S() {
	Serial.println("Init I2S...");

	// Konfiguracja kanału
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

	// Zwiększone bufory dla stabilności (odporność na jitter USB)
	chan_cfg.dma_desc_num = 4;
	chan_cfg.dma_frame_num = 256;
	chan_cfg.auto_clear = true;  // Zabezpiecza przed trzaskami przy braku danych

	esp_err_t err = i2s_new_channel(&chan_cfg, &txHandle, NULL);
	if (err != ESP_OK) {
		Serial.println("I2S channel error");
		return false;
	}

	// Konfiguracja standardowa
	i2s_std_config_t std_cfg = {
		.clk_cfg = {
		  .sample_rate_hz = SAMPLE_RATE,
		  .clk_src = I2S_CLK_SRC_PLL_160M,  // Bezpieczna wartość bazowa dla S3
		  .mclk_multiple = I2S_MCLK_MULTIPLE_256,
		},
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
		.gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = I2S_GPIO_UNUSED, .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } }
	};

	err = i2s_channel_init_std_mode(txHandle, &std_cfg);
	if (err != ESP_OK) {
		Serial.println("I2S init error");
		return false;
	}

	err = i2s_channel_enable(txHandle);
	if (err != ESP_OK) {
		Serial.println("I2S enable error");
		return false;
	}

	Serial.println("I2S OK");
	return true;
}
bool initI2S_old() {
	Serial.println("Init I2S...");

	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

	chan_cfg.dma_desc_num = DMA_DESC_NUM;
	chan_cfg.dma_frame_num = DMA_FRAME_NUM;

	esp_err_t err = i2s_new_channel(&chan_cfg, &txHandle, NULL);

	if (err != ESP_OK) {
		Serial.println("I2S channel error");
		return false;
	}

	i2s_std_config_t std_cfg = {
		.clk_cfg = {
		  .sample_rate_hz = SAMPLE_RATE,
		  .clk_src = I2S_CLK_SRC_PLL_160M,
		  .mclk_multiple = I2S_MCLK_MULTIPLE_256,
		},

		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
		.gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = I2S_GPIO_UNUSED, .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } }
	};

	err = i2s_channel_init_std_mode(txHandle, &std_cfg);
	if (err != ESP_OK) {
		Serial.println("I2S init error");
		return false;
	}
	err = i2s_channel_enable(txHandle);
	if (err != ESP_OK) {
		Serial.println("I2S enable error");
		return false;
	}

	Serial.println("I2S OK");
	return true;
}

void audioTask(void *parameter) {

	while (true) {
		static uint32_t audioBlock = 0;
		audioBlock++;

		if (!audioStartupReady()) {
			vTaskDelay(pdMS_TO_TICKS(5));
			continue;
		}


		// --- USB / FIFO diagnostyka ---
		static uint32_t lastUsbPrint = 0;

		if (millis() - lastUsbPrint > 1000) {
			lastUsbPrint = millis();

			uint32_t usbAge = millis() - lastUsbPacketTime;
			Serial.printf(
			  "USB age=%lu ms FIFO=%lu BLOCK=%lu\n",
			  usbAge,
			  audioFIFO.available(),
			  audioBlock);
		}


		// --- Pobranie danych z FIFO ---
		uint32_t bytes = audioFIFO.read(i2sDMABuffer, sizeof(i2sDMABuffer));

		// --- SYNCHRONIZACJA SPRZĘTOWA ---
		// Teraz wywołujemy to tutaj, aby utrzymać płynność pracy
		// synchronizeFIFO();


		// test
		if (bytes != sizeof(i2sDMABuffer)) {
			Serial.printf(
			  "SHORT AUDIO READ bytes=%lu FIFO=%lu BLOCK=%lu\n",
			  bytes,
			  audioFIFO.available(),
			  audioBlock);
		}
		// test


		// --- wykrywanie pustego FIFO ---
		static bool wasEmpty = false;

		bool fifoEmpty = (bytes == 0);

		if (fifoEmpty && !wasEmpty) {

			Serial.printf(
			  "FIFO EMPTY START available=%lu\n",
			  audioFIFO.available());
		}

		if (!fifoEmpty && wasEmpty) {
			Serial.println("FIFO DATA RETURN");
		}

		wasEmpty = fifoEmpty;



		// --- diagnostyka ciągłości PCM ---

		int16_t *samples = (int16_t *)i2sDMABuffer;

		int32_t maxJumpL = 0;
		int32_t maxJumpR = 0;

		static int16_t lastL = 0;
		static int16_t lastR = 0;

		uint32_t sampleCount = bytes / 4;


		for (uint32_t i = 0; i < sampleCount; i++) {

			int16_t L = samples[i * 2];
			int16_t R = samples[i * 2 + 1];


			int32_t jumpL = abs((int32_t)L - lastL);
			int32_t jumpR = abs((int32_t)R - lastR);


			if (jumpL > maxJumpL)
				maxJumpL = jumpL;

			if (jumpR > maxJumpR)
				maxJumpR = jumpR;


			lastL = L;
			lastR = R;
		}


		if (maxJumpL > 12000 || maxJumpR > 12000) {

			Serial.printf(
			  "PCM JUMP L=%ld R=%ld FIFO=%lu\n",
			  maxJumpL,
			  maxJumpR,
			  audioFIFO.available());
		}



		// --- zabezpieczenie underrun ---

		if (bytes < sizeof(i2sDMABuffer)) {

			if (bytes == 0) {

				stats.fifoUnderrun++;

				Serial.printf(
				  "AUDIO ZERO FIFO=%lu\n",
				  audioFIFO.available());
			}


			memset(
			  i2sDMABuffer + bytes,
			  0,
			  sizeof(i2sDMABuffer) - bytes);
		}



		// --- OLED diagnostyka ---



		// test
		static uint32_t lastBlockPrint = 0;
		if (millis() - lastBlockPrint > 5000) {
			lastBlockPrint = millis();
			Serial.printf(
			  "I2S blocks=%lu FIFO=%lu\n",
			  audioBlock,
			  audioFIFO.available());
		}
		// test


		oled_audioBlock = audioBlock;
		oled_fifo = audioFIFO.available();



		// --- wysyłanie do I2S ---

		size_t written = 0;


		uac.applyVolume(
		  i2sDMABuffer,
		  sizeof(i2sDMABuffer));

		uint32_t t0 = micros();
		esp_err_t err = i2s_channel_write(
		  txHandle,
		  i2sDMABuffer,
		  sizeof(i2sDMABuffer),
		  &written,
		  portMAX_DELAY);

		uint32_t dt = micros() - t0;
		if (dt > 5000) {
			Serial.printf(
			  "I2S WRITE DELAY=%lu us BLOCK=%lu FIFO=%lu\n",
			  dt,
			  audioBlock,
			  audioFIFO.available());
		}


		if (err != ESP_OK) {

			Serial.printf(
			  "I2S ERROR=%d\n",
			  err);
		}
	}
}

/***************************************************************************
 *                    FIFO SYNCHRONIZATION
 ***************************************************************************/
constexpr uint32_t FIFO_MIN_LEVEL = FIFO_SIZE * 25 / 100;
constexpr uint32_t FIFO_MAX_LEVEL = FIFO_SIZE * 75 / 100;
constexpr uint32_t FIFO_OPT_LEVEL = FIFO_SIZE * 50 / 100;

/***************************************************************************
 *              REMOVE ONE AUDIO FRAME
 ***************************************************************************/
uint32_t findZeroCrossing(int16_t *samples, uint32_t numSamples) {
	// Przeszukujemy bufor (pomijając same skraje), szukając miejsca,
	// gdzie dźwięk w obu kanałach ma wartość bliską zeru (cisza)
	for (uint32_t i = 8; i < numSamples - 8; i += 2) {
		// Sprawdzamy lewy i prawy kanał - szukamy amplitudy bliskiej osi 0
		if (samples[i] > -128 && samples[i] < 128 && samples[i + 1] > -128 && samples[i + 1] < 128) {
			return i;  // Znaleźliśmy idealny punkt!
		}
	}
	// Jeśli muzyka jest ekstremalnie głośna i "ściana dźwięku" nie ma przejść przez zero,
	// tniemy klasycznie na środku.
	return (numSamples / 2) & ~1;
}

void dropOneFrame(uint8_t *buffer, uint32_t size) {
	if (size < 8) return;

	int16_t *samples = (int16_t *)buffer;
	uint32_t numSamples = size / 2;

	// Zamiast ciąć na środku, szukamy idealnego punktu
	uint32_t safeIdx = findZeroCrossing(samples, numSamples);

	samples[safeIdx - 2] = (samples[safeIdx - 2] / 2) + (samples[safeIdx] / 2);
	samples[safeIdx - 1] = (samples[safeIdx - 1] / 2) + (samples[safeIdx + 1] / 2);

	memmove(&samples[safeIdx], &samples[safeIdx + 2], (numSamples - safeIdx - 2) * sizeof(int16_t));

	samples[numSamples - 2] = samples[numSamples - 4];
	samples[numSamples - 1] = samples[numSamples - 3];

	stats.droppedFrames++;
}

/***************************************************************************
 *              DUPLICATE ONE AUDIO FRAME
 ***************************************************************************/
void duplicateOneFrame(uint8_t *buffer, uint32_t size) {
	if (size < 8) return;

	int16_t *samples = (int16_t *)buffer;
	uint32_t numSamples = size / 2;

	// Zamiast dublować na środku, szukamy ciszy
	uint32_t safeIdx = findZeroCrossing(samples, numSamples);

	memmove(&samples[safeIdx + 2], &samples[safeIdx], (numSamples - safeIdx - 2) * sizeof(int16_t));

	samples[safeIdx] = (samples[safeIdx - 2] / 2) + (samples[safeIdx + 2] / 2);
	samples[safeIdx + 1] = (samples[safeIdx - 1] / 2) + (samples[safeIdx + 3] / 2);

	stats.duplicatedFrames++;
}

/***************************************************************************
 *                 FIFO LEVEL CONTROL
 ***************************************************************************/
void synchronizeFIFO() {
	static uint32_t lastAdjustTime = 0;
	const uint32_t ADJUST_INTERVAL = 500;  // Korekta nie częściej niż co 500ms

	uint32_t level = audioFIFO.available();
	uint32_t now = millis();

	// 1. Martwa strefa (Deadband) - niech bufor "żyje" w tym zakresie
	if (level > (FIFO_SIZE * 40 / 100) && level < (FIFO_SIZE * 60 / 100)) {
		return;
	}

	// 2. Czy minął czas od ostatniej korekty?
	if (now - lastAdjustTime < ADJUST_INTERVAL) {
		return;
	}

	// 3. Delikatna korekta zegara
	float delta = 0.1f;

	if (level > FIFO_MAX_LEVEL) {
		currentSampleRate += delta;
		adjustClock(currentSampleRate);
		lastAdjustTime = now;
	} else if (level < FIFO_MIN_LEVEL) {
		currentSampleRate -= delta;
		adjustClock(currentSampleRate);
		lastAdjustTime = now;
	}
}

/***************************************************************************
 *                         TASK START
 ***************************************************************************/
void startAudioTask() {
	xTaskCreatePinnedToCore(
	  audioTask,
	  "AudioTask",
	  4096,
	  nullptr,
	  configMAX_PRIORITIES - 1,
	  &audioTaskHandle,
	  1);
	Serial.println("Audio task started");
}

/***************************************************************************
 *                   USB AUDIO CALLBACK
 ***************************************************************************/
void usbAudioReceived(void *data, uint16_t len) {
	lastUsbPacket = millis();
	lastUsbPacketTime = millis();

	uint32_t written = audioFIFO.write((uint8_t *)data, len);

	if (written) {
		stats.usbPackets++;
		stats.usbBytes += written;
		// test
		oled_usbPackets = stats.usbPackets;
		// test
	}
	if (written != len) {
		Serial.printf(
		  "FIFO SHORT WRITE %lu/%u\n",
		  written,
		  len);
	}
}

/***************************************************************************
 *                         USB INIT
 ***************************************************************************/
void initUSB() {
	USB.VID(0x303A);
	USB.PID(0x4001);

	uac.onData(usbAudioReceived);

	uac.begin();
	// uac.applyVolume(void *data, uint16_t len)
	USB.begin();

	Serial.println("USB Audio device started");
}

/***************************************************************************
 *                              SETUP
 ***************************************************************************/

void setup() {
	Serial.begin(115200);

	delay(1000);

	Serial.println();
	Serial.println("==============================");
	Serial.println(" ESP32-S3 USB AUDIO DAC");
	Serial.println(" PCM5102 I2S");
	Serial.println("==============================");

	oled_encoder_init();


	//
	// FIFO zerujemy
	//
	audioFIFO.clear();

	//
	// I2S
	//
	if (!initI2S()) {
		Serial.println("I2S FAILED");
		while (1) {
			delay(1000);
		}
	}

	//
	// USB
	//
	initUSB();

	delay(500);

	//
	// Audio Task
	//
	startAudioTask();
	Serial.println("System ready");
}


void usb_card_log() {
	static uint32_t lastLog = 0;

	if (millis() - lastLog > 5000) {
		lastLog = millis();
		Serial.printf(
		  "FIFO=%lu USB=%lu OV=%lu UF=%lu\n",
		  audioFIFO.available(),
		  stats.usbPackets,
		  stats.fifoOverrun,
		  stats.fifoUnderrun);
	}

	delay(10);
}
/***************************************************************************
 *                              LOOP
 ***************************************************************************/
void loop() {
	// usb_card_log();
	oled_encoder_loop();
}

volatile float masterVolume = 0.5f;
// Wklej to na samym dole pliku .ino
bool tud_audio_set_req_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *p_buf) {
	// Sprawdźmy, czy Windows w ogóle wysyła żądania
	Serial.println("USB Audio Request received!");  // Odkomentuj to do testów

	if (p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
		// Tu odbieramy głośność (zazwyczaj 2 bajty w p_buf)
		uint16_t vol = (p_buf[1] << 8) | p_buf[0];

		// Logujemy, żeby zobaczyć co Windows wysyła
		Serial.printf("Windows sent volume: %d\n", vol);

		// Zaktualizuj swoją zmienną głośności
		masterVolume = (float)vol / 32767.0f;
		return true;
	}
	return false;
}