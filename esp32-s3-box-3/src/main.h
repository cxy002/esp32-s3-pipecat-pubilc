#include <stdint.h>

#define LOG_TAG "pipecat"
#define MAX_HTTP_OUTPUT_BUFFER 4096
#define HTTP_TIMEOUT_MS 10000
#define TICK_INTERVAL 15

// Wifi
extern void pipecat_init_wifi();

// Cellular
extern void pipecat_init_cellular();
extern bool pipecat_cellular_ready();

// Deepmem WebSocket
extern void pipecat_init_deepmem_ws();

// Breadboard audio hardware
extern void pipecat_init_hardware_audio();
extern int pipecat_audio_write_speaker(const int16_t *data, int samples);

// SD card song playback
extern void pipecat_init_song_player();
extern bool pipecat_play_song_from_sd(const char *path);
extern void pipecat_handle_asr_text(const char *text);

// VAD benchmark
extern void pipecat_init_vad_serial_test();

// SD card
extern bool pipecat_init_sdcard();

// SD-backed audio file storage and SQLite index
extern bool pipecat_init_audio_storage();

// Screen
extern void pipecat_init_screen();
extern void pipecat_screen_system_log(const char *text);
extern void pipecat_screen_new_log();
extern void pipecat_screen_log(const char *text);
