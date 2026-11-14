// Kids Games - simple touch games for little ones (age ~3):
// Maze, Draw, Bubbles and Memory, picked from a home screen.
// Board: Elecrow CrowPanel 7.0 (basic, ESP32-S3 N4R8, 4 MB flash). Not for the ADVANCE 7.0.
// Display pin and timing settings follow Elecrow's published LovyanGFX setup for this board.

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <algorithm>
#include <driver/i2s.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

static const int SCREEN_W = 800;
static const int SCREEN_H = 480;
#define PIN_BL     2
#define TOUCH_SDA  19
#define TOUCH_SCL  20

// ---------------------------------------------------------------- display
class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Touch_GT911 _touch;

  void configure(uint8_t touchAddr) {
    {
      auto cfg = _panel.config();
      cfg.memory_width = SCREEN_W;
      cfg.memory_height = SCREEN_H;
      cfg.panel_width = SCREEN_W;
      cfg.panel_height = SCREEN_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0  = GPIO_NUM_15;  // B0
      cfg.pin_d1  = GPIO_NUM_7;   // B1
      cfg.pin_d2  = GPIO_NUM_6;   // B2
      cfg.pin_d3  = GPIO_NUM_5;   // B3
      cfg.pin_d4  = GPIO_NUM_4;   // B4
      cfg.pin_d5  = GPIO_NUM_9;   // G0
      cfg.pin_d6  = GPIO_NUM_46;  // G1
      cfg.pin_d7  = GPIO_NUM_3;   // G2
      cfg.pin_d8  = GPIO_NUM_8;   // G3
      cfg.pin_d9  = GPIO_NUM_16;  // G4
      cfg.pin_d10 = GPIO_NUM_1;   // G5
      cfg.pin_d11 = GPIO_NUM_14;  // R0
      cfg.pin_d12 = GPIO_NUM_21;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_48;  // R3
      cfg.pin_d15 = GPIO_NUM_45;  // R4
      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync = GPIO_NUM_40;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 15000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    if (touchAddr) {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = SCREEN_W - 1;
      cfg.y_min = 0;
      cfg.y_max = SCREEN_H - 1;
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;
      cfg.i2c_addr = touchAddr;
      cfg.pin_sda = TOUCH_SDA;
      cfg.pin_scl = TOUCH_SCL;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;
// The shape helpers (stars, characters) draw onto *gfx: normally the screen,
// or briefly an off-screen sprite so moving pieces can be drawn without blinking.
static lgfx::LovyanGFX *gfx = &tft;
static LGFX_Sprite mazePatch(&tft);
// Whole-screen off-screen copy (PSRAM) for the Draw app and its gallery: screens
// are drawn here first, then shown in one go, so nothing visibly redraws.
static LGFX_Sprite screenSpr(&tft);

// start drawing off-screen (if there's memory for it; otherwise straight to the screen)
static bool offscreenBegin() {
  if (!screenSpr.getBuffer()) return false;
  gfx = &screenSpr;
  return true;
}
// show rows y0..y0+h of the off-screen copy, and go back to drawing on the screen
static void offscreenShow(int y0, int h) {
  gfx = &tft;
  tft.pushImage(0, y0, SCREEN_W, h, (lgfx::swap565_t *)screenSpr.getBuffer() + y0 * SCREEN_W);
}
static Preferences prefs;  // settings kept in flash (sound on/off)

// ---------------------------------------------------------------- colors
static uint16_t C_BG, C_BAR, C_FLOOR, C_WALL, C_FACE, C_FACE_EDGE, C_DARK, C_STAR, C_STAR_EDGE;
static uint16_t RAINBOW[7];

static void initColors() {
  C_BG        = tft.color565(170, 220, 255);  // sky blue
  C_BAR       = tft.color565(120, 190, 250);
  C_FLOOR     = tft.color565(255, 250, 235);  // cream
  C_WALL      = tft.color565(120, 70, 200);   // purple
  C_FACE      = tft.color565(255, 215, 0);
  C_FACE_EDGE = tft.color565(230, 150, 0);
  C_DARK      = tft.color565(40, 30, 60);
  C_STAR      = tft.color565(255, 120, 180);  // pink star
  C_STAR_EDGE = tft.color565(220, 40, 120);
  RAINBOW[0] = tft.color565(255, 80, 80);
  RAINBOW[1] = tft.color565(255, 160, 40);
  RAINBOW[2] = tft.color565(255, 220, 0);
  RAINBOW[3] = tft.color565(80, 200, 90);
  RAINBOW[4] = tft.color565(60, 160, 255);
  RAINBOW[5] = tft.color565(140, 90, 230);
  RAINBOW[6] = tft.color565(230, 90, 200);
}

// ---------------------------------------------------------------- sound
// Speaker connector on the basic CrowPanel 7.0: NS4168 I2S amplifier.
// Sounds are synthesized on the fly (no audio files) by a background task,
// so games never wait for a sound to finish. Harmless with no speaker plugged in.
#define I2S_BCLK 42
#define I2S_LRC  18
#define I2S_DOUT 17
static const int SAMPLE_RATE = 22050;

enum Snd : uint8_t { SND_STEP, SND_WIN, SND_POP, SND_FLIP, SND_MATCH, SND_NOPE,
                     SND_TAP, SND_COLOR, SND_CLEAR, SND_START };
struct Tone { uint16_t f0, f1, ms; uint8_t vol; };  // glide f0 -> f1, f0 = 0 is a rest
struct SoundReq { uint8_t id; uint8_t pitch; };     // pitch in percent (100 = as written)

static const Tone T_STEP[]  = {{660, 660, 70, 35}};
static const Tone T_WIN[]   = {{523, 523, 110, 70}, {659, 659, 110, 70}, {784, 784, 110, 70},
                               {1047, 1047, 160, 75}, {0, 0, 60, 0}, {784, 784, 90, 60}, {1047, 1047, 320, 80}};
static const Tone T_POP[]   = {{900, 280, 70, 70}};
static const Tone T_FLIP[]  = {{1100, 1500, 45, 40}};
static const Tone T_MATCH[] = {{784, 784, 100, 65}, {1047, 1047, 200, 70}};
static const Tone T_NOPE[]  = {{440, 440, 120, 45}, {370, 370, 180, 45}};
static const Tone T_TAP[]   = {{880, 880, 50, 45}};
static const Tone T_COLOR[] = {{1000, 1200, 50, 40}};
static const Tone T_CLEAR[] = {{300, 1300, 220, 45}};
static const Tone T_START[] = {{523, 523, 90, 55}, {784, 784, 160, 60}};

struct Melody { const Tone *tones; uint8_t count; bool interruptible; };
static const Melody MELODIES[] = {
  {T_STEP, 1, true}, {T_WIN, 7, false}, {T_POP, 1, true}, {T_FLIP, 1, true},
  {T_MATCH, 2, false}, {T_NOPE, 2, true}, {T_TAP, 1, true}, {T_COLOR, 1, true},
  {T_CLEAR, 1, true}, {T_START, 2, false},
};

static QueueHandle_t soundQ = nullptr;
static bool muted = false;
// peak sample value (of 32767) for Low / Medium / High
static const int VOLUME_LEVELS[3] = {3500, 9000, 20000};
static int volumeLevel = 1;  // 0 L, 1 M, 2 H
static volatile int masterVolume = 9000;

static void playSound(Snd id, uint8_t pitch = 100) {
  if (muted || !soundQ) return;
  SoundReq r = {id, pitch};
  xQueueSend(soundQ, &r, 0);  // drop it if the queue is full rather than wait
}

static void audioTask(void *) {
  static int16_t buf[256 * 2];  // stereo frames
  float phase = 0;
  SoundReq req;
  for (;;) {
    if (xQueueReceive(soundQ, &req, portMAX_DELAY) != pdTRUE) continue;
    const Melody &m = MELODIES[req.id];
    bool abort = false;
    for (int t = 0; t < m.count && !abort; t++) {
      const Tone &tn = m.tones[t];
      int total = SAMPLE_RATE * tn.ms / 1000;
      int attack = SAMPLE_RATE * 5 / 1000;
      float f0 = tn.f0 * req.pitch / 100.0f, f1 = tn.f1 * req.pitch / 100.0f;
      for (int i = 0; i < total && !abort;) {
        int n = min(256, total - i);
        for (int k = 0; k < n; k++, i++) {
          float s = 0;
          if (tn.f0) {
            float pos = (float)i / total;
            float f = f0 + (f1 - f0) * pos;
            phase += 2 * PI * f / SAMPLE_RATE;
            if (phase > 2 * PI) phase -= 2 * PI;
            // soft attack, bell-like fade out
            float env = i < attack ? (float)i / attack : (1 - pos) * (1 - pos) * 0.6f + (1 - pos) * 0.4f;
            s = (sinf(phase) + 0.2f * sinf(2 * phase)) * env * tn.vol / 100.0f;
          }
          int16_t v = (int16_t)(s * masterVolume);
          buf[2 * k] = v;
          buf[2 * k + 1] = v;
        }
        size_t written;
        i2s_write(I2S_NUM_0, buf, n * 4, &written, portMAX_DELAY);
        // a new sound cuts short a quick one (e.g. fast maze steps), never the win tune
        if (m.interruptible && uxQueueMessagesWaiting(soundQ) > 0) abort = true;
      }
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
  }
}

static void initSound() {
  prefs.begin("kidgames", false);
  muted = prefs.getBool("muted", false);
  volumeLevel = constrain(prefs.getUChar("vol", 1), 0, 2);
  masterVolume = VOLUME_LEVELS[volumeLevel];

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;  // silence, not a buzz, when nothing is playing
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("Sound: I2S init failed");
    return;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);

  soundQ = xQueueCreate(6, sizeof(SoundReq));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);
  Serial.printf("Sound ready (%s, volume %c)\n", muted ? "muted" : "on", "LMH"[volumeLevel]);
}

static void setVolumeLevel(int level) {
  volumeLevel = level;
  masterVolume = VOLUME_LEVELS[level];
  prefs.putUChar("vol", level);  // remembered across power cycles
}

static void setMuted(bool m) {
  muted = m;
  prefs.putBool("muted", m);  // remembered across power cycles
  if (m && soundQ) xQueueReset(soundQ);
}

// ---------------------------------------------------------------- backlight
// Brightness via PWM on the backlight pin, plus screen sleep:
// dims after DIM_AFTER_MS without a touch, turns off after OFF_AFTER_MS.
static const int BL_CHANNEL = 0;
static const uint8_t BRIGHT_LEVELS[3] = {70, 160, 255};  // Low / Medium / High
static int brightLevel = 2;
enum ScreenState { SCREEN_AWAKE, SCREEN_DIM, SCREEN_OFF };
static ScreenState screenState = SCREEN_AWAKE;
static uint32_t lastActivity = 0;
static const uint32_t DIM_AFTER_MS = 2UL * 60 * 1000;
static const uint32_t OFF_AFTER_MS = 5UL * 60 * 1000;

static void setBacklight(uint8_t duty) { ledcWrite(BL_CHANNEL, duty); }

static void initBacklight() {
  brightLevel = constrain(prefs.getUChar("bright", 2), 0, 2);
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_BL, BL_CHANNEL);
  setBacklight(BRIGHT_LEVELS[brightLevel]);
  lastActivity = millis();
}

static void setBrightLevel(int level) {
  brightLevel = level;
  prefs.putUChar("bright", level);  // remembered across power cycles
  setBacklight(BRIGHT_LEVELS[level]);
}

// ---------------------------------------------------------------- maze
struct Level { uint8_t cols, rows; };
static const Level LEVELS[] = {{3, 2}, {4, 2}, {4, 3}, {5, 3}, {5, 4}, {6, 4}, {7, 4}, {7, 5}, {8, 5}};
// Difficulty L / M / H picks a range of maze sizes; each win moves one size up
// within the range: L 3x2 to 4x3, M 4x3 to 6x4, H 6x4 to 8x5.
static const uint8_t BAND_FIRST[3] = {0, 2, 5}, BAND_LAST[3] = {2, 5, 8};
static int mazeDiff = 0;  // 0 L, 1 M, 2 H (remembered)
#define MAXC 8
#define MAXR 5

static const int8_t DX[4] = {0, 1, 0, -1};
static const int8_t DY[4] = {-1, 0, 1, 0};
static const uint8_t BIT[4] = {1, 2, 4, 8};  // N E S W
static const uint8_t OPP[4] = {4, 8, 1, 2};

static uint8_t walls[MAXR][MAXC];  // bit set = wall present
static uint8_t trailCol[MAXR][MAXC];  // rainbow trail color index, 255 = no trail
static int cols, rows;
static int level = 0;
static int starsEarned = 0;
static int px, py;          // player cell
static int gx, gy;          // goal cell
static int steps = 0;

// layout
static const int TOP = 64;
static int cell, ox, oy, wallT;

static void generateMaze() {
  cols = LEVELS[level].cols;
  rows = LEVELS[level].rows;
  bool seen[MAXR][MAXC] = {};
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < cols; x++) { walls[y][x] = 15; trailCol[y][x] = 255; }

  int sx[MAXC * MAXR], sy[MAXC * MAXR], sp = 0;
  sx[sp] = 0; sy[sp] = 0; sp++;
  seen[0][0] = true;
  while (sp > 0) {
    int cx = sx[sp - 1], cy = sy[sp - 1];
    int opts[4], n = 0;
    for (int d = 0; d < 4; d++) {
      int nx = cx + DX[d], ny = cy + DY[d];
      if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && !seen[ny][nx]) opts[n++] = d;
    }
    if (n == 0) { sp--; continue; }
    int d = opts[esp_random() % n];
    int nx = cx + DX[d], ny = cy + DY[d];
    walls[cy][cx] &= ~BIT[d];
    walls[ny][nx] &= ~OPP[d];
    seen[ny][nx] = true;
    sx[sp] = nx; sy[sp] = ny; sp++;
  }

  px = 0; py = 0;
  gx = cols - 1; gy = rows - 1;
  steps = 0;

  int availW = SCREEN_W - 2 * 24;
  int availH = SCREEN_H - TOP - 24;
  cell = min(availW / cols, availH / rows);
  ox = (SCREEN_W - cell * cols) / 2;
  oy = TOP + (SCREEN_H - TOP - cell * rows) / 2;
  wallT = max(10, cell / 9);
}

// BFS distance-limited path step: returns direction of first step toward target, or -1.
static int firstStepToward(int tx, int ty, int maxDist) {
  int dist[MAXR][MAXC];
  int8_t firstDir[MAXR][MAXC];
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < cols; x++) { dist[y][x] = -1; firstDir[y][x] = -1; }
  int qx[MAXC * MAXR], qy[MAXC * MAXR], qh = 0, qt = 0;
  dist[py][px] = 0;
  qx[qt] = px; qy[qt] = py; qt++;
  while (qh < qt) {
    int cx = qx[qh], cy = qy[qh]; qh++;
    if (cx == tx && cy == ty) break;
    for (int d = 0; d < 4; d++) {
      if (walls[cy][cx] & BIT[d]) continue;
      int nx = cx + DX[d], ny = cy + DY[d];
      if (nx < 0 || ny < 0 || nx >= cols || ny >= rows || dist[ny][nx] >= 0) continue;
      dist[ny][nx] = dist[cy][cx] + 1;
      firstDir[ny][nx] = (cx == px && cy == py) ? d : firstDir[cy][cx];
      qx[qt] = nx; qy[qt] = ny; qt++;
    }
  }
  if (dist[ty][tx] <= 0 || dist[ty][tx] > maxDist) return -1;
  return firstDir[ty][tx];
}

// ---------------------------------------------------------------- drawing
static void drawStar(int cx, int cy, int r, uint16_t fill, uint16_t edge) {
  int xs[10], ys[10];
  for (int pass = 0; pass < 2; pass++) {
    int rr = pass == 0 ? r : r - max(3, r / 8);
    uint16_t col = pass == 0 ? edge : fill;
    for (int i = 0; i < 10; i++) {
      float a = -PI / 2 + i * PI / 5;
      float rad = (i % 2) ? rr * 0.45f : rr;
      xs[i] = cx + (int)(cosf(a) * rad);
      ys[i] = cy + (int)(sinf(a) * rad);
    }
    for (int i = 0; i < 10; i++)
      gfx->fillTriangle(cx, cy, xs[i], ys[i], xs[(i + 1) % 10], ys[(i + 1) % 10], col);
  }
}

static void glintEye(int x, int y, int er) {
  gfx->fillCircle(x, y, er, C_DARK);
  gfx->fillCircle(x + er / 3, y - er / 3, max(1, er / 3), TFT_WHITE);
}

static void drawCat(int cx, int cy, int r) {
  uint16_t fur = gfx->color565(255, 160, 60), edge = gfx->color565(200, 110, 30);
  uint16_t pink = gfx->color565(255, 140, 170);
  int hr = r * 0.78f, hy = cy + r * 0.12f;
  // ears (outer, then pink inside)
  for (int s = -1; s <= 1; s += 2) {
    gfx->fillTriangle(cx + s * hr * 0.9f, hy - hr * 0.2f, cx + s * hr * 0.2f, hy - hr * 0.75f,
                     cx + s * hr * 0.75f, hy - hr * 1.25f, edge);
    gfx->fillTriangle(cx + s * hr * 0.75f, hy - hr * 0.35f, cx + s * hr * 0.35f, hy - hr * 0.7f,
                     cx + s * hr * 0.68f, hy - hr * 1.0f, pink);
  }
  gfx->fillCircle(cx, hy, hr, edge);
  gfx->fillCircle(cx, hy, hr - max(3, r / 12), fur);
  int er = max(3, (int)(hr * 0.14f));
  glintEye(cx - hr * 0.38f, hy - hr * 0.12f, er);
  glintEye(cx + hr * 0.38f, hy - hr * 0.12f, er);
  gfx->fillTriangle(cx - hr * 0.12f, hy + hr * 0.12f, cx + hr * 0.12f, hy + hr * 0.12f, cx, hy + hr * 0.26f, pink);
  gfx->fillArc(cx - hr * 0.11f, hy + hr * 0.26f, hr * 0.13f, hr * 0.07f, 0, 180, C_DARK);
  gfx->fillArc(cx + hr * 0.11f, hy + hr * 0.26f, hr * 0.13f, hr * 0.07f, 0, 180, C_DARK);
  float wt = max(1.0f, r / 30.0f);
  for (int s = -1; s <= 1; s += 2) {
    gfx->drawWideLine(cx + s * hr * 0.3f, hy + hr * 0.2f, cx + s * hr * 1.08f, hy + hr * 0.08f, wt, C_DARK);
    gfx->drawWideLine(cx + s * hr * 0.3f, hy + hr * 0.26f, cx + s * hr * 1.08f, hy + hr * 0.36f, wt, C_DARK);
  }
}

static void drawCar(int cx, int cy, int r) {
  uint16_t red = gfx->color565(240, 60, 60), glass = gfx->color565(170, 225, 255);
  uint16_t gray = gfx->color565(190, 190, 200), yellow = gfx->color565(255, 230, 80);
  int bw = r * 1.8f, bh = r * 0.55f;
  int bx = cx - bw / 2, by = cy - bh / 2 + r * 0.15f;
  int cw = r * 1.0f, ch = r * 0.5f;
  int cl = cx - cw / 2 - r * 0.1f, ct = by - ch + r * 0.1f;
  gfx->fillRoundRect(cl, ct, cw, ch + bh / 3, ch / 2, red);          // cabin
  gfx->fillRoundRect(bx, by, bw, bh, bh / 3, red);                   // body
  gfx->fillRoundRect(cl + r * 0.1f, ct + r * 0.1f, cw * 0.38f, ch * 0.55f, 4, glass);
  gfx->fillRoundRect(cl + cw * 0.54f, ct + r * 0.1f, cw * 0.38f, ch * 0.55f, 4, glass);
  gfx->fillCircle(bx + bw - r * 0.12f, by + bh * 0.35f, max(3, (int)(r * 0.08f)), yellow);  // headlight
  // happy face on the front door
  gfx->fillArc(cx + r * 0.1f, by + bh * 0.35f, r * 0.16f, r * 0.1f, 20, 160, C_DARK);
  int wr = r * 0.27f;
  for (int s = -1; s <= 1; s += 2) {
    gfx->fillCircle(cx + s * r * 0.5f, by + bh, wr, C_DARK);
    gfx->fillCircle(cx + s * r * 0.5f, by + bh, wr * 0.45f, gray);
  }
}

static void drawDino(int cx, int cy, int r) {
  uint16_t g = gfx->color565(90, 200, 90), gd = gfx->color565(40, 140, 60);
  uint16_t belly = gfx->color565(200, 240, 170);
  // back spikes first, body covers their bases
  for (int i = 0; i < 3; i++) {
    int x = cx - r * 0.5f + i * r * 0.22f;
    gfx->fillTriangle(x, cy - r * 0.08f, x + r * 0.2f, cy - r * 0.08f, x + r * 0.1f, cy - r * 0.38f, gd);
  }
  gfx->fillTriangle(cx - r * 0.5f, cy + r * 0.05f, cx - r * 0.5f, cy + r * 0.45f, cx - r * 0.97f, cy - r * 0.05f, g);  // tail
  gfx->fillRoundRect(cx - r * 0.4f, cy + r * 0.45f, r * 0.2f, r * 0.35f, 4, g);   // legs
  gfx->fillRoundRect(cx + r * 0.05f, cy + r * 0.45f, r * 0.2f, r * 0.35f, 4, g);
  gfx->fillEllipse(cx - r * 0.1f, cy + r * 0.2f, r * 0.55f, r * 0.4f, g);         // body
  gfx->fillEllipse(cx - r * 0.05f, cy + r * 0.3f, r * 0.32f, r * 0.2f, belly);
  gfx->fillRoundRect(cx + r * 0.15f, cy - r * 0.35f, r * 0.3f, r * 0.6f, r * 0.12f, g);  // neck
  gfx->fillEllipse(cx + r * 0.45f, cy - r * 0.45f, r * 0.38f, r * 0.26f, g);      // head
  glintEye(cx + r * 0.48f, cy - r * 0.54f, max(3, (int)(r * 0.08f)));
  gfx->fillArc(cx + r * 0.55f, cy - r * 0.48f, r * 0.22f, r * 0.16f, 20, 110, C_DARK);
  gfx->fillCircle(cx + r * 0.76f, cy - r * 0.52f, max(1, (int)(r * 0.03f)), gd);
}

static int character = 0;   // 0 cat, 1 car, 2 dino
static int mazeCount = 0;

static void drawPlayer(int cx, int cy, int r) {
  switch (character) {
    case 0: drawCat(cx, cy, r); break;
    case 1: drawCar(cx, cy, r); break;
    default: drawDino(cx, cy, r); break;
  }
}

static void cellCenter(int x, int y, int &cx, int &cy) {
  cx = ox + x * cell + cell / 2;
  cy = oy + y * cell + cell / 2;
}

static int pieceRadius() { return (cell - wallT) / 2 - 6; }

// house icon, top left: back to the home screen
static void drawHomeButton() {
  int bx = 40, by = (TOP - 8) / 2;
  gfx->fillCircle(bx, by, 24, TFT_WHITE);
  gfx->fillTriangle(bx - 17, by - 1, bx + 17, by - 1, bx, by - 17, C_WALL);
  gfx->fillRect(bx - 11, by - 2, 22, 16, C_WALL);
  gfx->fillRect(bx - 4, by + 5, 8, 9, TFT_WHITE);
}

// round-arrow icon, top right: start over
static void drawNewButton() {
  int bx = SCREEN_W - 40, by = (TOP - 8) / 2;
  gfx->fillCircle(bx, by, 24, TFT_WHITE);
  // ring going clockwise from 3 o'clock round to the upper right, gap at the top right
  const float endDeg = 300;
  gfx->fillArc(bx, by, 16, 10, 0, endDeg, C_WALL);
  // arrowhead sits on the end of the ring and points along it (clockwise)
  float a = endDeg * DEG_TO_RAD;
  float rm = 13;                         // middle of the ring
  float ex = bx + cosf(a) * rm, ey = by + sinf(a) * rm;
  float tx = -sinf(a), ty = cosf(a);     // direction of travel
  float nx = cosf(a), ny = sinf(a);      // outward from the center
  gfx->fillTriangle(ex + tx * 10, ey + ty * 10,
                   ex + nx * 9 - tx * 2, ey + ny * 9 - ty * 2,
                   ex - nx * 9 - tx * 2, ey - ny * 9 - ty * 2, C_WALL);
}

// L / M / H difficulty buttons, toward the right of the Maze and Memory top bars
static int diffBtnX(int l) { return 560 + l * 56; }

static void drawDiffButtons(int sel) {
  int y = (TOP - 8) / 2;
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextDatum(textdatum_t::middle_center);
  for (int l = 0; l < 3; l++) {
    int x = diffBtnX(l);
    tft.fillCircle(x, y, 22, l == sel ? C_WALL : TFT_WHITE);
    tft.setTextColor(l == sel ? TFT_WHITE : C_WALL);
    tft.drawString(l == 0 ? "L" : l == 1 ? "M" : "H", x, y + 1);
  }
}

// which difficulty button was tapped, or -1
static int diffPressed(bool pressed, int32_t tx, int32_t ty) {
  if (!pressed || ty >= TOP - 8) return -1;
  for (int l = 0; l < 3; l++)
    if (abs(tx - diffBtnX(l)) < 28) return l;
  return -1;
}

static void drawTopBar() {
  tft.fillRect(0, 0, SCREEN_W, TOP - 8, C_BAR);
  drawHomeButton();
  // earned stars (up to 8 shown)
  int shown = starsEarned % 9;
  for (int i = 0; i < 8; i++) {
    int cx = 116 + i * 50, cy = (TOP - 8) / 2;
    if (i < shown) drawStar(cx, cy, 22, C_FACE, C_FACE_EDGE);
    else tft.fillCircle(cx, cy, 6, tft.color565(200, 230, 255));
  }
  drawDiffButtons(mazeDiff);
  drawNewButton();
}

static void drawMaze() {
  tft.startWrite();
  tft.fillScreen(C_BG);
  drawTopBar();
  int pad = wallT;
  tft.fillRoundRect(ox - pad, oy - pad, cell * cols + 2 * pad, cell * rows + 2 * pad, 18, C_FLOOR);
  int h = wallT / 2;
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      int x0 = ox + x * cell, y0 = oy + y * cell;
      uint8_t w = walls[y][x];
      if (w & 1) tft.fillRoundRect(x0 - h, y0 - h, cell + wallT, wallT, h, C_WALL);
      if (w & 4) tft.fillRoundRect(x0 - h, y0 + cell - h, cell + wallT, wallT, h, C_WALL);
      if (w & 8) tft.fillRoundRect(x0 - h, y0 - h, wallT, cell + wallT, h, C_WALL);
      if (w & 2) tft.fillRoundRect(x0 + cell - h, y0 - h, wallT, cell + wallT, h, C_WALL);
    }
  }
  int cx, cy;
  cellCenter(gx, gy, cx, cy);
  drawStar(cx, cy, pieceRadius(), C_STAR, C_STAR_EDGE);
  cellCenter(px, py, cx, cy);
  drawPlayer(cx, cy, pieceRadius());
  tft.endWrite();
}

// Redraw a rectangle inside the maze (it must stay clear of the walls): floor,
// trail dots, the goal star (optional) and the character at (pcx, pcy).
// Everything is drawn off-screen and copied in one go, so nothing blinks.
static void renderPatch(int x0, int y0, int w, int h, int pcx, int pcy, int pr, bool showStar) {
  bool offscreen = mazePatch.createSprite(w, h);
  int dx = 0, dy = 0;
  if (offscreen) {
    gfx = &mazePatch;
    dx = -x0; dy = -y0;
    mazePatch.fillScreen(C_FLOOR);
  } else {  // out of memory: fall back to drawing straight on the screen
    tft.setClipRect(x0, y0, w, h);
    tft.fillRect(x0, y0, w, h, C_FLOOR);
  }
  int cx, cy;
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < cols; x++)
      if (trailCol[y][x] != 255) {
        cellCenter(x, y, cx, cy);
        gfx->fillCircle(cx + dx, cy + dy, max(6, cell / 9), RAINBOW[trailCol[y][x]]);
      }
  if (showStar) {
    cellCenter(gx, gy, cx, cy);
    drawStar(cx + dx, cy + dy, pieceRadius(), C_STAR, C_STAR_EDGE);
  }
  drawPlayer(pcx + dx, pcy + dy, pr);
  gfx = &tft;
  if (offscreen) {
    mazePatch.pushSprite(x0, y0);
    mazePatch.deleteSprite();
  } else {
    tft.clearClipRect();
  }
}

static void movePlayer(int d) {
  int ax, ay, bx, by;
  cellCenter(px, py, ax, ay);
  trailCol[py][px] = steps % 7;  // leave a rainbow dot behind
  int nx = px + DX[d], ny = py + DY[d];
  cellCenter(nx, ny, bx, by);
  // the patch covers both squares (minus their walls) so the glide fits inside it
  int in = wallT / 2 + 2;
  int x0 = ox + min(px, nx) * cell + in, y0 = oy + min(py, ny) * cell + in;
  int w = (abs(nx - px) + 1) * cell - 2 * in, h = (abs(ny - py) + 1) * cell - 2 * in;
  px = nx;
  py = ny;
  steps++;
  bool intoGoal = (px == gx && py == gy);
  // glide speed: about a quarter second per square (FRAMES x ~20 ms)
  const int FRAMES = 12;
  tft.startWrite();
  for (int f = 1; f <= FRAMES; f++) {
    float t = (float)f / FRAMES;
    float e = t * t * (3 - 2 * t);  // ease in and out
    renderPatch(x0, y0, w, h, ax + (bx - ax) * e, ay + (by - ay) * e, pieceRadius(),
                !(intoGoal && f == FRAMES));
    delay(16);
  }
  tft.endWrite();
}

// confetti burst of little stars and dots
static void confetti(int count) {
  for (int i = 0; i < count; i++) {
    int x = 20 + esp_random() % (SCREEN_W - 40);
    int y = TOP + esp_random() % (SCREEN_H - TOP - 20);
    uint16_t col = RAINBOW[esp_random() % 7];
    tft.startWrite();
    if (i % 4 == 0) drawStar(x, y, 14 + esp_random() % 14, col, col);
    else tft.fillCircle(x, y, 5 + esp_random() % 8, col);
    tft.endWrite();
    delay(10);
  }
}

static void celebrate() {
  int cx, cy;
  cellCenter(gx, gy, cx, cy);
  // character does a little happy bounce: grows and shrinks smoothly, 3 times
  int in = wallT / 2 + 2;
  int x0 = ox + gx * cell + in, y0 = oy + gy * cell + in, sz = cell - 2 * in;
  tft.startWrite();
  for (int i = 0; i < 3 * 12; i++) {
    float s = sinf((i % 12) * PI / 12);  // 0 -> 1 -> 0
    renderPatch(x0, y0, sz, sz, cx, cy, pieceRadius() + (int)(s * 5), false);
    delay(20);
  }
  tft.endWrite();
  confetti(140);
  // big happy face in the middle
  tft.startWrite();
  tft.fillCircle(SCREEN_W / 2, (SCREEN_H + TOP) / 2, 140, TFT_WHITE);
  drawPlayer(SCREEN_W / 2, (SCREEN_H + TOP) / 2, 125);
  tft.endWrite();
  starsEarned++;
  tft.startWrite();
  drawTopBar();
  tft.endWrite();
  delay(1800);
}

// ---------------------------------------------------------------- touch setup
// Some board revisions (V3.x) hold the GT911 reset/INT on a PCA9557 I/O expander at 0x18.
// Pulse it if present; harmless otherwise. Then find which address the GT911 answers on.
static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t findTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  delay(10);
  if (i2cPresent(0x18)) {
    Serial.println("PCA9557 found, resetting touch");
    writeReg(0x18, 0x01, 0x00);  // outputs low (RST=IO0, INT=IO1)
    writeReg(0x18, 0x03, 0xFC);  // IO0, IO1 as outputs
    delay(20);
    writeReg(0x18, 0x01, 0x01);  // release reset
    delay(100);
    writeReg(0x18, 0x03, 0xFE);  // IO1 (INT) back to input
    delay(50);
  }
  uint8_t addr = 0;
  for (int tries = 0; tries < 40 && !addr; tries++) {  // GT911 can take a while to wake
    if (i2cPresent(0x5D)) addr = 0x5D;
    else if (i2cPresent(0x14)) addr = 0x14;
    else delay(25);
  }
  Serial.print("I2C devices:");
  for (uint8_t a = 1; a < 127; a++)
    if (i2cPresent(a)) Serial.printf(" 0x%02X", a);
  Serial.println();
  Serial.printf("GT911 touch at 0x%02X\n", addr);
  Wire.end();
  return addr;
}

// ---------------------------------------------------------------- shared
enum App { APP_HOME, APP_MAZE, APP_DRAW, APP_BUBBLES, APP_MEMORY, APP_GALLERY };
static App app = APP_HOME;
static const int BAR = TOP - 8;  // height of the top bar
static bool ignoreUntilRelease = false;

static bool homePressed(bool pressed, int32_t tx, int32_t ty) {
  return pressed && ty < BAR && tx < 90;
}
static bool newPressed(bool pressed, int32_t tx, int32_t ty) {
  return pressed && ty < BAR && tx > SCREEN_W - 90;
}

static void waitForRelease() {
  int32_t a, b;
  while (tft.getTouch(&a, &b)) delay(20);
}

static void drawHeart(int cx, int cy, int r) {
  uint16_t col = tft.color565(240, 60, 100);
  tft.fillCircle(cx - r * 0.45f, cy - r * 0.2f, r * 0.5f, col);
  tft.fillCircle(cx + r * 0.45f, cy - r * 0.2f, r * 0.5f, col);
  tft.fillTriangle(cx - r * 0.93f, cy - r * 0.02f, cx + r * 0.93f, cy - r * 0.02f, cx, cy + r * 0.9f, col);
  tft.fillCircle(cx - r * 0.5f, cy - r * 0.35f, max(2, (int)(r * 0.12f)), TFT_WHITE);
}

static void drawBall(int cx, int cy, int r) {
  tft.fillArc(cx, cy, r, 0, 0, 120, RAINBOW[0]);
  tft.fillArc(cx, cy, r, 0, 120, 240, RAINBOW[2]);
  tft.fillArc(cx, cy, r, 0, 240, 360, RAINBOW[4]);
  tft.fillCircle(cx, cy, r * 0.18f, TFT_WHITE);
}

// draws on any target: the screen, or an off-screen sprite
static void drawBubble(lgfx::LovyanGFX &g, int x, int y, int r, uint16_t col) {
  g.fillCircle(x, y, r, col);
  g.drawCircle(x, y, r, TFT_WHITE);
  g.drawCircle(x, y, r - 1, TFT_WHITE);
  g.fillCircle(x - r * 0.35f, y - r * 0.35f, max(3, (int)(r * 0.2f)), TFT_WHITE);
  g.fillCircle(x - r * 0.1f, y - r * 0.55f, max(2, (int)(r * 0.08f)), TFT_WHITE);
}

// 0 star, 1 cat, 2 car, 3 dino, 4 heart, 5 ball
static void drawSymbol(int s, int cx, int cy, int r) {
  switch (s) {
    case 0: drawStar(cx, cy, r, C_FACE, C_FACE_EDGE); break;
    case 1: drawCat(cx, cy, r); break;
    case 2: drawCar(cx, cy, r); break;
    case 3: drawDino(cx, cy, r); break;
    case 4: drawHeart(cx, cy, r); break;
    default: drawBall(cx, cy, r); break;
  }
}

static void drawPlainBar() {
  gfx->fillRect(0, 0, SCREEN_W, BAR, C_BAR);
  drawHomeButton();
}

// ---------------------------------------------------------------- home screen
static const int TILE_W = 370, TILE_H = 196;
static int tileX(int i) { return 20 + (i % 2) * 390; }
static int tileY(int i) { return 62 + (i / 2) * 208; }

// Top right of the home screen: L / M / H volume buttons, then a speaker icon
// that turns sound on/off.
static const int SOUND_BX = SCREEN_W - 40, SOUND_BY = 32;
static int volBtnX(int level) { return SCREEN_W - 208 + level * 56; }

static void drawSoundButton() {
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextDatum(textdatum_t::middle_center);
  for (int l = 0; l < 3; l++) {
    bool on = (l == volumeLevel) && !muted;
    int x = volBtnX(l);
    tft.fillCircle(x, SOUND_BY, 22, on ? C_WALL : TFT_WHITE);
    tft.setTextColor(on ? TFT_WHITE : C_WALL);
    tft.drawString(l == 0 ? "L" : l == 1 ? "M" : "H", x, SOUND_BY + 1);
  }
  int bx = SOUND_BX, by = SOUND_BY;
  tft.fillCircle(bx, by, 24, TFT_WHITE);
  tft.fillRect(bx - 14, by - 6, 8, 12, C_WALL);                                // speaker body
  tft.fillTriangle(bx - 7, by - 6, bx - 7, by + 6, bx + 3, by - 13, C_WALL);   // cone
  tft.fillTriangle(bx - 7, by + 6, bx + 3, by + 13, bx + 3, by - 13, C_WALL);
  if (muted) {
    uint16_t red = tft.color565(230, 60, 60);
    tft.drawWideLine(bx + 7, by - 7, bx + 17, by + 7, 2.5f, red);
    tft.drawWideLine(bx + 7, by + 7, bx + 17, by - 7, 2.5f, red);
  } else {
    tft.fillArc(bx + 1, by, 11, 8, 315, 360, C_WALL);  // sound waves
    tft.fillArc(bx + 1, by, 11, 8, 0, 45, C_WALL);
    tft.fillArc(bx + 1, by, 18, 15, 315, 360, C_WALL);
    tft.fillArc(bx + 1, by, 18, 15, 0, 45, C_WALL);
  }
}

// Top left of the home screen: a sun icon, then L / M / H brightness buttons
// (mirrors the sound controls on the right).
static const int BRIGHT_BY = 32;
static int brightBtnX(int level) { return 96 + level * 56; }

static void drawBrightButtons() {
  int sx = 40, sy = BRIGHT_BY;
  tft.fillCircle(sx, sy, 24, TFT_WHITE);
  for (int k = 0; k < 8; k++) {  // sun rays
    float a = k * PI / 4;
    tft.drawWideLine(sx + cosf(a) * 12, sy + sinf(a) * 12, sx + cosf(a) * 18, sy + sinf(a) * 18, 2, C_FACE_EDGE);
  }
  tft.fillCircle(sx, sy, 9, C_FACE);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextDatum(textdatum_t::middle_center);
  for (int l = 0; l < 3; l++) {
    bool on = (l == brightLevel);
    int x = brightBtnX(l);
    tft.fillCircle(x, BRIGHT_BY, 22, on ? C_WALL : TFT_WHITE);
    tft.setTextColor(on ? TFT_WHITE : C_WALL);
    tft.drawString(l == 0 ? "L" : l == 1 ? "M" : "H", x, BRIGHT_BY + 1);
  }
}

static void drawHome() {
  static const char* labels[4] = {"Maze", "Draw", "Bubbles", "Memory"};
  uint16_t fills[4] = {tft.color565(215, 195, 255), tft.color565(255, 222, 175),
                       tft.color565(205, 238, 255), tft.color565(255, 205, 228)};
  tft.startWrite();
  tft.fillScreen(C_BG);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.setTextColor(C_WALL);
  tft.drawString("Let's Play!", SCREEN_W / 2, 32);
  drawBrightButtons();
  drawSoundButton();
  for (int i = 0; i < 4; i++) {
    int x = tileX(i), y = tileY(i);
    tft.fillRoundRect(x, y, TILE_W, TILE_H, 28, C_WALL);
    tft.fillRoundRect(x + 6, y + 6, TILE_W - 12, TILE_H - 12, 24, fills[i]);
    int icx = x + TILE_W / 2, icy = y + 78;
    switch (i) {
      case 0:  // maze: cat goes to the star
        drawCat(icx - 90, icy, 52);
        for (int k = 0; k < 3; k++) tft.fillCircle(icx - 22 + k * 24, icy, 7, RAINBOW[k * 2]);
        drawStar(icx + 90, icy, 50, C_STAR, C_STAR_EDGE);
        break;
      case 1:  // draw: rainbow squiggles
        for (int k = 0; k < 3; k++)
          for (int j = 0; j < 11; j++) {
            float x0 = icx - 110 + j * 20, x1 = x0 + 20;
            float y0 = icy - 35 + k * 35 + sinf(j * 0.9f) * 12, y1 = icy - 35 + k * 35 + sinf((j + 1) * 0.9f) * 12;
            tft.drawWideLine(x0, y0, x1, y1, 7, RAINBOW[k * 2 + 1 > 6 ? 6 : k * 2]);
          }
        break;
      case 2:  // bubbles
        drawBubble(tft, icx - 80, icy + 15, 38, RAINBOW[4]);
        drawBubble(tft, icx, icy - 20, 30, RAINBOW[6]);
        drawBubble(tft, icx + 75, icy + 10, 42, RAINBOW[3]);
        drawBubble(tft, icx + 15, icy + 40, 18, RAINBOW[1]);
        break;
      case 3:  // memory: one card face down, one showing a star
        tft.fillRoundRect(icx - 115, icy - 60, 95, 120, 14, C_WALL);
        tft.setFont(&fonts::FreeSansBold24pt7b);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("?", icx - 67, icy);
        tft.fillRoundRect(icx + 20, icy - 60, 95, 120, 14, C_WALL);
        tft.fillRoundRect(icx + 25, icy - 55, 85, 110, 11, TFT_WHITE);
        drawStar(icx + 67, icy, 36, C_FACE, C_FACE_EDGE);
        break;
    }
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextColor(C_WALL);
    tft.drawString(labels[i], x + TILE_W / 2, y + TILE_H - 30);
  }
  tft.endWrite();
}

static void startMaze();
static void startDraw();
static void startBubbles();
static void startMemory();

static void enterApp(App a) {
  app = a;
  ignoreUntilRelease = true;
  switch (a) {
    case APP_HOME: drawHome(); break;
    case APP_MAZE: startMaze(); break;
    case APP_DRAW: startDraw(); break;
    case APP_BUBBLES: startBubbles(); break;
    case APP_MEMORY: startMemory(); break;
    case APP_GALLERY: break;  // opened from Draw
  }
}

static void homeTick(bool pressed, int32_t tx, int32_t ty) {
  if (!pressed) return;
  int sx = tx - SOUND_BX, sy = ty - SOUND_BY;
  if (sx * sx + sy * sy < 34 * 34) {
    setMuted(!muted);
    tft.startWrite();
    drawSoundButton();
    tft.endWrite();
    playSound(SND_TAP);  // only heard when sound was just turned on
    return;
  }
  for (int l = 0; l < 3; l++) {
    int bx = tx - brightBtnX(l), by = ty - BRIGHT_BY;
    if (bx * bx + by * by < 27 * 27) {  // pick a brightness
      setBrightLevel(l);
      tft.startWrite();
      drawBrightButtons();
      tft.endWrite();
      playSound(SND_TAP);
      return;
    }
  }
  for (int l = 0; l < 3; l++) {
    int vx = tx - volBtnX(l), vy = ty - SOUND_BY;
    if (vx * vx + vy * vy < 27 * 27) {  // pick a volume (also un-mutes)
      setVolumeLevel(l);
      if (muted) setMuted(false);
      tft.startWrite();
      drawSoundButton();
      tft.endWrite();
      playSound(SND_TAP);  // test blip at the new volume
      return;
    }
  }
  for (int i = 0; i < 4; i++) {
    if (tx >= tileX(i) && tx < tileX(i) + TILE_W && ty >= tileY(i) && ty < tileY(i) + TILE_H) {
      static const App apps[4] = {APP_MAZE, APP_DRAW, APP_BUBBLES, APP_MEMORY};
      playSound(SND_TAP);
      enterApp(apps[i]);
      return;
    }
  }
}

// ---------------------------------------------------------------- maze app
static uint32_t lastStep = 0;

static void startMaze() {
  character = mazeCount++ % 3;  // cat, car, dino take turns
  generateMaze();
  drawMaze();
}

static void mazeTick(bool touching, bool pressed, int32_t tx, int32_t ty) {
  if (homePressed(pressed, tx, ty)) { playSound(SND_TAP); enterApp(APP_HOME); return; }
  if (newPressed(pressed, tx, ty)) { playSound(SND_TAP); startMaze(); return; }
  int dl = diffPressed(pressed, tx, ty);
  if (dl >= 0) {
    playSound(SND_TAP);
    if (dl != mazeDiff) {  // new difficulty: a new maze at the smallest size of that range
      mazeDiff = dl;
      prefs.putUChar("mazeDiff", dl);
      level = BAND_FIRST[mazeDiff];
      startMaze();
    }
    return;
  }
  if (!touching || ty < oy || tx < ox || millis() - lastStep < 70) return;
  int cx = (tx - ox) / cell;
  int cy = (ty - oy) / cell;
  if (cx < 0 || cy < 0 || cx >= cols || cy >= rows || (cx == px && cy == py)) return;
  // forgiving: follow the finger if it's within 2 steps along the path
  int d = firstStepToward(cx, cy, 2);
  if (d < 0) return;
  // each step plays the next note of a little pentatonic tune
  static const uint8_t STEP_PITCH[5] = {100, 112, 126, 150, 168};
  playSound(SND_STEP, STEP_PITCH[steps % 5]);
  movePlayer(d);
  lastStep = millis();
  if (px == gx && py == gy) {
    playSound(SND_WIN);
    celebrate();
    if (level < BAND_LAST[mazeDiff]) level++;
    startMaze();
    waitForRelease();
  }
}

// ---------------------------------------------------------------- draw app
static const int NSW = 9;  // 7 rainbow, dark, eraser (white = the paper)
static const int PAPER = NSW - 1;
static int drawCol = 0;
static bool stroking = false;
static int lastX, lastY;

static uint16_t swatchColor(int i) {
  if (i < 7) return RAINBOW[i];
  if (i == 7) return C_DARK;
  return TFT_WHITE;
}
static int swatchX(int i) { return 108 + i * 52; }
static const int SAVE_BX = 600, GALLERY_BX = 680;

// camera: "take a picture" of the drawing
static void drawSaveIcon(int cx, int cy) {
  gfx->fillCircle(cx, cy, 24, TFT_WHITE);
  gfx->fillRoundRect(cx - 16, cy - 8, 32, 21, 5, C_WALL);
  gfx->fillRoundRect(cx - 7, cy - 13, 14, 7, 2, C_WALL);
  gfx->fillCircle(cx, cy + 2, 8, TFT_WHITE);
  gfx->fillCircle(cx, cy + 2, 5, RAINBOW[4]);
  gfx->fillCircle(cx + 11, cy - 4, 2, C_FACE);
}

// a framed picture: the saved drawings
static void drawGalleryIcon(int cx, int cy) {
  gfx->fillCircle(cx, cy, 24, TFT_WHITE);
  gfx->fillRoundRect(cx - 16, cy - 13, 32, 26, 4, C_WALL);
  gfx->fillRect(cx - 12, cy - 9, 24, 18, C_BG);
  gfx->fillTriangle(cx - 12, cy + 9, cx - 3, cy - 3, cx + 6, cy + 9, RAINBOW[3]);
  gfx->fillTriangle(cx - 1, cy + 9, cx + 5, cy + 1, cx + 12, cy + 9, gfx->color565(40, 150, 60));
  gfx->fillCircle(cx + 6, cy - 4, 3, C_FACE);
}

static void renderDrawBar() {
  gfx->startWrite();
  drawPlainBar();
  int y = BAR / 2;
  for (int i = 0; i < NSW; i++) {
    int x = swatchX(i);
    gfx->fillCircle(x, y, 25, i == drawCol ? C_DARK : C_BAR);
    gfx->fillCircle(x, y, 20, swatchColor(i));
    if (i == NSW - 1) {  // eraser: white with a little pink band
      gfx->fillRoundRect(x - 13, y - 5, 26, 10, 4, C_STAR);
    }
  }
  drawSaveIcon(SAVE_BX, y);
  drawGalleryIcon(GALLERY_BX, y);
  drawNewButton();
  gfx->endWrite();
}

static void drawDrawBar() {
  bool off = offscreenBegin();
  renderDrawBar();
  if (off) offscreenShow(0, BAR);
}

// ---- saved drawings
// A drawing is kept as one palette index per pixel (the 9 paint colors), and
// saved run-length packed, so mostly-white pictures stay small (a few KB).
// Files are /kidsdraw/<number>.kd; a higher number is newer.
// With a microSD card in the slot, drawings go on the card (up to 60). Without
// one they go in the board's own flash (up to 6). Either way, saving past the
// limit drops the oldest. Drawings already in flash move to the card when one
// is put in.
static const int CANVAS_H = SCREEN_H - BAR;
static const int CANVAS_PX = SCREEN_W * CANVAS_H;
static const char *SAVE_DIR = "/kidsdraw";
// A drawing is a few KB, so a card could hold far more than 1000; the cap keeps
// the gallery quick to open (it reads the whole folder).
static const int FLASH_MAX_SAVES = 6, SD_MAX_SAVES = 1000;
#define SD_CS   10  // microSD slot (SPI)
#define SD_MOSI 11
#define SD_SCK  12
#define SD_MISO 13

static uint8_t *canvasIdx = nullptr;  // the drawing on screen (PSRAM)
static uint8_t *loadIdx = nullptr;    // scratch for reading saves (PSRAM)
static uint16_t *canvasBackup = nullptr;  // exact copy of the drawing while the gallery is open
static bool haveBackup = false;
static bool flashReady = false, sdReady = false;
static uint32_t lastSdTry = 0;
static fs::FS *store = nullptr;       // where drawings go right now
static int maxSaves = 0;
static std::vector<int> saveIds;      // drawings in the store, newest first
static fs::FS *listedStore = nullptr; // which store saveIds was read from
static uint16_t pal565[NSW];
static uint8_t palR[NSW], palG[NSW], palB[NSW];

static String savePath(int id) { return String(SAVE_DIR) + "/" + id + ".kd"; }

static void listIds(fs::FS &fs, std::vector<int> &ids) {
  ids.clear();
  File dir = fs.open(SAVE_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (!f.isDirectory() && n.endsWith(".kd")) {
      int id = n.toInt();
      if (id > 0) ids.push_back(id);
    }
  }
  std::sort(ids.begin(), ids.end(), std::greater<int>());
}

static bool mountSD() {
  static bool spiStarted = false;
  if (!spiStarted) { SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS); spiStarted = true; }
  lastSdTry = millis();
  if (!SD.begin(SD_CS, SPI, 20000000) || SD.cardType() == CARD_NONE ||
      (!SD.exists(SAVE_DIR) && !SD.mkdir(SAVE_DIR))) {
    SD.end();
    return false;
  }
  Serial.printf("SD card: %u MB\n", (unsigned)(SD.cardSize() / (1024 * 1024)));
  return true;
}

static bool copyFile(fs::FS &from, const String &src, fs::FS &to, const String &dst) {
  File in = from.open(src, "r");
  if (!in) return false;
  File out = to.open(dst, "w");
  if (!out) { in.close(); return false; }
  static uint8_t buf[1024];
  bool ok = true;
  size_t n;
  while (ok && (n = in.read(buf, sizeof(buf))) > 0) ok = out.write(buf, n) == n;
  in.close();
  out.close();
  if (!ok) to.remove(dst);
  return ok;
}

// drawings saved in flash while there was no card: move them onto the card, oldest first
static bool moveFlashToSD() {
  if (!flashReady) return false;
  std::vector<int> fromFlash, onCard;
  listIds(LittleFS, fromFlash);
  if (fromFlash.empty()) return false;
  listIds(SD, onCard);
  int next = onCard.empty() ? 1 : onCard[0] + 1;
  for (int i = (int)fromFlash.size() - 1; i >= 0; i--) {
    if (copyFile(LittleFS, savePath(fromFlash[i]), SD, savePath(next))) {
      LittleFS.remove(savePath(fromFlash[i]));
      next++;
    }
  }
  Serial.printf("Moved %u drawings to the SD card\n", (unsigned)fromFlash.size());
  return true;
}

// pick the card if there is one (it can be put in or taken out any time), else flash
static void chooseStore() {
  if (sdReady && !SD.exists(SAVE_DIR)) { SD.end(); sdReady = false; }  // card taken out
  if (!sdReady && (store != &LittleFS || millis() - lastSdTry > 3000)) sdReady = mountSD();
  bool moved = false;
  if (sdReady) { store = &SD; maxSaves = SD_MAX_SAVES; moved = moveFlashToSD(); }
  else if (flashReady) { store = &LittleFS; maxSaves = FLASH_MAX_SAVES; }
  else store = nullptr;
  // re-read the folder only when the store changed (it can hold up to 1000 files)
  if (store != listedStore || moved) {
    if (store) listIds(*store, saveIds);
    else saveIds.clear();
    listedStore = store;
  }
}

static void initDrawStorage() {
  for (int i = 0; i < NSW; i++) {
    uint16_t c = swatchColor(i);
    pal565[i] = c;
    palR[i] = ((c >> 11) & 31) * 255 / 31;
    palG[i] = ((c >> 5) & 63) * 255 / 63;
    palB[i] = (c & 31) * 255 / 31;
  }
  canvasIdx = (uint8_t *)ps_malloc(CANVAS_PX);
  loadIdx = (uint8_t *)ps_malloc(CANVAS_PX);
  canvasBackup = (uint16_t *)ps_malloc(CANVAS_PX * 2);
  screenSpr.setPsram(true);
  screenSpr.setColorDepth(16);
  if (!screenSpr.createSprite(SCREEN_W, SCREEN_H)) Serial.println("No memory for off-screen drawing");
  if (!canvasIdx || !loadIdx) { Serial.println("Drawing storage not available"); return; }
  flashReady = LittleFS.begin(true);  // formats on first use
  if (flashReady && !LittleFS.exists(SAVE_DIR)) LittleFS.mkdir(SAVE_DIR);
  chooseStore();
  Serial.printf("Drawings: %u saved on %s\n", (unsigned)saveIds.size(),
                store == &SD ? "the SD card" : store ? "flash" : "nothing (no storage)");
}

// read the canvas back from the screen, snapping each pixel to the nearest paint color
static void captureCanvas(uint8_t *dst) {
  static lgfx::rgb888_t row[SCREEN_W];
  for (int y = 0; y < CANVAS_H; y++) {
    tft.readRect(0, BAR + y, SCREEN_W, 1, row);
    uint8_t *out = dst + y * SCREEN_W;
    int lr = -1, lg = -1, lb = -1, li = PAPER;
    for (int x = 0; x < SCREEN_W; x++) {
      int r = row[x].r, g = row[x].g, b = row[x].b;
      if (r != lr || g != lg || b != lb) {  // same as the last pixel: reuse the answer
        int best = PAPER, bestD = INT32_MAX;
        for (int i = 0; i < NSW; i++) {
          int dr = r - palR[i], dg = g - palG[i], db = b - palB[i];
          int d = dr * dr + dg * dg + db * db;
          if (d < bestD) { bestD = d; best = i; }
        }
        lr = r; lg = g; lb = b; li = best;
      }
      out[x] = li;
    }
  }
}

static void paintCanvas(const uint8_t *src) {
  gfx->startWrite();
  for (int y = 0; y < CANVAS_H; y++) {
    const uint8_t *row = src + y * SCREEN_W;
    int x = 0;
    while (x < SCREEN_W) {
      int len = 1;
      while (x + len < SCREEN_W && row[x + len] == row[x]) len++;
      gfx->writeFastHLine(x, BAR + y, len, pal565[row[x]]);
      x += len;
    }
  }
  gfx->endWrite();
}

static bool canvasBlank(const uint8_t *src) {
  for (int i = 0; i < CANVAS_PX; i++) if (src[i] != PAPER) return false;
  return true;
}

// file: "KD", version 1, canvas height (2 bytes), then (color, run length) pairs
static bool writeDrawing(fs::FS &fs, const String &path, const uint8_t *src) {
  File f = fs.open(path, "w");
  if (!f) return false;
  uint8_t hdr[5] = {'K', 'D', 1, (uint8_t)(CANVAS_H & 0xFF), (uint8_t)(CANVAS_H >> 8)};
  bool ok = f.write(hdr, 5) == 5;
  static uint8_t buf[512];
  int n = 0;
  for (int y = 0; y < CANVAS_H && ok; y++) {
    const uint8_t *row = src + y * SCREEN_W;
    int x = 0;
    while (x < SCREEN_W) {
      int len = 1;
      while (x + len < SCREEN_W && len < 255 && row[x + len] == row[x]) len++;
      buf[n++] = row[x];
      buf[n++] = len;
      x += len;
      if (n >= (int)sizeof(buf)) { ok = ok && f.write(buf, n) == (size_t)n; n = 0; }
    }
  }
  if (n && ok) ok = f.write(buf, n) == (size_t)n;
  f.close();
  return ok;
}

static bool readDrawing(fs::FS &fs, const String &path, uint8_t *dst) {
  File f = fs.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  if (sz < 7 || sz > 5 + 2 * (size_t)CANVAS_PX) { f.close(); return false; }
  uint8_t *data = (uint8_t *)ps_malloc(sz);
  if (!data) { f.close(); return false; }
  bool ok = f.read(data, sz) == sz;
  f.close();
  ok = ok && data[0] == 'K' && data[1] == 'D' && data[2] == 1 &&
       (data[3] | (data[4] << 8)) == CANVAS_H;
  size_t p = 5;
  int out = 0;
  while (ok && p + 1 < sz && out < CANVAS_PX) {
    uint8_t c = data[p], len = data[p + 1];
    p += 2;
    if (c >= NSW || len == 0 || out + len > CANVAS_PX) { ok = false; break; }
    memset(dst + out, c, len);
    out += len;
  }
  free(data);
  return ok && out == CANVAS_PX;
}

static bool saveDrawing(const uint8_t *src) {
  chooseStore();
  if (!store) return false;
  int id = saveIds.empty() ? 1 : saveIds[0] + 1;
  bool ok = writeDrawing(*store, savePath(id), src);
  for (int tries = 0; !ok && tries < 3 && !saveIds.empty(); tries++) {  // full: make room, try again
    store->remove(savePath(id));
    store->remove(savePath(saveIds.back()));
    saveIds.pop_back();
    ok = writeDrawing(*store, savePath(id), src);
  }
  if (!ok) { store->remove(savePath(id)); return false; }
  saveIds.insert(saveIds.begin(), id);
  while ((int)saveIds.size() > maxSaves) {
    store->remove(savePath(saveIds.back()));
    saveIds.pop_back();
  }
  return true;
}

static void startDraw() {
  bool off = offscreenBegin();
  gfx->fillScreen(TFT_WHITE);
  renderDrawBar();
  if (off) offscreenShow(0, SCREEN_H);
  stroking = false;
}

// keep an exact copy of the drawing on screen before the gallery covers it
static void backupCanvas() {
  haveBackup = canvasBackup != nullptr;
  if (haveBackup) tft.readRect(0, BAR, SCREEN_W, CANVAS_H, (lgfx::swap565_t *)canvasBackup);
  else captureCanvas(canvasIdx);
}

// back to drawing: the picture from before the gallery (fromBackup), or the one
// just opened from it (in canvasIdx). Built off-screen, then shown in one go.
static void resumeDraw(bool fromBackup) {
  app = APP_DRAW;
  ignoreUntilRelease = true;
  stroking = false;
  bool off = offscreenBegin();
  if (fromBackup && haveBackup) {
    if (off) memcpy((uint16_t *)screenSpr.getBuffer() + BAR * SCREEN_W, canvasBackup, CANVAS_PX * 2);
    else tft.pushImage(0, BAR, SCREEN_W, CANVAS_H, (lgfx::swap565_t *)canvasBackup);
  } else {
    paintCanvas(canvasIdx);
  }
  renderDrawBar();
  if (off) offscreenShow(0, SCREEN_H);
}

static void startGallery();

static void showSaved(bool ok) {
  int y = BAR / 2;
  tft.startWrite();
  tft.fillCircle(SAVE_BX, y, 24, ok ? RAINBOW[3] : RAINBOW[0]);
  if (ok) {  // a check mark
    tft.drawWideLine(SAVE_BX - 11, y + 1, SAVE_BX - 3, y + 9, 3, TFT_WHITE);
    tft.drawWideLine(SAVE_BX - 3, y + 9, SAVE_BX + 12, y - 8, 3, TFT_WHITE);
  } else {   // an X
    tft.drawWideLine(SAVE_BX - 9, y - 9, SAVE_BX + 9, y + 9, 3, TFT_WHITE);
    tft.drawWideLine(SAVE_BX - 9, y + 9, SAVE_BX + 9, y - 9, 3, TFT_WHITE);
  }
  tft.endWrite();
  delay(700);
  tft.startWrite();
  drawSaveIcon(SAVE_BX, y);
  tft.endWrite();
}

static bool barButtonPressed(bool pressed, int32_t tx, int32_t ty, int bx) {
  int dx = tx - bx, dy = ty - BAR / 2;
  return pressed && ty < BAR && dx * dx + dy * dy < 30 * 30;
}

static void drawTick(bool touching, bool pressed, int32_t tx, int32_t ty) {
  if (homePressed(pressed, tx, ty)) { playSound(SND_TAP); enterApp(APP_HOME); return; }
  if (newPressed(pressed, tx, ty)) {
    playSound(SND_CLEAR);
    tft.fillRect(0, BAR, SCREEN_W, SCREEN_H - BAR, TFT_WHITE);
    return;
  }
  if (barButtonPressed(pressed, tx, ty, SAVE_BX)) {
    stroking = false;
    if (!canvasIdx) { playSound(SND_NOPE); return; }
    captureCanvas(canvasIdx);
    if (canvasBlank(canvasIdx)) { playSound(SND_NOPE); return; }  // nothing drawn yet
    bool ok = saveDrawing(canvasIdx);
    playSound(ok ? SND_MATCH : SND_NOPE);
    showSaved(ok);
    ignoreUntilRelease = true;
    return;
  }
  if (barButtonPressed(pressed, tx, ty, GALLERY_BX)) {
    stroking = false;
    if (!canvasIdx) { playSound(SND_NOPE); return; }
    playSound(SND_TAP);
    backupCanvas();  // keep the current drawing for when we come back
    startGallery();
    return;
  }
  if (pressed && ty < BAR) {
    int best = -1, bestD = 30 * 30;
    for (int i = 0; i < NSW; i++) {
      int dx = tx - swatchX(i), dy = ty - BAR / 2;
      if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = i; }
    }
    if (best >= 0) { drawCol = best; playSound(SND_COLOR); drawDrawBar(); }
    stroking = false;
    return;
  }
  if (!touching || ty < BAR) { stroking = false; return; }
  int brush = drawCol == NSW - 1 ? 24 : 12;
  uint16_t col = swatchColor(drawCol);
  // keep the whole brush below the top bar (the line's soft edge spills 1-2 px more),
  // so paint never lands on the buttons, near the top of the page included
  int minY = BAR + brush + 2;
  if (ty < minY) ty = minY;
  tft.startWrite();
  tft.setClipRect(0, BAR, SCREEN_W, SCREEN_H - BAR);
  if (!stroking) tft.fillCircle(tx, ty, brush, col);
  else tft.drawWideLine(lastX, lastY, tx, ty, brush, col);
  tft.clearClipRect();
  tft.endWrite();
  stroking = true;
  lastX = tx;
  lastY = ty;
}

// ---------------------------------------------------------------- gallery
// The saved drawings, newest first, 6 to a page. Tap one to keep drawing on it;
// the arrow at the top left goes back to the drawing you had. With more than one
// page, the arrows at the top right flip pages and the dots show which page.
static const int TH_W = 240, TH_H = CANVAS_H * TH_W / SCREEN_W;  // 0.3 scale
static const int PER_PAGE = 6;
static const int PREV_BX = 680, NEXT_BX = 760;
static int galleryPage = 0;
static int thumbX(int i) { return 20 + (i % 3) * (TH_W + 20); }
static int thumbY(int i) { return BAR + (CANVAS_H - 2 * TH_H - 44) / 2 + (i / 3) * (TH_H + 44); }
static int galleryPages() { return max(1, ((int)saveIds.size() + PER_PAGE - 1) / PER_PAGE); }
static int pageCount(int page) { return min(PER_PAGE, (int)saveIds.size() - page * PER_PAGE); }

static void drawBackButton() {
  int bx = 40, by = BAR / 2;
  gfx->fillCircle(bx, by, 24, TFT_WHITE);
  gfx->fillTriangle(bx - 16, by, bx - 2, by - 14, bx - 2, by + 14, C_WALL);
  gfx->fillRoundRect(bx - 4, by - 5, 20, 10, 3, C_WALL);
}

// page arrow: dir -1 = previous, +1 = next; faded when there's no page that way
static void drawPageArrow(int bx, int dir, bool active) {
  int by = BAR / 2;
  gfx->fillCircle(bx, by, 24, active ? TFT_WHITE : C_BG);
  uint16_t col = active ? C_WALL : C_BAR;
  gfx->fillTriangle(bx + dir * 12, by, bx - dir * 8, by - 14, bx - dir * 8, by + 14, col);
}

// Delete button in each picture's top right corner. It takes two taps so little
// fingers don't delete by accident: the first turns it big and red ("armed"),
// a second tap within 3 seconds deletes; otherwise it goes back to normal.
static int armedSlot = -1;  // slot on this page whose X is armed, or -1
static uint32_t armedAt = 0;
static const uint32_t ARM_MS = 3000;
static void deleteXCenter(int slot, int &cx, int &cy) {
  cx = thumbX(slot) + TH_W - 10;
  cy = thumbY(slot) + 10;
}

static void drawDeleteX(int slot, bool armed) {
  int cx, cy;
  deleteXCenter(slot, cx, cy);
  int r = armed ? 21 : 16, a = armed ? 8 : 6;
  gfx->fillCircle(cx, cy, r, armed ? RAINBOW[0] : TFT_WHITE);
  if (!armed) gfx->drawCircle(cx, cy, r, C_DARK);
  uint16_t col = armed ? TFT_WHITE : C_DARK;
  gfx->drawWideLine(cx - a, cy - a, cx + a, cy + a, armed ? 3 : 2, col);
  gfx->drawWideLine(cx - a, cy + a, cx + a, cy - a, armed ? 3 : 2, col);
}

static void drawThumb(int slot, int index) {
  int x = thumbX(slot), y = thumbY(slot);
  gfx->fillRoundRect(x - 8, y - 8, TH_W + 16, TH_H + 16, 12, RAINBOW[index % 7]);
  if (!store || !readDrawing(*store, savePath(saveIds[index]), loadIdx)) {
    gfx->fillRect(x, y, TH_W, TH_H, TFT_WHITE);
  } else {
    static uint16_t row[TH_W];
    for (int ty = 0; ty < TH_H; ty++) {
      const uint8_t *src = loadIdx + (ty * CANVAS_H / TH_H) * SCREEN_W;
      for (int tx = 0; tx < TH_W; tx++) row[tx] = pal565[src[tx * SCREEN_W / TH_W]];
      gfx->pushImage(x, y + ty, TH_W, 1, (lgfx::rgb565_t *)row);
    }
  }
  drawDeleteX(slot, false);
}

// put an armed X back to normal (redraws just that picture, off-screen first)
static void disarmDelete() {
  if (armedSlot < 0) return;
  int slot = armedSlot;
  armedSlot = -1;
  bool off = offscreenBegin();
  drawThumb(slot, galleryPage * PER_PAGE + slot);
  if (off) {
    int y = thumbY(slot) - 12;  // the armed X pokes a few pixels past the frame
    offscreenShow(y, TH_H + 24);
  }
}

static void renderGalleryPage() {
  int pages = galleryPages();
  gfx->startWrite();
  gfx->fillScreen(C_BG);
  gfx->fillRect(0, 0, SCREEN_W, BAR, C_BAR);
  drawBackButton();
  if (pages > 1) {
    // the arrows wrap around (next on the last page goes to the first), so both always work
    drawPageArrow(PREV_BX, -1, true);
    drawPageArrow(NEXT_BX, +1, true);
    if (pages <= 12) {  // a dot per page
      int spacing = 28;
      int x0 = 370 - (pages - 1) * spacing / 2;
      for (int p = 0; p < pages; p++)
        gfx->fillCircle(x0 + p * spacing, BAR / 2, p == galleryPage ? 9 : 5,
                       p == galleryPage ? TFT_WHITE : C_BG);
    } else {            // too many for dots: "3 / 40"
      char buf[16];
      snprintf(buf, sizeof(buf), "%d / %d", galleryPage + 1, pages);
      gfx->setFont(&fonts::FreeSansBold12pt7b);
      gfx->setTextDatum(textdatum_t::middle_center);
      gfx->setTextColor(TFT_WHITE);
      gfx->drawString(buf, 370, BAR / 2 + 1);
    }
  }
  gfx->endWrite();
  if (saveIds.empty()) {  // nothing saved yet: a big empty picture frame
    int cx = SCREEN_W / 2, cy = BAR + CANVAS_H / 2;
    gfx->startWrite();
    gfx->fillRoundRect(cx - 130, cy - 100, 260, 200, 20, C_WALL);
    gfx->fillRoundRect(cx - 110, cy - 80, 220, 160, 10, C_FLOOR);
    drawSaveIcon(cx, cy);
    gfx->endWrite();
    return;
  }
  int n = pageCount(galleryPage);
  for (int i = 0; i < n; i++) drawThumb(i, galleryPage * PER_PAGE + i);
}

// the whole page is built off-screen and shown in one go
static void drawGalleryPage() {
  armedSlot = -1;
  bool off = offscreenBegin();
  renderGalleryPage();
  if (off) offscreenShow(0, SCREEN_H);
}

static void startGallery() {
  app = APP_GALLERY;
  ignoreUntilRelease = true;
  chooseStore();
  galleryPage = 0;
  drawGalleryPage();
}

static void deleteDrawing(int slot) {
  int index = galleryPage * PER_PAGE + slot;
  if (store) store->remove(savePath(saveIds[index]));
  saveIds.erase(saveIds.begin() + index);
  if (galleryPage >= galleryPages()) galleryPage = galleryPages() - 1;
  playSound(SND_CLEAR);
  drawGalleryPage();
}

static void galleryTick(bool pressed, int32_t tx, int32_t ty) {
  if (armedSlot >= 0 && millis() - armedAt > ARM_MS) disarmDelete();
  if (!pressed) return;
  int n = pageCount(galleryPage);
  for (int i = 0; i < n; i++) {  // delete X (checked before the picture under it)
    int cx, cy;
    deleteXCenter(i, cx, cy);
    int dx = tx - cx, dy = ty - cy;
    if (dx * dx + dy * dy < 28 * 28) {
      ignoreUntilRelease = true;
      if (armedSlot == i) { deleteDrawing(i); return; }
      disarmDelete();
      armedSlot = i;
      armedAt = millis();
      tft.startWrite();
      drawDeleteX(i, true);
      tft.endWrite();
      playSound(SND_NOPE);
      return;
    }
  }
  disarmDelete();  // tapping anywhere else cancels
  if (ty < BAR && tx < 90) { playSound(SND_TAP); resumeDraw(true); return; }
  int pages = galleryPages();
  if (pages > 1 && barButtonPressed(pressed, tx, ty, PREV_BX)) {
    galleryPage = (galleryPage + pages - 1) % pages;  // wraps to the last page
    playSound(SND_FLIP);
    drawGalleryPage();
    ignoreUntilRelease = true;
    return;
  }
  if (pages > 1 && barButtonPressed(pressed, tx, ty, NEXT_BX)) {
    galleryPage = (galleryPage + 1) % pages;  // wraps to the first page
    playSound(SND_FLIP);
    drawGalleryPage();
    ignoreUntilRelease = true;
    return;
  }
  for (int i = 0; i < n; i++) {
    int x = thumbX(i), y = thumbY(i);
    if (tx >= x - 8 && tx < x + TH_W + 8 && ty >= y - 8 && ty < y + TH_H + 8) {
      if (store && readDrawing(*store, savePath(saveIds[galleryPage * PER_PAGE + i]), canvasIdx)) {
        playSound(SND_FLIP);
        resumeDraw(false);
      } else {
        playSound(SND_NOPE);
      }
      return;
    }
  }
}

// ---------------------------------------------------------------- bubbles app
struct Bubble {
  float x, y, vy, phase, baseX;
  int r, col, popT;
  int drawnX, drawnY, eraseR;
};
#define NB 8
static Bubble bubbles[NB];
static uint32_t lastFrame = 0;
static uint16_t C_BUB_BG;

static void spawnBubble(Bubble &b, bool atBottom) {
  b.r = 40 + esp_random() % 35;
  b.baseX = b.r + esp_random() % (SCREEN_W - 2 * b.r);
  b.x = b.baseX;
  b.y = atBottom ? SCREEN_H + b.r + esp_random() % 150 : BAR + b.r + esp_random() % (SCREEN_H - BAR - b.r);
  b.vy = 1.0f + (esp_random() % 100) / 70.0f;
  b.phase = (esp_random() % 628) / 100.0f;
  b.col = esp_random() % 7;
  b.popT = 0;
}

static LGFX_Sprite patch(&tft);  // small off-screen buffer in internal RAM

static int burstRadius(const Bubble &b) { return b.r * 0.5f + b.popT * 7 + 16; }

// draw one bubble (or its pop burst) onto a target, shifted by (ox, oy)
static void renderBubble(lgfx::LovyanGFX &g, const Bubble &b, int ox, int oy) {
  int x = (int)b.x + ox, y = (int)b.y + oy;
  if (b.popT > 0) {
    float r0 = b.r * 0.5f + b.popT * 7, r1 = r0 + 16;
    for (int k = 0; k < 8; k++) {
      float a = k * PI / 4;
      g.drawWideLine(x + cosf(a) * r0, y + sinf(a) * r0, x + cosf(a) * r1, y + sinf(a) * r1, 4, RAINBOW[b.col]);
    }
  } else {
    drawBubble(g, x, y, b.r, RAINBOW[b.col]);
  }
}

static void startBubbles() {
  patch.setPsram(false);
  patch.setColorDepth(16);
  C_BUB_BG = tft.color565(230, 245, 255);
  tft.startWrite();
  tft.fillScreen(C_BUB_BG);
  drawPlainBar();
  tft.endWrite();
  for (int i = 0; i < NB; i++) {
    spawnBubble(bubbles[i], false);
    bubbles[i].eraseR = 0;
  }
}

static void bubblesTick(bool touching, bool pressed, int32_t tx, int32_t ty) {
  if (homePressed(pressed, tx, ty)) { playSound(SND_TAP); enterApp(APP_HOME); return; }
  if (touching && ty > BAR) {
    for (int i = 0; i < NB; i++) {
      Bubble &b = bubbles[i];
      float dx = tx - b.x, dy = ty - b.y;
      if (b.popT == 0 && dx * dx + dy * dy < (b.r + 12) * (b.r + 12)) {
        b.popT = 1;
        playSound(SND_POP, 190 - b.r * 1.5f);  // big bubbles pop lower
      }
    }
  }
  if (millis() - lastFrame < 33) return;
  lastFrame = millis();

  // 1) move everything and work out how big each one is now (0 = nothing visible)
  int oldX[NB], oldY[NB], oldR[NB], newR[NB];
  for (int i = 0; i < NB; i++) {
    Bubble &b = bubbles[i];
    oldX[i] = b.drawnX; oldY[i] = b.drawnY; oldR[i] = b.eraseR;
    newR[i] = 0;
    if (b.popT > 0) {
      b.popT++;
      if (b.popT > 7) spawnBubble(b, true);
      else newR[i] = burstRadius(b) + 4;
    } else {
      b.y -= b.vy;
      b.phase += 0.05f;
      b.x = b.baseX + sinf(b.phase) * 14;
      if (b.y + b.r < BAR) spawnBubble(b, true);
      else if (b.y - b.r <= SCREEN_H) newR[i] = b.r + 2;
    }
  }

  // 2) for each bubble, redraw the patch covering where it was and where it is now.
  //    The patch is drawn off-screen first and copied in one go, so nothing blinks.
  tft.startWrite();
  tft.setClipRect(0, BAR, SCREEN_W, SCREEN_H - BAR);
  for (int i = 0; i < NB; i++) {
    Bubble &b = bubbles[i];
    int x0 = SCREEN_W, y0 = SCREEN_H, x1 = 0, y1 = 0;
    if (oldR[i]) { x0 = oldX[i] - oldR[i]; y0 = oldY[i] - oldR[i]; x1 = oldX[i] + oldR[i]; y1 = oldY[i] + oldR[i]; }
    if (newR[i]) {
      x0 = min(x0, (int)b.x - newR[i]); y0 = min(y0, (int)b.y - newR[i]);
      x1 = max(x1, (int)b.x + newR[i]); y1 = max(y1, (int)b.y + newR[i]);
    }
    x0 = max(x0, 0); y0 = max(y0, BAR); x1 = min(x1, SCREEN_W); y1 = min(y1, SCREEN_H);
    if (x1 <= x0 || y1 <= y0) continue;
    if (!patch.createSprite(x1 - x0, y1 - y0)) continue;
    patch.fillScreen(C_BUB_BG);
    for (int k = 0; k < NB; k++)
      if (newR[k]) renderBubble(patch, bubbles[k], -x0, -y0);
    patch.pushSprite(x0, y0);
    patch.deleteSprite();
  }
  tft.clearClipRect();
  tft.endWrite();

  for (int i = 0; i < NB; i++) {
    bubbles[i].drawnX = bubbles[i].x;
    bubbles[i].drawnY = bubbles[i].y;
    bubbles[i].eraseR = newR[i];
  }
}

// ---------------------------------------------------------------- memory app
// Difficulty L / M / H: L switches between 2 and 3 pairs, M between 3 and 4,
// H between 5 and 6. Easier levels also leave a wrong pair face up longer.
static const uint8_t MEM_PAIRS[3][2] = {{2, 3}, {3, 4}, {5, 6}};
static const uint16_t MEM_SHOW_MS[3] = {1500, 1100, 1000};
static int memDiff = 1;  // 0 L, 1 M, 2 H (remembered)
static int memSym[12];
static uint8_t memState[12];  // 0 face down, 1 face up, 2 matched
static int memN, memCols, memFirst, memSecond, memRound = 0;
static uint32_t memFlipBack = 0;
static int cardW, cardH;

static int cardX(int i) { return 20 + (i % memCols) * (cardW + 20); }
static int cardY(int i) { return BAR + 20 + (i / memCols) * (cardH + 20); }

static void drawCard(int i) {
  int x = cardX(i), y = cardY(i);
  tft.startWrite();
  if (memState[i] == 0) {
    tft.fillRoundRect(x, y, cardW, cardH, 20, C_WALL);
    tft.fillRoundRect(x + 10, y + 10, cardW - 20, cardH - 20, 14, tft.color565(150, 100, 220));
    tft.setFont(&fonts::FreeSansBold24pt7b);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawString("?", x + cardW / 2, y + cardH / 2);
    tft.setTextSize(1);
  } else {
    tft.fillRoundRect(x, y, cardW, cardH, 20, memState[i] == 2 ? RAINBOW[3] : C_WALL);
    tft.fillRoundRect(x + 8, y + 8, cardW - 16, cardH - 16, 14, TFT_WHITE);
    drawSymbol(memSym[i], x + cardW / 2, y + cardH / 2, min(cardW, cardH) * 0.36f);
  }
  tft.endWrite();
}

static void startMemory() {
  int pairs = MEM_PAIRS[memDiff][memRound % 2];
  memN = pairs * 2;
  memCols = pairs;
  cardW = (SCREEN_W - 40 - (memCols - 1) * 20) / memCols;
  cardH = (SCREEN_H - BAR - 40 - 20) / 2;
  int syms[6] = {0, 1, 2, 3, 4, 5};
  for (int i = 5; i > 0; i--) { int j = esp_random() % (i + 1); int t = syms[i]; syms[i] = syms[j]; syms[j] = t; }
  for (int i = 0; i < memN; i++) memSym[i] = syms[i / 2];
  for (int i = memN - 1; i > 0; i--) { int j = esp_random() % (i + 1); int t = memSym[i]; memSym[i] = memSym[j]; memSym[j] = t; }
  for (int i = 0; i < memN; i++) memState[i] = 0;
  memFirst = memSecond = -1;
  memFlipBack = 0;
  tft.startWrite();
  tft.fillScreen(C_BG);
  drawPlainBar();
  drawDiffButtons(memDiff);
  tft.endWrite();
  for (int i = 0; i < memN; i++) drawCard(i);
}

static void memoryTick(bool touching, bool pressed, int32_t tx, int32_t ty) {
  if (homePressed(pressed, tx, ty)) { playSound(SND_TAP); enterApp(APP_HOME); return; }
  int dl = diffPressed(pressed, tx, ty);
  if (dl >= 0) {
    playSound(SND_TAP);
    if (dl != memDiff) {  // new difficulty: deal a new game
      memDiff = dl;
      prefs.putUChar("memDiff", dl);
      memRound = 0;
      startMemory();
    }
    return;
  }
  if (memFlipBack && millis() >= memFlipBack) {  // not a match: turn both back over
    memState[memFirst] = memState[memSecond] = 0;
    playSound(SND_NOPE);
    drawCard(memFirst);
    drawCard(memSecond);
    memFirst = memSecond = -1;
    memFlipBack = 0;
  }
  if (!pressed || memFlipBack || ty < BAR) return;
  for (int i = 0; i < memN; i++) {
    int x = cardX(i), y = cardY(i);
    if (tx < x || tx >= x + cardW || ty < y || ty >= y + cardH || memState[i] != 0) continue;
    memState[i] = 1;
    playSound(SND_FLIP);
    drawCard(i);
    if (memFirst < 0) { memFirst = i; return; }
    memSecond = i;
    if (memSym[memFirst] == memSym[memSecond]) {
      memState[memFirst] = memState[memSecond] = 2;
      playSound(SND_MATCH);
      drawCard(memFirst);
      drawCard(memSecond);
      memFirst = memSecond = -1;
      bool done = true;
      for (int k = 0; k < memN; k++) if (memState[k] != 2) done = false;
      if (done) {
        delay(500);
        playSound(SND_WIN);
        confetti(120);
        tft.startWrite();
        tft.fillCircle(SCREEN_W / 2, (SCREEN_H + BAR) / 2, 140, TFT_WHITE);
        drawStar(SCREEN_W / 2, (SCREEN_H + BAR) / 2, 125, C_FACE, C_FACE_EDGE);
        tft.endWrite();
        delay(1800);
        memRound++;
        startMemory();
        waitForRelease();
      }
    } else {
      memFlipBack = millis() + MEM_SHOW_MS[memDiff];
    }
    return;
  }
}

// ---------------------------------------------------------------- main
static bool wasTouching = false;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nKids Games starting");
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, LOW);

  uint8_t touchAddr = findTouch();
  tft.configure(touchAddr);
  if (!tft.init()) Serial.println("Display init FAILED");
  initColors();
  initSound();      // also opens the saved settings
  initBacklight();  // turns the screen on at the saved brightness
  initDrawStorage();
  mazeDiff = constrain(prefs.getUChar("mazeDiff", 0), 0, 2);  // saved difficulty levels
  level = BAND_FIRST[mazeDiff];
  memDiff = constrain(prefs.getUChar("memDiff", 1), 0, 2);
  Serial.printf("Difficulty: maze %c, memory %c\n", "LMH"[mazeDiff], "LMH"[memDiff]);
  Serial.printf("Display up (brightness %c)\n", "LMH"[brightLevel]);

  drawHome();
  playSound(SND_START);
}

void loop() {
  int32_t tx = 0, ty = 0;
  bool touching = tft.getTouch(&tx, &ty);
  bool pressed = touching && !wasTouching;
  wasTouching = touching;

  // screen sleep: dim, then off, after a while without touches; a touch wakes it
  if (touching) {
    lastActivity = millis();
    if (screenState != SCREEN_AWAKE) {
      // a tap on a dark screen only wakes it; on a dimmed screen it also works normally
      if (screenState == SCREEN_OFF) ignoreUntilRelease = true;
      screenState = SCREEN_AWAKE;
      setBacklight(BRIGHT_LEVELS[brightLevel]);
    }
  } else {
    uint32_t idle = millis() - lastActivity;
    if (screenState == SCREEN_AWAKE && idle > DIM_AFTER_MS) {
      screenState = SCREEN_DIM;
      setBacklight(max(10, BRIGHT_LEVELS[brightLevel] / 3));
    } else if (screenState == SCREEN_DIM && idle > OFF_AFTER_MS) {
      screenState = SCREEN_OFF;
      setBacklight(0);
    }
  }
  if (screenState == SCREEN_OFF) { delay(20); return; }  // pause the games while dark

  // after switching screens, ignore the finger until it lifts
  if (ignoreUntilRelease) {
    if (touching) touching = pressed = false;
    else ignoreUntilRelease = false;
  }

  switch (app) {
    case APP_HOME: homeTick(pressed, tx, ty); break;
    case APP_MAZE: mazeTick(touching, pressed, tx, ty); break;
    case APP_DRAW: drawTick(touching, pressed, tx, ty); break;
    case APP_BUBBLES: bubblesTick(touching, pressed, tx, ty); break;
    case APP_MEMORY: memoryTick(touching, pressed, tx, ty); break;
    case APP_GALLERY: galleryTick(pressed, tx, ty); break;
  }
  delay(app == APP_BUBBLES || app == APP_DRAW ? 2 : 10);
}
