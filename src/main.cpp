// Kids Games - simple touch games for little ones (age ~3):
// Maze, Draw, Bubbles and Memory, picked from a home screen.
// Board: Elecrow CrowPanel 7.0 (basic, ESP32-S3 N4R8, 4 MB flash). Not for the ADVANCE 7.0.
// Display pin and timing settings follow Elecrow's published LovyanGFX setup for this board.

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
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

// ---------------------------------------------------------------- maze
struct Level { uint8_t cols, rows; };
static const Level LEVELS[] = {{3, 2}, {4, 2}, {4, 3}, {5, 3}, {5, 4}, {6, 4}};
static const int NUM_LEVELS = sizeof(LEVELS) / sizeof(LEVELS[0]);
#define MAXC 6
#define MAXR 4

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
  tft.fillCircle(bx, by, 24, TFT_WHITE);
  tft.fillTriangle(bx - 17, by - 1, bx + 17, by - 1, bx, by - 17, C_WALL);
  tft.fillRect(bx - 11, by - 2, 22, 16, C_WALL);
  tft.fillRect(bx - 4, by + 5, 8, 9, TFT_WHITE);
}

// round-arrow icon, top right: start over
static void drawNewButton() {
  int bx = SCREEN_W - 40, by = (TOP - 8) / 2;
  tft.fillCircle(bx, by, 24, TFT_WHITE);
  // ring going clockwise from 3 o'clock round to the upper right, gap at the top right
  const float endDeg = 300;
  tft.fillArc(bx, by, 16, 10, 0, endDeg, C_WALL);
  // arrowhead sits on the end of the ring and points along it (clockwise)
  float a = endDeg * DEG_TO_RAD;
  float rm = 13;                         // middle of the ring
  float ex = bx + cosf(a) * rm, ey = by + sinf(a) * rm;
  float tx = -sinf(a), ty = cosf(a);     // direction of travel
  float nx = cosf(a), ny = sinf(a);      // outward from the center
  tft.fillTriangle(ex + tx * 10, ey + ty * 10,
                   ex + nx * 9 - tx * 2, ey + ny * 9 - ty * 2,
                   ex - nx * 9 - tx * 2, ey - ny * 9 - ty * 2, C_WALL);
}

static void drawTopBar() {
  tft.fillRect(0, 0, SCREEN_W, TOP - 8, C_BAR);
  drawHomeButton();
  // earned stars (up to 10 shown)
  int shown = starsEarned % 11;
  for (int i = 0; i < 10; i++) {
    int cx = 120 + i * 52, cy = (TOP - 8) / 2;
    if (i < shown) drawStar(cx, cy, 22, C_FACE, C_FACE_EDGE);
    else tft.fillCircle(cx, cy, 6, tft.color565(200, 230, 255));
  }
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
enum App { APP_HOME, APP_MAZE, APP_DRAW, APP_BUBBLES, APP_MEMORY };
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
  tft.fillRect(0, 0, SCREEN_W, BAR, C_BAR);
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
    if (level < NUM_LEVELS - 1) level++;
    startMaze();
    waitForRelease();
  }
}

// ---------------------------------------------------------------- draw app
static const int NSW = 9;  // 7 rainbow, dark, eraser
static int drawCol = 0;
static bool stroking = false;
static int lastX, lastY;

static uint16_t swatchColor(int i) {
  if (i < 7) return RAINBOW[i];
  if (i == 7) return C_DARK;
  return TFT_WHITE;
}
static int swatchX(int i) { return 130 + i * 62; }

static void drawDrawBar() {
  tft.startWrite();
  drawPlainBar();
  int y = BAR / 2;
  for (int i = 0; i < NSW; i++) {
    int x = swatchX(i);
    tft.fillCircle(x, y, 27, i == drawCol ? C_DARK : C_BAR);
    tft.fillCircle(x, y, 22, swatchColor(i));
    if (i == NSW - 1) {  // eraser: white with a little pink band
      tft.fillRoundRect(x - 14, y - 6, 28, 12, 4, C_STAR);
    }
  }
  drawNewButton();
  tft.endWrite();
}

static void startDraw() {
  tft.fillScreen(TFT_WHITE);
  drawDrawBar();
  stroking = false;
}

static void drawTick(bool touching, bool pressed, int32_t tx, int32_t ty) {
  if (homePressed(pressed, tx, ty)) { playSound(SND_TAP); enterApp(APP_HOME); return; }
  if (newPressed(pressed, tx, ty)) {
    playSound(SND_CLEAR);
    tft.fillRect(0, BAR, SCREEN_W, SCREEN_H - BAR, TFT_WHITE);
    return;
  }
  if (pressed && ty < BAR) {
    for (int i = 0; i < NSW; i++) {
      int dx = tx - swatchX(i), dy = ty - BAR / 2;
      if (dx * dx + dy * dy < 31 * 31) { drawCol = i; playSound(SND_COLOR); drawDrawBar(); break; }
    }
    stroking = false;
    return;
  }
  if (!touching || ty < BAR) { stroking = false; return; }
  int brush = drawCol == NSW - 1 ? 24 : 12;
  uint16_t col = swatchColor(drawCol);
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
static int memSym[8];
static uint8_t memState[8];  // 0 face down, 1 face up, 2 matched
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
  int pairs = (memRound % 2 == 0) ? 3 : 4;
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
  tft.endWrite();
  for (int i = 0; i < memN; i++) drawCard(i);
}

static void memoryTick(bool touching, bool pressed, int32_t tx, int32_t ty) {
  if (homePressed(pressed, tx, ty)) { playSound(SND_TAP); enterApp(APP_HOME); return; }
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
      memFlipBack = millis() + 1100;
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
  digitalWrite(PIN_BL, HIGH);
  Serial.println("Display up");
  initSound();

  drawHome();
  playSound(SND_START);
}

void loop() {
  int32_t tx = 0, ty = 0;
  bool touching = tft.getTouch(&tx, &ty);
  bool pressed = touching && !wasTouching;
  wasTouching = touching;

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
  }
  delay(app == APP_BUBBLES || app == APP_DRAW ? 2 : 10);
}
