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

// Screen
extern void pipecat_init_screen();
extern void pipecat_screen_system_log(const char *text);
extern void pipecat_screen_new_log();
extern void pipecat_screen_log(const char *text);
