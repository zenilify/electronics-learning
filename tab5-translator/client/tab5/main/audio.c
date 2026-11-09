#include "audio.h"
#include "papp.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"

static const char *TAG = "audio";
static esp_codec_dev_handle_t s_mic, s_spk;
static TaskHandle_t s_rec_task;
static volatile bool s_rec, s_stop_play, s_playing;
static volatile int s_side, s_level;
static volatile int64_t s_rec_t0;
static uint8_t *s_buf;                         // PSRAM: 44 + 30 s * 32 kB
enum { MODE_PTT, MODE_NOTE, MODE_VAD, MODE_STREAM };
static volatile int s_mode = MODE_PTT, s_vad_state;
static volatile bool s_vad_on, s_vad_paused;
static int s_note_max;
static audio_wav_cb_t s_cb;
static void *s_cb_ud;
static const size_t BUF_MAX = 44 + PAPP_MAX_SEC * PAPP_RATE * 2;

void wav_header(uint8_t *h, size_t n, int rate)
{
    uint32_t v;
    memcpy(h, "RIFF", 4); v = 36 + n; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);            // PCM
    s = 1; memcpy(h + 22, &s, 2);                     // mono
    v = rate; memcpy(h + 24, &v, 4);
    v = rate * 2; memcpy(h + 28, &v, 4);              // byte rate
    s = 2; memcpy(h + 32, &s, 2);                     // block align
    s = 16; memcpy(h + 34, &s, 2);                    // bits
    memcpy(h + 36, "data", 4); v = n; memcpy(h + 40, &v, 4);
}

bool wav_parse(const uint8_t *w, size_t len, const int16_t **pcm, size_t *samples, int *rate)
{
    if (len < 44 || memcmp(w, "RIFF", 4) || memcmp(w + 8, "WAVE", 4)) return false;
    size_t off = 12; int r = 0, ch = 0, bits = 0;
    while (off + 8 <= len) {
        uint32_t sz; memcpy(&sz, w + off + 4, 4);
        if (!memcmp(w + off, "fmt ", 4) && sz >= 16) {
            uint16_t c, b; uint32_t rr;
            memcpy(&c, w + off + 10, 2); memcpy(&rr, w + off + 12, 4); memcpy(&b, w + off + 22, 2);
            ch = c; r = rr; bits = b;
        } else if (!memcmp(w + off, "data", 4)) {
            if (ch != 1 || bits != 16) return false;
            size_t n = sz; if (off + 8 + n > len) n = len - off - 8;
            *pcm = (const int16_t *)(w + off + 8); *samples = n / 2; *rate = r;
            return true;
        }
        off += 8 + sz + (sz & 1);
    }
    return false;
}

static int chunk_peak_rms(const int16_t *p, int ns, int *rms)
{
    int pk = 0; double acc = 0;
    for (int i = 0; i < ns; i++) { int v = abs(p[i]); if (v > pk) pk = v; acc += (double)p[i] * p[i]; }
    *rms = (int)sqrt(acc / (ns ? ns : 1));
    return pk;
}

static void rec_note(void)
{
    const size_t CH = PAPP_RATE / 50 * 2;
    size_t cap = 44 + (size_t)s_note_max * PAPP_RATE * 2, n = 0;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) { ESP_LOGE(TAG, "note: no PSRAM for %u kB", (unsigned)(cap / 1024)); s_rec = false; return; }
    uint8_t junk[640];
    for (int i = 0; i < 3; i++) esp_codec_dev_read(s_mic, junk, sizeof junk);
    while (s_rec && 44 + n + CH <= cap) {
        if (esp_codec_dev_read(s_mic, buf + 44 + n, CH) != ESP_CODEC_DEV_OK) { vTaskDelay(1); continue; }
        int rms; s_level = chunk_peak_rms((int16_t *)(buf + 44 + n), CH / 2, &rms) * 100 / 32768;
        n += CH;
    }
    s_rec = false; s_level = 0;
    ESP_LOGI(TAG, "note recorded %.1f s", n / 2.0 / PAPP_RATE);
    if (n < PAPP_RATE) { free(buf); return; }                  // < 0.5 s: accidental
    wav_header(buf, n, PAPP_RATE);
    if (s_cb) s_cb(buf, 44 + n, s_cb_ud); else free(buf);
}

static void rec_vad(void)
{
    enum { CH = PAPP_RATE / 50 * 2, PRE = 15, END_SIL = 40, MIN_SPEECH = 25, MAX_LEN = 15 * PAPP_RATE * 2 };
    static uint8_t pre[PRE][CH];
    uint8_t c[CH];
    int pre_i = 0, pre_n = 0, sil = 0, speech = 0, above = 0;
    size_t n = 0; float nf = 300;
    bool in_utt = false;
    for (int i = 0; i < 3; i++) esp_codec_dev_read(s_mic, c, sizeof c);
    ESP_LOGI(TAG, "vad: listening");
    while (s_vad_on) {
        if (esp_codec_dev_read(s_mic, c, CH) != ESP_CODEC_DEV_OK) { vTaskDelay(1); continue; }
        int rms; int pk = chunk_peak_rms((int16_t *)c, CH / 2, &rms);
        s_level = pk * 100 / 32768;
        if (s_vad_paused || s_playing) {                       // half duplex: never hear ourselves
            in_utt = false; n = 0; sil = speech = above = 0; pre_n = 0; s_vad_state = 3; continue;
        }
        float thr = nf * 3.0f; if (thr < 450) thr = 450;
        bool loud = rms > thr;
        if (!in_utt) {
            s_vad_state = 1;
            memcpy(pre[pre_i], c, CH); pre_i = (pre_i + 1) % PRE; if (pre_n < PRE) pre_n++;
            if (!loud) { nf = nf * 0.97f + rms * 0.03f; above = 0; continue; }
            if (++above < 4) continue;                         // 80 ms above threshold = speech onset
            in_utt = true; n = 0; sil = 0; speech = above;
            for (int k = 0; k < pre_n; k++) { memcpy(s_buf + 44 + n, pre[(pre_i + PRE - pre_n + k) % PRE], CH); n += CH; }
            s_vad_state = 2;
            continue;
        }
        if (44 + n + CH <= BUF_MAX) { memcpy(s_buf + 44 + n, c, CH); n += CH; }
        if (loud) { speech++; sil = 0; } else sil++;
        if (sil >= END_SIL || n >= MAX_LEN) {
            in_utt = false; above = 0; pre_n = 0;
            if (speech >= MIN_SPEECH) {
                size_t keep = n - (size_t)(sil > 10 ? sil - 10 : 0) * CH;   // trim most of the trailing silence
                uint8_t *w = heap_caps_malloc(44 + keep, MALLOC_CAP_SPIRAM);
                if (w) {
                    wav_header(s_buf, keep, PAPP_RATE); memcpy(w, s_buf, 44 + keep);
                    ESP_LOGI(TAG, "vad: utterance %.1f s (speech %d ms, floor %.0f)", keep / 2.0 / PAPP_RATE, speech * 20, nf);
                    if (s_cb) s_cb(w, 44 + keep, s_cb_ud); else free(w);
                }
            } else ESP_LOGI(TAG, "vad: blip ignored (%d ms)", speech * 20);
            n = 0;
        }
    }
    s_vad_state = 0; s_level = 0;
    ESP_LOGI(TAG, "vad: off");
}

// Live subtitles: raw PCM16 in fixed chunks (no WAV header) until audio_stream_stop(). cb owns each chunk.
static volatile bool s_stream_on;
static int s_stream_bytes;
static void rec_stream(void)
{
    const size_t CH = PAPP_RATE / 50 * 2;
    uint8_t junk[640];
    for (int i = 0; i < 3; i++) esp_codec_dev_read(s_mic, junk, sizeof junk);
    ESP_LOGI(TAG, "stream: on (%d B chunks)", s_stream_bytes);
    uint8_t *buf = NULL; size_t n = 0;
    while (s_stream_on) {
        if (!buf) { buf = heap_caps_malloc(s_stream_bytes, MALLOC_CAP_SPIRAM); n = 0; if (!buf) { vTaskDelay(pdMS_TO_TICKS(20)); continue; } }
        if (esp_codec_dev_read(s_mic, buf + n, CH) != ESP_CODEC_DEV_OK) { vTaskDelay(1); continue; }
        int rms; s_level = chunk_peak_rms((int16_t *)(buf + n), CH / 2, &rms) * 100 / 32768;
        n += CH;
        if (n + CH > (size_t)s_stream_bytes) { if (s_cb) s_cb(buf, n, s_cb_ud); else free(buf); buf = NULL; }
    }
    if (buf && n >= CH * 10 && s_cb) s_cb(buf, n, s_cb_ud); else free(buf);   // flush the tail (>= 200 ms)
    s_level = 0;
    ESP_LOGI(TAG, "stream: off");
}

static void rec_task(void *arg)
{
    const size_t CH = PAPP_RATE / 50 * 2;  // 20 ms
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_mode == MODE_NOTE) { rec_note(); s_mode = MODE_PTT; continue; }
        if (s_mode == MODE_VAD) { rec_vad(); s_mode = MODE_PTT; continue; }
        if (s_mode == MODE_STREAM) { rec_stream(); s_mode = MODE_PTT; continue; }
        size_t n = 0;
        int side = s_side;
        // flush stale DMA data so the recording starts at the press
        uint8_t junk[640];
        for (int i = 0; i < 3; i++) esp_codec_dev_read(s_mic, junk, sizeof junk);
        while (s_rec && 44 + n + CH <= BUF_MAX) {
            if (esp_codec_dev_read(s_mic, s_buf + 44 + n, CH) != ESP_CODEC_DEV_OK) { vTaskDelay(1); continue; }
            int16_t *p = (int16_t *)(s_buf + 44 + n); int pk = 0;
            for (int i = 0; i < (int)(CH / 2); i++) { int a = abs(p[i]); if (a > pk) pk = a; }
            s_level = pk * 100 / 32768;
            n += CH;
        }
        s_rec = false; s_level = 0;
        int ms = (int)(n / 2 * 1000 / PAPP_RATE);
        if (ms < PAPP_MIN_MS) { ESP_LOGI(TAG, "tap ignored (%d ms)", ms); continue; }
        papp_job_t *job = calloc(1, sizeof *job);
        job->wav = heap_caps_malloc(44 + n, MALLOC_CAP_SPIRAM);
        if (!job->wav) { free(job); ESP_LOGE(TAG, "no PSRAM for %u B", (unsigned)(44 + n)); continue; }
        wav_header(s_buf, n, PAPP_RATE);
        memcpy(job->wav, s_buf, 44 + n);
        job->wav_len = 44 + n; job->wav_owned = true; job->side = side; job->demo_idx = -1;
        lang_t l, r; pair_langs(papp_get_pair(), &l, &r);
        job->src = side ? r : l; job->tgt = side ? l : r;
        {   int16_t *pp = (int16_t *)(s_buf + 44); size_t ns = n / 2; int pk = 0; double acc = 0;
            for (size_t i = 0; i < ns; i++) { int v = abs(pp[i]); if (v > pk) pk = v; acc += (double)pp[i] * pp[i]; }
            ESP_LOGI(TAG, "recorded %d ms side %d %s->%s peak %d rms %d", ms, side, lang_code(job->src),
                     lang_code(job->tgt), pk, (int)sqrt(acc / (ns ? ns : 1))); }
        if (!papp_submit(job)) { free(job->wav); free(job); }
    }
}

bool audio_init(void)
{
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PAPP_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = BSP_I2S_LCLK,
                      .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN },
    };
    if (bsp_audio_init(&cfg) != ESP_OK) { ESP_LOGE(TAG, "i2s init failed"); return false; }
    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_spk || !s_mic) { ESP_LOGE(TAG, "codec init failed spk=%p mic=%p", s_spk, s_mic); return false; }
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = PAPP_RATE };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) ESP_LOGE(TAG, "speaker open failed");
    if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) ESP_LOGE(TAG, "mic open failed");
    esp_codec_dev_set_out_vol(s_spk, 80);
    esp_codec_dev_set_in_gain(s_mic, 30.0f);
    s_buf = heap_caps_malloc(BUF_MAX, MALLOC_CAP_SPIRAM);
    if (!s_buf) return false;
    xTaskCreatePinnedToCore(rec_task, "rec", 6144, NULL, 6, &s_rec_task, 1);
    ESP_LOGI(TAG, "audio ready (%d Hz mono, buffer %u kB PSRAM)", PAPP_RATE, (unsigned)(BUF_MAX / 1024));
    return true;
}

void audio_rec_start(int side)
{
    if (s_rec || !s_rec_task || s_mode != MODE_PTT || s_vad_on || s_stream_on) return;
    audio_play_stop();
    s_side = side; s_rec_t0 = esp_timer_get_time(); s_rec = true;
    xTaskNotifyGive(s_rec_task);
}
void audio_rec_stop(void) { s_rec = false; }
bool audio_recording(void) { return s_rec; }
float audio_rec_seconds(void) { return s_rec ? (esp_timer_get_time() - s_rec_t0) / 1e6f : 0; }
int audio_level(void) { return s_level; }
void audio_play_stop(void) { if (s_playing) s_stop_play = true; }
bool audio_playing(void) { return s_playing; }

void audio_play(const int16_t *pcm, size_t samples, int rate)
{
    if (!s_spk || !pcm || !samples) return;
    s_playing = true; s_stop_play = false;
    enum { CHUNK = 1024 };
    static int16_t out[CHUNK];
    // linear resample to PAPP_RATE when the reply is at another rate (we ask for 16 kHz, so normally 1:1)
    double step = (double)rate / PAPP_RATE, pos = 0;
    size_t total = rate == PAPP_RATE ? samples : (size_t)(samples / step);
    for (size_t done = 0; done < total && !s_stop_play;) {
        size_t n = total - done > CHUNK ? CHUNK : total - done;
        if (rate == PAPP_RATE) memcpy(out, pcm + done, n * 2);
        else for (size_t i = 0; i < n; i++, pos += step) {
            size_t k = (size_t)pos; double f = pos - k;
            int a = pcm[k], b = k + 1 < samples ? pcm[k + 1] : a;
            out[i] = (int16_t)(a + (b - a) * f);
        }
        esp_codec_dev_write(s_spk, out, n * 2);
        done += n;
    }
    // short silence so the amp does not clip the tail
    memset(out, 0, sizeof out);
    esp_codec_dev_write(s_spk, out, sizeof out);
    s_playing = false;
}

bool audio_note_start(int max_sec, audio_wav_cb_t cb, void *ud)
{
    if (s_rec || s_vad_on || s_stream_on || !s_rec_task) return false;
    audio_play_stop();
    s_note_max = max_sec; s_cb = cb; s_cb_ud = ud; s_mode = MODE_NOTE;
    s_rec_t0 = esp_timer_get_time(); s_rec = true;
    xTaskNotifyGive(s_rec_task);
    return true;
}
void audio_note_stop(void) { if (s_mode == MODE_NOTE) s_rec = false; }

bool audio_vad_start(audio_wav_cb_t cb, void *ud)
{
    if (s_rec || s_vad_on || s_stream_on || !s_rec_task) return false;
    s_cb = cb; s_cb_ud = ud; s_mode = MODE_VAD; s_vad_paused = false; s_vad_on = true;
    xTaskNotifyGive(s_rec_task);
    return true;
}
void audio_vad_stop(void) { s_vad_on = false; }
void audio_vad_pause(bool p) { s_vad_paused = p; }
int audio_vad_state(void) { return s_vad_on ? s_vad_state : 0; }

bool audio_stream_start(int chunk_ms, audio_wav_cb_t cb, void *ud)
{
    if (s_rec || s_vad_on || s_stream_on || !s_rec_task) return false;
    audio_play_stop();
    s_stream_bytes = PAPP_RATE * 2 * chunk_ms / 1000;
    s_cb = cb; s_cb_ud = ud; s_mode = MODE_STREAM; s_stream_on = true;
    xTaskNotifyGive(s_rec_task);
    return true;
}
void audio_stream_stop(void) { s_stream_on = false; }
bool audio_streaming(void) { return s_stream_on; }
