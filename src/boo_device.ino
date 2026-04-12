/**
 * boo_device.ino  v2.0
 * Claude Code 許可デバイス — マスコット「ブー (Boo)」
 * M5StickC PLUS2 用
 *
 * ■ たまごっち思想
 *   fed    : 承認するとご飯をあげられる。時間経過で減る。
 *            否認・タイムアウトでも減る（ブーが悲しむ）。
 *   energy : Claudeが作業するほど消費。スリープ中に回復。
 *   mood   : fed × energy から算出。表情・アニメに反映。
 *
 * ■ 通信プロトコル: JSON over USB Serial (115200bps)
 *   受信: {"type":"approve","tool":"Bash","details":"...","danger":false,"timeout":30}
 *   受信: {"type":"working","tool":"Bash"}
 *   受信: {"type":"idle"}
 *   受信: {"type":"tokens","total":328500,"today":267}
 *   送信: {"approved":true}
 */

#include <M5StickCPlus2.h>
#include <ArduinoJson.h>

// ============================================================
// 定数
// ============================================================
#define SCREEN_W        135
#define SCREEN_H        240
#define CHAR_W          6
#define CHAR_H          8
#define MASCOT_Y        8
#define SERIAL_BAUD     115200
#define ANIM_INTERVAL   600     // アニメ更新間隔(ms)
#define RESULT_SHOW_MS  1400    // 承認/否認結果表示時間(ms)
#define IDLE_SLEEP_SEC  120     // アイドル→スリープ(秒)

// ---- ゲージ上限 ----
#define FED_MAX         8.0f    // ハート8個
#define ENERGY_MAX      6.0f    // ブロック6個

// ---- fed 変化量 ----
#define FED_APPROVE      2.0f   // 承認でご飯 +2
#define FED_DENY        -1.0f   // 否認でちょっとさびしい
#define FED_TIMEOUT     -2.0f   // タイムアウト放置で空腹
#define FED_DECAY_PER_MIN -0.15f // 自然減衰(毎分)
#define FED_PET          0.5f   // なでる +0.5

// ---- energy 変化量 ----
#define ENERGY_WORK_PER_MIN  -0.4f  // 作業中の消費(毎分)
#define ENERGY_IDLE_PER_MIN  -0.05f // アイドル中の微減
#define ENERGY_SLEEP_PER_MIN  0.8f  // スリープ中の回復(毎分)
#define ENERGY_TOKEN_PER_1K  -0.02f // トークン1K ごとの消費

// TFT カラー (RGB565)
#define COL_BG          0x0000
#define COL_BOO         0x07FF
#define COL_HEART       0xF81F
#define COL_WARN        0xFFE0
#define COL_OK          0x07E0
#define COL_NG          0xF800
#define COL_INFO        0xFFFF
#define COL_DIM         0x4208
#define COL_ORANGE      0xFD20

// ============================================================
// アスキーアート定義
// ============================================================
struct AsciiArt { const char* lines[7]; uint8_t count; uint16_t color; };

const AsciiArt ART_IDLE = {{
  "   .-\"\"-.   ", "  / o   o \\  ",
  " |   ~~~   | ", "  \\ _____ /  ", "   '-----'   ",
}, 5, COL_BOO};

const AsciiArt ART_SLEEP = {{
  "   .-\"\"-.   ", "  / -   - \\  ",
  " |   ___   | ", "  \\ _____ /  ", "  zzZZ...    ",
}, 5, COL_DIM};

const AsciiArt ART_ALERT = {{
  "      !      ", "   .-\"\"-.   ",
  "  / o ! o \\  ", " |   ~~~   | ", "  \\ _____ /  ",
}, 5, COL_WARN};

const AsciiArt ART_DANGER = {{
  " !! .-\"\"-. !!", "!(o  o  o)!  ",
  " |  !!!  |   ", " \\-------/   ", " !DANGER!    ",
}, 5, COL_NG};

const AsciiArt ART_WORK = {{
  "   .-\"\"-.   ", "  /->   - \\  ",
  " |  . . .  | ", "  \\ _____ /  ",
}, 4, COL_BOO};

const AsciiArt ART_BUSY = {{
  "* .-\"\"-. *  ", " /o~v~o\\    ",
  "v|  ~  |v   ", " \\-----/    ",
}, 4, COL_ORANGE};

const AsciiArt ART_APPROVED = {{
  "   .-\"\"-.   ", "  / ^ ^ \\   ",
  " |  \\~/  |  ", "  \\ _____ /  ", "    yay!!    ",
}, 5, COL_OK};

const AsciiArt ART_DENIED = {{
  "   .-\"\"-.   ", "  / x   x \\  ",
  " |   nnn   | ", "  \\ _____ /  ", "   no way    ",
}, 5, COL_NG};

const AsciiArt ART_CRUSH = {{
  " v       v   ", "   .-\"\"-.   ",
  "  / v   v \\  ", " |   ~~~   | ", "  \\ _____ /  ",
}, 5, COL_HEART};

const AsciiArt ART_LOVE = {{
  "   .-\"\"-.   ", "  / v   v \\  ",
  " |  v v v  | ", "  \\ _____ /  ", "  v  v  v    ",
}, 5, COL_HEART};

const AsciiArt ART_EXCITED = {{
  " v  v  v     ", "   .-\"\"-.   ",
  "  / v w v \\  ", " | v~~~~~v | ", "  \\ _____ /  ",
}, 5, COL_HEART};

const AsciiArt ART_PLEAD = {{
  "     v       ", "   .-\"\"-.   ",
  "  />v   v\\  ", " |  pleaz  | ", "  \\ _____ /  ",
}, 5, COL_HEART};

const AsciiArt ART_BREAK = {{
  "   .-\"\"-.   ", "  / ; ~ ; \\  ",
  " |  v/v    | ", "  \\ _____ /  ", "    ...      ",
}, 5, COL_HEART};

// ---- 空腹 (fed 低下時) ----
const AsciiArt ART_HUNGRY = {{
  "   .-\"\"-.   ", "  / o _ o \\  ",
  " |  hungry | ", "  \\ _____ /  ", "   feed me   ",
}, 5, COL_WARN};

// ---- 疲弊 (energy 低下時) ----
const AsciiArt ART_TIRED = {{
  "   .-\"\"-.   ", "  / ~ _ ~ \\  ",
  " |  tired  | ", "  \\ _____ /  ", "   zzzZZZ    ",
}, 5, COL_DIM};

// ============================================================
// ステートマシン
// ============================================================
enum DeviceState {
  ST_BOOT, ST_IDLE, ST_SLEEP,
  ST_APPROVAL, ST_WORKING,
  ST_APPROVED, ST_DENIED, ST_STATS
};

// ============================================================
// ブーのパラメータ & 統計
// ============================================================
struct BooParams {
  float    fed         = 6.0f;   // ご飯ゲージ  0.0〜FED_MAX
  float    energy      = 5.0f;   // エネルギー  0.0〜ENERGY_MAX
  uint32_t approved    = 0;      // 承認回数
  uint32_t denied      = 0;      // 否認回数
  uint32_t nappedSec   = 0;      // 累計スリープ秒
  uint32_t tokenTotal  = 0;      // 累計トークン
  uint32_t today       = 0;      // 今日の承認数
  uint32_t sessionStart= 0;
};

// ============================================================
// 承認リクエスト
// ============================================================
struct ApprovalReq {
  char     toolName[48];
  char     details[96];
  bool     isDanger;
  uint16_t timeoutSec;
  int16_t  remaining;
  uint32_t startMs;
};

// ============================================================
// グローバル変数
// ============================================================
DeviceState  gState       = ST_BOOT;
BooParams    gBoo;
ApprovalReq  gReq;

char         gSerialBuf[384];
uint16_t     gSerialPos   = 0;

uint32_t     gLastFrameMs = 0;
uint32_t     gLastIdleMs  = 0;
uint32_t     gLastDecayMs = 0;   // 毎分の減衰タイマー
uint32_t     gSleepStartMs= 0;   // スリープ開始時刻
uint32_t     gResultMs    = 0;
uint8_t      gAnimIdx     = 0;
bool         gFlash       = false;

// ============================================================
// ゲージ計算
// ============================================================

// mood: fed と energy の加重平均 → 0〜8
uint8_t calcMood() {
  float f = gBoo.fed    / FED_MAX;
  float e = gBoo.energy / ENERGY_MAX;
  float m = (f * 0.6f + e * 0.4f) * 8.0f;
  return (uint8_t)constrain(m, 0.0f, 8.0f);
}

void addFed(float delta) {
  gBoo.fed = constrain(gBoo.fed + delta, 0.0f, FED_MAX);
}

void addEnergy(float delta) {
  gBoo.energy = constrain(gBoo.energy + delta, 0.0f, ENERGY_MAX);
}

// ============================================================
// 毎分の減衰処理
// ============================================================
void tickDecay(uint32_t now) {
  if (now - gLastDecayMs < 60000UL) return;
  float elapsed = (float)(now - gLastDecayMs) / 60000.0f;
  gLastDecayMs = now;

  // fed は常に自然減衰
  addFed(FED_DECAY_PER_MIN * elapsed);

  // energy は状態に応じて変化
  switch (gState) {
    case ST_WORKING:
      addEnergy(ENERGY_WORK_PER_MIN * elapsed);
      break;
    case ST_SLEEP:
      addEnergy(ENERGY_SLEEP_PER_MIN * elapsed);
      gBoo.nappedSec += (uint32_t)(elapsed * 60.0f);
      break;
    default:
      addEnergy(ENERGY_IDLE_PER_MIN * elapsed);
      break;
  }
}

// ============================================================
// 描画ユーティリティ
// ============================================================
void cls() { M5.Lcd.fillScreen(COL_BG); }

void drawArt(const AsciiArt& art, int16_t yTop) {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(art.color, COL_BG);
  for (uint8_t i = 0; i < art.count; i++) {
    uint8_t len = strlen(art.lines[i]);
    int16_t x   = (SCREEN_W - len * CHAR_W) / 2;
    M5.Lcd.setCursor(x, yTop + i * (CHAR_H + 2));
    M5.Lcd.print(art.lines[i]);
  }
}

void drawText(int16_t x, int16_t y, uint16_t col, uint8_t sz,
              const char* fmt, ...) {
  char buf[64]; va_list ap;
  va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  M5.Lcd.setTextSize(sz);
  M5.Lcd.setTextColor(col, COL_BG);
  M5.Lcd.setCursor(x, y);
  M5.Lcd.print(buf);
}

void hline(int16_t y, uint16_t col = COL_DIM) {
  M5.Lcd.drawLine(0, y, SCREEN_W, y, col);
}

// fed ハートゲージ（色は残量に応じて変化）
void drawFedGauge(int16_t x, int16_t y) {
  uint8_t filled = (uint8_t)roundf(gBoo.fed);
  uint16_t col   = gBoo.fed < 2.0f ? COL_NG
                 : gBoo.fed < 4.0f ? COL_WARN
                 : COL_HEART;
  M5.Lcd.setTextSize(1);
  for (uint8_t i = 0; i < (uint8_t)FED_MAX; i++) {
    M5.Lcd.setTextColor(i < filled ? col : COL_DIM, COL_BG);
    M5.Lcd.setCursor(x + i * 8, y);
    M5.Lcd.print(i < filled ? "v" : ".");
  }
}

// energy ブロックゲージ（色は残量に応じて変化）
void drawEnergyGauge(int16_t x, int16_t y) {
  uint8_t filled = (uint8_t)roundf(gBoo.energy);
  uint16_t col   = gBoo.energy < 1.5f ? COL_NG
                 : gBoo.energy < 3.0f ? COL_WARN
                 : COL_OK;
  M5.Lcd.setTextSize(1);
  for (uint8_t i = 0; i < (uint8_t)ENERGY_MAX; i++) {
    M5.Lcd.setTextColor(i < filled ? col : COL_DIM, COL_BG);
    M5.Lcd.setCursor(x + i * 8, y);
    M5.Lcd.print(i < filled ? "#" : ".");
  }
}

// mood ハートゲージ
void drawMoodGauge(int16_t x, int16_t y) {
  uint8_t mood = calcMood();
  M5.Lcd.setTextSize(1);
  for (uint8_t i = 0; i < 8; i++) {
    M5.Lcd.setTextColor(i < mood ? COL_HEART : COL_DIM, COL_BG);
    M5.Lcd.setCursor(x + i * 8, y);
    M5.Lcd.print(i < mood ? "v" : ".");
  }
}

// ============================================================
// アイドルアニメ選択
// mood が高いほどハート表情が増える
// fed=0 → 空腹顔、energy=0 → 疲弊顔 を優先
// ============================================================
const AsciiArt* pickIdleArt(uint8_t frame) {
  if (gBoo.fed    < 1.0f) return &ART_HUNGRY;
  if (gBoo.energy < 0.5f) return &ART_TIRED;

  uint8_t mood = calcMood();

  // mood 0-2: 元気なし
  if (mood <= 2) {
    return (frame % 4 < 3) ? &ART_IDLE : &ART_TIRED;
  }
  // mood 3-5: 通常
  if (mood <= 5) {
    const AsciiArt* seq[] = {
      &ART_IDLE, &ART_IDLE, &ART_CRUSH, &ART_IDLE
    };
    return seq[frame % 4];
  }
  // mood 6-8: ハート全開
  const AsciiArt* seq[] = {
    &ART_CRUSH, &ART_IDLE, &ART_EXCITED,
    &ART_IDLE,  &ART_LOVE, &ART_IDLE,
    &ART_CRUSH, &ART_EXCITED
  };
  return seq[frame % 8];
}

// ============================================================
// 画面描画 — アイドル
// ============================================================
void drawIdleScreen() {
  cls();
  drawArt(*pickIdleArt(gAnimIdx), MASCOT_Y);

  uint32_t lv = 1 + gBoo.approved / 10;
  drawText(4, 78, COL_INFO, 1, "Boo  Lv%lu", lv);
  drawText(100, 78, COL_DIM, 1, "1/2");
  hline(88);

  // mood
  drawText(4,  92, COL_DIM, 1, "mood ");
  drawMoodGauge(40, 92);

  // fed  ← 承認でご飯をあげる
  drawText(4, 104, COL_DIM, 1, "fed  ");
  drawFedGauge(40, 104);

  // energy ← 作業で消費、スリープで回復
  drawText(4, 116, COL_DIM, 1, "enrg ");
  drawEnergyGauge(40, 116);

  hline(128);

  uint32_t uptimeSec = (millis() - gBoo.sessionStart) / 1000;
  uint32_t nH = gBoo.nappedSec / 3600;
  uint32_t nM = (gBoo.nappedSec % 3600) / 60;

  drawText(4, 132, COL_DIM, 1, "approved  %lu", gBoo.approved);
  drawText(4, 142, COL_DIM, 1, "denied    %lu", gBoo.denied);
  drawText(4, 152, COL_DIM, 1, "napped  %luh%02lum", nH, nM);
  drawText(4, 162, COL_DIM, 1, "tokens  %luK", gBoo.tokenTotal / 1000);
  drawText(4, 172, COL_DIM, 1, "today     %lu", gBoo.today);
}

// ============================================================
// 画面描画 — スリープ（energy 回復中）
// ============================================================
void drawSleepScreen() {
  cls();
  drawArt(ART_SLEEP, MASCOT_Y + 10);
  drawText(4, 92, COL_DIM, 1, "recovering energy...");
  drawText(4, 102, COL_DIM, 1, "enrg ");
  drawEnergyGauge(40, 102);
  uint32_t nH = gBoo.nappedSec / 3600;
  uint32_t nM = (gBoo.nappedSec % 3600) / 60;
  drawText(4, 116, COL_DIM, 1, "napped  %luh%02lum", nH, nM);
  hline(128);
  drawText(16, 132, COL_DIM, 1, "press A to wake");
}

// ============================================================
// 画面描画 — 承認リクエスト
// ============================================================
void drawApprovalScreen() {
  cls();
  const AsciiArt* art;
  if      (gReq.isDanger)          art = &ART_DANGER;
  else if (gReq.remaining <= 5)    art = &ART_PLEAD;
  else                             art = (gAnimIdx%2==0) ? &ART_ALERT : &ART_WORK;
  drawArt(*art, MASCOT_Y);

  hline(82);
  uint16_t col = gReq.remaining > 10 ? COL_INFO
               : gReq.remaining >  5 ? COL_WARN : COL_NG;
  drawText(4, 86, col, 1, "approve? %ds", gReq.remaining);
  drawText(4,  98, COL_WARN, 1, "%.21s", gReq.toolName);
  drawText(4, 110, COL_BOO,  1, "%.21s", gReq.details);
  if (strlen(gReq.details) > 21)
    drawText(4, 120, COL_BOO, 1, "%.21s", gReq.details + 21);
  drawText(4, 133, COL_DIM, 1, "(called");
  drawText(4, 143, COL_DIM, 1, " %.20s)", gReq.toolName);
  hline(156);
  drawText(4,  160, COL_OK, 1, "A: approve");
  drawText(76, 160, COL_NG, 1, "B: deny");
}

// ============================================================
// 画面描画 — 処理中（energy 消費中）
// ============================================================
const char* workDots[] = { "   ", ".  ", ".. ", "..." };
void drawWorkScreen() {
  cls();
  drawArt(ART_BUSY, MASCOT_Y + 6);
  drawText(16, 84, COL_BOO, 1, "working%s", workDots[gAnimIdx % 4]);
  drawText(4,  96, COL_DIM, 1, "enrg consuming...");
  drawText(4, 106, COL_DIM, 1, "enrg ");
  drawEnergyGauge(40, 106);
  drawText(4, 120, COL_DIM, 1, "tokens  %luK", gBoo.tokenTotal / 1000);
}

// ============================================================
// 画面描画 — 承認/否認 結果
// ============================================================
void drawResultScreen(bool approved) {
  cls();
  if (approved) {
    drawArt(ART_APPROVED, MASCOT_Y);
    hline(86);
    drawText(22, 92, COL_OK, 2, "APPROVED");
    // fed が増えたことを見せる
    drawText(4, 114, COL_OK, 1, "fed  +2 !");
    drawFedGauge(4, 126);
    drawText(4, 140, COL_OK, 1, "sent...");
  } else {
    // fed < 1 なら空腹顔を使う
    drawArt(gBoo.fed < 1.0f ? ART_HUNGRY : ART_DENIED, MASCOT_Y);
    hline(86);
    drawText(30, 92, COL_NG, 2, "DENIED");
    // fed が減ったことを見せる
    drawText(4, 114, COL_NG, 1, "fed  -1 ...");
    drawFedGauge(4, 126);
    drawText(22, 140, COL_NG, 1, "sent...");
  }
}

// ============================================================
// 画面描画 — スタッツ (ページ2)
// ============================================================
void drawStatsScreen() {
  cls();
  drawArt(ART_IDLE, MASCOT_Y);
  drawText(90, MASCOT_Y + 2, COL_DIM, 1, "2/2");
  hline(82);

  uint32_t uptimeSec = (millis() - gBoo.sessionStart) / 1000;
  uint32_t h = uptimeSec / 3600, m = (uptimeSec % 3600) / 60;
  uint32_t total = gBoo.approved + gBoo.denied;
  uint8_t  pct   = total > 0 ? (uint8_t)(gBoo.approved * 100 / total) : 0;
  uint32_t nH = gBoo.nappedSec / 3600, nM = (gBoo.nappedSec % 3600) / 60;

  drawText(4,  86, COL_INFO, 1, "-- Session Stats --");
  drawText(4,  98, COL_OK,   1, "approved : %lu",    gBoo.approved);
  drawText(4, 108, COL_NG,   1, "denied   : %lu",    gBoo.denied);
  drawText(4, 118, COL_WARN, 1, "approve%% : %u%%",   pct);
  drawText(4, 128, COL_DIM,  1, "napped  : %luh%02lum", nH, nM);
  drawText(4, 138, COL_BOO,  1, "tokens  : %luK",    gBoo.tokenTotal / 1000);
  drawText(4, 148, COL_DIM,  1, "uptime  : %luh%02lum", h, m);
  drawText(4, 158, COL_INFO, 1, "today   : %lu",     gBoo.today);
  hline(170);
  drawText(4, 174, COL_DIM,  1, "A:back  B:reset");
}

// ============================================================
// 起動アニメ
// ============================================================
void bootAnimation() {
  cls();
  M5.Lcd.setTextColor(COL_HEART, COL_BG);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(28, 50);
  M5.Lcd.print("( Boo )");
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_DIM, COL_BG);
  M5.Lcd.setCursor(14, 80); M5.Lcd.print("Claude Code Guard");
  M5.Lcd.setCursor(20, 96); M5.Lcd.print("v2.0  M5StickC+2");
  delay(600);
  for (int16_t y = 220; y > MASCOT_Y; y -= 14) {
    cls(); drawArt(ART_LOVE, y); delay(25);
  }
  drawArt(ART_EXCITED, MASCOT_Y); delay(400);
  drawArt(ART_IDLE,    MASCOT_Y); delay(300);
}

// ============================================================
// シリアル通信
// ============================================================
void sendJson(bool approved) {
  StaticJsonDocument<64> doc;
  doc["approved"] = approved;
  serializeJson(doc, Serial);
  Serial.println();
}

void processMessage(const char* msg) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, msg)) return;
  const char* type = doc["type"] | "";

  if (strcmp(type, "approve") == 0) {
    strlcpy(gReq.toolName, doc["tool"]    | "unknown", sizeof(gReq.toolName));
    strlcpy(gReq.details,  doc["details"] | "",        sizeof(gReq.details));
    gReq.isDanger   = doc["danger"]  | false;
    gReq.timeoutSec = doc["timeout"] | 30;
    gReq.remaining  = gReq.timeoutSec;
    gReq.startMs    = millis();
    gState          = ST_APPROVAL;
    gLastIdleMs     = millis();

  } else if (strcmp(type, "working") == 0) {
    if (gState != ST_APPROVAL) gState = ST_WORKING;

  } else if (strcmp(type, "idle") == 0) {
    if (gState == ST_WORKING) {
      gState = ST_IDLE; gLastIdleMs = millis();
    }

  } else if (strcmp(type, "tokens") == 0) {
    uint32_t newTotal = doc["total"] | gBoo.tokenTotal;
    uint32_t newK     = newTotal / 1000;
    uint32_t prevK    = gBoo.tokenTotal / 1000;
    // トークン増分に応じて energy 消費
    if (newK > prevK)
      addEnergy(ENERGY_TOKEN_PER_1K * (float)(newK - prevK));
    gBoo.tokenTotal = newTotal;
    gBoo.today      = doc["today"] | gBoo.today;
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (gSerialPos > 0) {
        gSerialBuf[gSerialPos] = '\0';
        processMessage(gSerialBuf);
        gSerialPos = 0;
      }
    } else if (gSerialPos < (sizeof(gSerialBuf) - 1)) {
      gSerialBuf[gSerialPos++] = c;
    }
  }
}

// ============================================================
// setup / loop
// ============================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(0);
  M5.Lcd.setBrightness(160);
  M5.Lcd.fillScreen(COL_BG);
  Serial.begin(SERIAL_BAUD);

  uint32_t now      = millis();
  gBoo.sessionStart = now;
  gLastIdleMs       = now;
  gLastFrameMs      = now;
  gLastDecayMs      = now;

  bootAnimation();
  gState = ST_IDLE;
  drawIdleScreen();
}

void loop() {
  M5.update();
  pollSerial();
  uint32_t now = millis();

  // 毎分の減衰処理（fed 自然減 / energy 状態別増減）
  tickDecay(now);

  // ---- アニメーション更新 ----
  if (now - gLastFrameMs >= ANIM_INTERVAL) {
    gAnimIdx++;
    gLastFrameMs = now;
    gFlash = !gFlash;
    switch (gState) {
      case ST_IDLE:     drawIdleScreen();     break;
      case ST_SLEEP:    drawSleepScreen();    break;
      case ST_APPROVAL: drawApprovalScreen(); break;
      case ST_WORKING:  drawWorkScreen();     break;
      default: break;
    }
  }

  // ---- 承認リクエスト処理 ----
  if (gState == ST_APPROVAL) {
    int16_t elapsed = (int16_t)((now - gReq.startMs) / 1000);
    gReq.remaining  = (int16_t)gReq.timeoutSec - elapsed;

    if (gReq.remaining <= 0) {
      // タイムアウト → 自動否認 + fed 大幅減
      addFed(FED_TIMEOUT);
      gBoo.denied++;
      sendJson(false);
      gState = ST_DENIED; gResultMs = now;
      drawResultScreen(false);
      return;
    }
    if (M5.BtnA.wasPressed()) {
      // 承認 → fed +2（ご飯をあげる！）
      addFed(FED_APPROVE);
      gBoo.approved++; gBoo.today++;
      sendJson(true);
      gState = ST_APPROVED; gResultMs = now;
      drawResultScreen(true);
      return;
    }
    if (M5.BtnB.wasPressed()) {
      // 否認 → fed -1（ブーがさびしがる）
      addFed(FED_DENY);
      gBoo.denied++;
      sendJson(false);
      gState = ST_DENIED; gResultMs = now;
      drawResultScreen(false);
      return;
    }
  }

  // ---- 結果表示 → アイドルへ ----
  if ((gState == ST_APPROVED || gState == ST_DENIED) &&
      (now - gResultMs >= RESULT_SHOW_MS)) {
    gState = ST_IDLE; gLastIdleMs = now;
    drawIdleScreen();
    return;
  }

  // ---- アイドル → スリープ ----
  if (gState == ST_IDLE &&
      (now - gLastIdleMs) / 1000 >= IDLE_SLEEP_SEC) {
    gState = ST_SLEEP;
    gSleepStartMs = now;
    drawSleepScreen();
  }

  // ---- スリープ中（energy 回復中） ----
  if (gState == ST_SLEEP) {
    if (M5.BtnA.wasPressed()) {
      // 起動 → 経過分の nappedSec を記録
      gBoo.nappedSec += (now - gSleepStartMs) / 1000;
      gState = ST_IDLE; gLastIdleMs = now;
      drawIdleScreen();
    }
  }

  // ---- アイドル操作 ----
  if (gState == ST_IDLE) {
    if (M5.BtnA.wasPressed()) {
      gState = ST_STATS; drawStatsScreen();
    }
    if (M5.BtnB.wasPressed()) {
      // ブーをなでる → fed +0.5 & ときめき表示
      addFed(FED_PET);
      cls(); drawArt(ART_EXCITED, MASCOT_Y); delay(600);
      drawIdleScreen();
    }
    gLastIdleMs = now;
  }

  // ---- スタッツ ----
  if (gState == ST_STATS) {
    if (M5.BtnA.wasPressed()) {
      gState = ST_IDLE; gLastIdleMs = now; drawIdleScreen();
    }
    if (M5.BtnB.wasPressed()) {
      // 統計リセット（fed/energy はリセットしない — ブーは生き続ける）
      gBoo.approved = gBoo.denied = gBoo.today = 0;
      gBoo.nappedSec = 0; gBoo.tokenTotal = 0;
      gBoo.sessionStart = now;
      drawStatsScreen();
    }
  }

  // ---- 作業中: ボタンで強制アイドル ----
  if (gState == ST_WORKING && M5.BtnA.wasPressed()) {
    gState = ST_IDLE; gLastIdleMs = now; drawIdleScreen();
  }
}
