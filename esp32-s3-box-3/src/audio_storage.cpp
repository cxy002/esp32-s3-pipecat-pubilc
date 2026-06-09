#include "main.h"

#include "board_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sqlite3.h"

namespace {
const char *TAG = "audio_storage";

constexpr const char *kAudioDir = SDCARD_MOUNT_POINT "/audio";
constexpr const char *kDbPath = SDCARD_MOUNT_POINT "/audio.db";
constexpr const char *kTestWavPath = SDCARD_MOUNT_POINT "/audio/T0000001.WAV";
constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
constexpr int kDurationMs = 1000;

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

esp_err_t ensure_dir(const char *path) {
  struct stat st = {};
  if (stat(path, &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      ESP_LOGI(TAG, "Directory exists: %s", path);
      return ESP_OK;
    }
    ESP_LOGE(TAG, "%s exists but is not a directory", path);
    return ESP_FAIL;
  }

  if (mkdir(path, 0775) == 0 || errno == EEXIST) {
    ESP_LOGI(TAG, "Created directory: %s", path);
    return ESP_OK;
  }

  ESP_LOGE(TAG, "Failed to create %s, errno=%d", path, errno);
  return ESP_FAIL;
}

esp_err_t write_test_wav() {
  FILE *f = fopen(kTestWavPath, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s for writing, errno=%d", kTestWavPath, errno);
    return ESP_FAIL;
  }

  const uint32_t data_bytes =
      kSampleRate * kChannels * (kBitsPerSample / 8) * kDurationMs / 1000;
  const uint32_t byte_rate = kSampleRate * kChannels * (kBitsPerSample / 8);
  const uint16_t block_align = kChannels * (kBitsPerSample / 8);

  fwrite("RIFF", 1, 4, f);
  write_le32(f, 36 + data_bytes);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);
  write_le32(f, 16);
  write_le16(f, 1);
  write_le16(f, kChannels);
  write_le32(f, kSampleRate);
  write_le32(f, byte_rate);
  write_le16(f, block_align);
  write_le16(f, kBitsPerSample);
  fwrite("data", 1, 4, f);
  write_le32(f, data_bytes);

  uint8_t silence[512] = {};
  uint32_t remaining = data_bytes;
  while (remaining > 0) {
    const size_t chunk = remaining > sizeof(silence) ? sizeof(silence) : remaining;
    if (fwrite(silence, 1, chunk, f) != chunk) {
      fclose(f);
      ESP_LOGE(TAG, "Failed to write WAV data");
      return ESP_FAIL;
    }
    remaining -= chunk;
  }

  fclose(f);
  ESP_LOGI(TAG, "Wrote test WAV: %s (%lu bytes)", kTestWavPath,
           static_cast<unsigned long>(44 + data_bytes));
  return ESP_OK;
}

bool exec_sql(sqlite3 *db, const char *sql) {
  char *errmsg = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL failed: %s, err=%s", sql, errmsg ? errmsg : sqlite3_errmsg(db));
    sqlite3_free(errmsg);
    return false;
  }
  return true;
}

bool insert_audio_record(sqlite3 *db) {
  const char *sql =
      "INSERT INTO audio_record("
      "file_path,sample_rate,channels,bits_per_sample,duration_ms,created_at_ms,note"
      ") VALUES(?,?,?,?,?,?,?);";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "prepare failed: %s", sqlite3_errmsg(db));
    return false;
  }

  sqlite3_bind_text(stmt, 1, kTestWavPath, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, kSampleRate);
  sqlite3_bind_int(stmt, 3, kChannels);
  sqlite3_bind_int(stmt, 4, kBitsPerSample);
  sqlite3_bind_int(stmt, 5, kDurationMs);
  sqlite3_bind_int64(stmt, 6, esp_timer_get_time() / 1000);
  sqlite3_bind_text(stmt, 7, "startup sqlite audio index test", -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "insert failed: %s", sqlite3_errmsg(db));
    return false;
  }

  ESP_LOGI(TAG, "Inserted audio index: %s", kTestWavPath);
  return true;
}

bool log_latest_record(sqlite3 *db) {
  const char *sql =
      "SELECT id,file_path,sample_rate,duration_ms FROM audio_record "
      "ORDER BY id DESC LIMIT 1;";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "query prepare failed: %s", sqlite3_errmsg(db));
    return false;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    ESP_LOGI(TAG, "Latest audio id=%lld path=%s rate=%d duration=%dms",
             sqlite3_column_int64(stmt, 0),
             reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)),
             sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3));
  } else {
    ESP_LOGW(TAG, "No audio record found");
  }

  sqlite3_finalize(stmt);
  return rc == SQLITE_ROW;
}
}  // namespace

bool pipecat_init_audio_storage() {
  if (ensure_dir(kAudioDir) != ESP_OK) {
    pipecat_screen_system_log("Audio dir fail\n");
    return false;
  }

  if (write_test_wav() != ESP_OK) {
    pipecat_screen_system_log("WAV write fail\n");
    return false;
  }

  ESP_LOGI(TAG, "Initializing SQLite");
  int rc = sqlite3_initialize();
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "sqlite3_initialize failed: rc=%d", rc);
    pipecat_screen_system_log("SQLite init fail\n");
    return false;
  }

  sqlite3 *db = nullptr;
  ESP_LOGI(TAG, "Opening database: %s", kDbPath);
  rc = sqlite3_open_v2(kDbPath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       "esp32");
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "open db failed: %s", db ? sqlite3_errmsg(db) : "no db");
    if (db) {
      sqlite3_close(db);
    }
    pipecat_screen_system_log("SQLite open fail\n");
    return false;
  }

  bool ok = exec_sql(db, "PRAGMA journal_mode=DELETE;") &&
            exec_sql(db, "PRAGMA synchronous=NORMAL;") &&
            exec_sql(db,
                     "CREATE TABLE IF NOT EXISTS audio_record ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "file_path TEXT NOT NULL,"
                     "sample_rate INTEGER NOT NULL,"
                     "channels INTEGER NOT NULL,"
                     "bits_per_sample INTEGER NOT NULL,"
                     "duration_ms INTEGER NOT NULL,"
                     "created_at_ms INTEGER NOT NULL,"
                     "note TEXT"
                     ");") &&
            insert_audio_record(db) && log_latest_record(db);

  sqlite3_close(db);

  if (!ok) {
    pipecat_screen_system_log("SQLite test fail\n");
    return false;
  }

  ESP_LOGI(TAG, "Audio DB ready: %s", kDbPath);
  pipecat_screen_system_log("Audio DB OK\n");
  return true;
}
