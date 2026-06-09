#include "main.h"

#include "board_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <math.h>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sqlite3.h"

namespace {
const char *TAG = "song_player";
constexpr const char *kMusicDir = SDCARD_MOUNT_POINT "/music";
constexpr const char *kDbPath = SDCARD_MOUNT_POINT "/audio.db";
constexpr int kSongSampleRate = AUDIO_OUTPUT_SAMPLE_RATE;
constexpr int kSongChannels = 1;
constexpr int kSongBitsPerSample = 16;

struct WavInfo {
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  uint32_t data_offset = 0;
  uint32_t data_size = 0;
};

uint16_t read_le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool ensure_dir(const char *path) {
  struct stat st = {};
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return mkdir(path, 0775) == 0 || errno == EEXIST;
}

void write_le16(FILE *f, uint16_t value) {
  fputc(value & 0xff, f);
  fputc((value >> 8) & 0xff, f);
}

void write_le32(FILE *f, uint32_t value) {
  fputc(value & 0xff, f);
  fputc((value >> 8) & 0xff, f);
  fputc((value >> 16) & 0xff, f);
  fputc((value >> 24) & 0xff, f);
}

bool write_test_song_if_missing() {
  struct stat st = {};
  if (stat(SONG_DEFAULT_PATH, &st) == 0 && st.st_size > 44) {
    ESP_LOGI(TAG, "Song already exists: %s (%ld bytes)", SONG_DEFAULT_PATH,
             static_cast<long>(st.st_size));
    return true;
  }

  FILE *f = fopen(SONG_DEFAULT_PATH, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Create song failed: %s errno=%d", SONG_DEFAULT_PATH, errno);
    return false;
  }

  constexpr float kNotes[] = {523.25f, 587.33f, 659.25f, 523.25f,
                              523.25f, 587.33f, 659.25f, 523.25f,
                              659.25f, 698.46f, 783.99f,
                              659.25f, 698.46f, 783.99f};
  constexpr int kNoteCount = sizeof(kNotes) / sizeof(kNotes[0]);
  constexpr int kNoteMs = 260;
  constexpr int kSamplesPerNote = kSongSampleRate * kNoteMs / 1000;
  constexpr uint32_t kDataBytes =
      kNoteCount * kSamplesPerNote * kSongChannels * (kSongBitsPerSample / 8);
  constexpr uint32_t kByteRate =
      kSongSampleRate * kSongChannels * (kSongBitsPerSample / 8);
  constexpr uint16_t kBlockAlign = kSongChannels * (kSongBitsPerSample / 8);

  fwrite("RIFF", 1, 4, f);
  write_le32(f, 36 + kDataBytes);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);
  write_le32(f, 16);
  write_le16(f, 1);
  write_le16(f, kSongChannels);
  write_le32(f, kSongSampleRate);
  write_le32(f, kByteRate);
  write_le16(f, kBlockAlign);
  write_le16(f, kSongBitsPerSample);
  fwrite("data", 1, 4, f);
  write_le32(f, kDataBytes);

  for (int n = 0; n < kNoteCount; ++n) {
    for (int i = 0; i < kSamplesPerNote; ++i) {
      float t = static_cast<float>(i) / kSongSampleRate;
      float envelope = 1.0f;
      if (i < 240) {
        envelope = static_cast<float>(i) / 240.0f;
      } else if (i > kSamplesPerNote - 240) {
        envelope = static_cast<float>(kSamplesPerNote - i) / 240.0f;
      }
      int16_t sample = static_cast<int16_t>(
          sinf(2.0f * static_cast<float>(M_PI) * kNotes[n] * t) * 7000.0f *
          envelope);
      write_le16(f, static_cast<uint16_t>(sample));
    }
  }

  fclose(f);
  ESP_LOGI(TAG, "Generated test song: %s (%lu bytes)", SONG_DEFAULT_PATH,
           static_cast<unsigned long>(44 + kDataBytes));
  pipecat_screen_system_log("Song saved\n");
  return true;
}

bool parse_wav(FILE *f, WavInfo *info) {
  uint8_t header[12];
  if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
      memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
    return false;
  }

  bool got_fmt = false;
  bool got_data = false;
  while (!got_data) {
    uint8_t chunk[8];
    if (fread(chunk, 1, sizeof(chunk), f) != sizeof(chunk)) {
      break;
    }

    uint32_t size = read_le32(chunk + 4);
    long data_pos = ftell(f);
    if (data_pos < 0) {
      return false;
    }

    if (memcmp(chunk, "fmt ", 4) == 0) {
      if (size < 16) {
        return false;
      }
      uint8_t fmt[16];
      if (fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt)) {
        return false;
      }
      info->audio_format = read_le16(fmt);
      info->channels = read_le16(fmt + 2);
      info->sample_rate = read_le32(fmt + 4);
      info->bits_per_sample = read_le16(fmt + 14);
      got_fmt = true;
    } else if (memcmp(chunk, "data", 4) == 0) {
      info->data_offset = static_cast<uint32_t>(data_pos);
      info->data_size = size;
      got_data = true;
    }

    long next = data_pos + size + (size & 1);
    if (fseek(f, next, SEEK_SET) != 0) {
      return false;
    }
  }

  return got_fmt && got_data;
}

bool exec_sql(sqlite3 *db, const char *sql) {
  char *errmsg = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    ESP_LOGW(TAG, "SQL failed: %s, err=%s", sql, errmsg ? errmsg : sqlite3_errmsg(db));
    sqlite3_free(errmsg);
    return false;
  }
  return true;
}

void register_song_if_present() {
  struct stat st = {};
  if (stat(SONG_DEFAULT_PATH, &st) != 0) {
    ESP_LOGW(TAG, "Song not found after generation: %s", SONG_DEFAULT_PATH);
    return;
  }

  sqlite3_initialize();
  sqlite3 *db = nullptr;
  int rc = sqlite3_open_v2(kDbPath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                           "esp32");
  if (rc != SQLITE_OK) {
    ESP_LOGW(TAG, "Open song db failed: %s", db ? sqlite3_errmsg(db) : "no db");
    if (db) {
      sqlite3_close(db);
    }
    return;
  }

  if (exec_sql(db,
               "CREATE TABLE IF NOT EXISTS song_library ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "title TEXT NOT NULL,"
               "file_path TEXT NOT NULL UNIQUE,"
               "sample_rate INTEGER,"
               "channels INTEGER"
               ");")) {
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(
        db,
        "INSERT OR IGNORE INTO song_library(title,file_path,sample_rate,channels) "
        "VALUES(?,?,?,?);",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
      sqlite3_bind_text(stmt, 1, "default-song", -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, SONG_DEFAULT_PATH, -1, SQLITE_STATIC);
      sqlite3_bind_int(stmt, 3, AUDIO_OUTPUT_SAMPLE_RATE);
      sqlite3_bind_int(stmt, 4, 1);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      ESP_LOGI(TAG, "Song indexed: %s", SONG_DEFAULT_PATH);
    }
  }

  sqlite3_close(db);
}

void song_test_task(void *pv) {
  (void)pv;
  vTaskDelay(pdMS_TO_TICKS(3000));
  pipecat_handle_asr_text("唱歌");
  vTaskDelete(nullptr);
}
}  // namespace

bool pipecat_play_song_from_sd(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Open song failed: %s errno=%d", path, errno);
    pipecat_screen_system_log("Song open fail\n");
    return false;
  }

  WavInfo info;
  if (!parse_wav(f, &info)) {
    fclose(f);
    ESP_LOGE(TAG, "Invalid WAV: %s", path);
    pipecat_screen_system_log("Song wav fail\n");
    return false;
  }

  if (info.audio_format != 1 || info.channels != 1 ||
      info.bits_per_sample != 16 ||
      info.sample_rate != AUDIO_OUTPUT_SAMPLE_RATE) {
    fclose(f);
    ESP_LOGE(TAG,
             "Unsupported WAV: format=%u channels=%u rate=%lu bits=%u, need PCM "
             "mono %d Hz 16-bit",
             info.audio_format, info.channels,
             static_cast<unsigned long>(info.sample_rate), info.bits_per_sample,
             AUDIO_OUTPUT_SAMPLE_RATE);
    pipecat_screen_system_log("Song fmt fail\n");
    return false;
  }

  if (fseek(f, info.data_offset, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }

  ESP_LOGI(TAG, "Playing song: %s (%lu bytes)", path,
           static_cast<unsigned long>(info.data_size));
  pipecat_screen_system_log("Singing...\n");

  std::vector<int16_t> pcm(1024);
  uint32_t remaining = info.data_size;
  while (remaining > 0) {
    size_t want_bytes = pcm.size() * sizeof(int16_t);
    if (want_bytes > remaining) {
      want_bytes = remaining;
    }
    size_t got = fread(pcm.data(), 1, want_bytes, f);
    if (got == 0) {
      break;
    }
    pipecat_audio_write_speaker(pcm.data(), got / sizeof(int16_t));
    remaining -= got;
  }

  fclose(f);
  ESP_LOGI(TAG, "Song finished");
  pipecat_screen_system_log("Song done\n");
  return true;
}

void pipecat_handle_asr_text(const char *text) {
  if (!text) {
    return;
  }
  ESP_LOGI(TAG, "ASR text: %s", text);
  if (strstr(text, "唱歌") || strstr(text, "sing")) {
    pipecat_play_song_from_sd(SONG_DEFAULT_PATH);
  }
}

void pipecat_init_song_player() {
  if (!ensure_dir(kMusicDir)) {
    ESP_LOGW(TAG, "Create music directory failed: %s", kMusicDir);
    pipecat_screen_system_log("Music dir fail\n");
    return;
  }

  write_test_song_if_missing();
  register_song_if_present();
  pipecat_screen_system_log("Song player OK\n");

#if ENABLE_SONG_PLAYER_TEST
  xTaskCreate(song_test_task, "song_test", 4096, nullptr, 4, nullptr);
#endif
}
