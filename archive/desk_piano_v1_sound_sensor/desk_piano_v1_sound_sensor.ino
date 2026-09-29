/*
  DESK PIANO
  Tap your desk to play notes. WHERE you tap picks the note.

  - HC-SR04 lies on the side of the desk, aimed along the desk surface.
    Your hand's distance from it = which key (C major scale, C to C).
  - Sound sensor taped to the desk hears/feels the tap and triggers the note.
  - LCD row 1: an 8-key keyboard. Hovered key is half-filled, played key lights up.
  - LCD row 2: last note played + a scrolling trail of your melody.
  - Button: cycle octave (3 / 4 / 5).

  Elegoo Super Starter Kit (UNO R3):
  LCD1602, 10k pot, HC-SR04, sound sensor module, PASSIVE buzzer, button, 220 ohm resistor.

  Tuning: set DEBUG = true, open Serial Monitor at 115200, and watch the
  mic levels and distances while you tap. Then adjust TAP_THRESHOLD and the key sizes.
*/

#include <LiquidCrystal.h>

// ---------------- Pins ----------------
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);   // RS, E, D4, D5, D6, D7
const byte BUTTON_PIN = 2;
const byte TRIG_PIN   = 3;
const byte ECHO_PIN   = 4;
const byte BUZZER_PIN = 6;
const byte MIC_PIN    = A0;               // sound sensor AO

// ---------------- Tuning ----------------
const bool DEBUG = false;                 // true = print levels to Serial Monitor
const int  FIRST_KEY_CM  = 5;             // where the first key (C) starts, from the sensor
const int  KEY_WIDTH_CM  = 4;             // width of each key on your paper keyboard
const int  TAP_THRESHOLD = 25;            // higher = needs a harder tap; lower = more sensitive
const unsigned int NOTE_MS    = 220;      // note length
const unsigned int LOCKOUT_MS = NOTE_MS + 40;  // mic ignored while buzzer plays (it'd hear itself)

// ---------------- Notes (C major, C4 to C5) ----------------
const byte  NUM_KEYS = 8;
const char  NOTE_LETTERS[NUM_KEYS] = {'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C'};
const float C4_FREQS[NUM_KEYS] = {261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25};

// ---------------- LCD keyboard characters ----------------
// Each key is 2 characters wide (left half + right half) so 8 keys fill 16 columns.
enum { CH_KEY_L, CH_KEY_R, CH_HOV_L, CH_HOV_R, CH_PRESS };
byte keyL[8]  = {0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
byte keyR[8]  = {0b11111, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b11111};
byte hovL[8]  = {0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111, 0b11111, 0b11111};
byte hovR[8]  = {0b11111, 0b00001, 0b00001, 0b00001, 0b00001, 0b11111, 0b11111, 0b11111};
byte press[8] = {0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111};

// ---------------- State ----------------
int  hoverKey   = -1;
int  pressedKey = -1;
byte octave     = 4;
unsigned long pressedUntil = 0;
unsigned long lockUntil    = 0;
unsigned long lastPing     = 0;
unsigned long restoreInfoAt = 0;

long baseline64 = 512L * 64;   // mic resting level, x64 for smooth integer averaging
int  peakDev    = 0;           // for DEBUG
unsigned long lastDebug = 0;

char history[13] = "            ";  // 12-char melody trail
char lastName[4] = "--";

bool buttonWasDown = false;
unsigned long lastButtonChange = 0;

// ============================================================
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  if (DEBUG) Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.createChar(CH_KEY_L, keyL);
  lcd.createChar(CH_KEY_R, keyR);
  lcd.createChar(CH_HOV_L, hovL);
  lcd.createChar(CH_HOV_R, hovR);
  lcd.createChar(CH_PRESS, press);

  // Splash + calibrate the mic's resting level
  lcd.print("  DESK  PIANO");
  lcd.setCursor(0, 1);
  lcd.print("Quiet... tuning");
  long sum = 0;
  for (int i = 0; i < 256; i++) {
    sum += analogRead(MIC_PIN);
    delay(2);
  }
  baseline64 = (sum / 256) * 64;

  // Little C-E-G hello
  int hello[] = {262, 330, 392, 523};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, hello[i], 90);
    delay(110);
  }

  lcd.clear();
  drawKeyboard();
  drawInfo();
}

void loop() {
  // 1) Tap on the desk -> play the key under your hand
  if (detectTap()) {
    int k = measureKeyNow();
    if (k < 0) k = hoverKey;          // fall back to the last known hand position
    if (k >= 0) playKey(k);
    else lockUntil = millis() + 120;  // tapped outside the keyboard: ignore the echoing thump
  }

  // 2) Track the hovering hand so the LCD shows which key you're over
  if (millis() - lastPing >= 40) {
    int k = keyFromCm(measureCm());
    if (k != hoverKey) {
      hoverKey = k;
      drawKeyboard();
    }
  }

  // 3) Un-light the played key when the note ends
  if (pressedKey >= 0 && millis() >= pressedUntil) {
    pressedKey = -1;
    drawKeyboard();
  }

  // 4) Octave button
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down != buttonWasDown && millis() - lastButtonChange > 30) {
    lastButtonChange = millis();
    buttonWasDown = down;
    if (down) changeOctave();
  }

  if (restoreInfoAt && millis() >= restoreInfoAt) {
    restoreInfoAt = 0;
    drawInfo();
  }

  // 5) Debug readout for tuning
  if (DEBUG && millis() - lastDebug >= 150) {
    lastDebug = millis();
    Serial.print("mic peak: ");
    Serial.print(peakDev);
    Serial.print("   (threshold ");
    Serial.print(TAP_THRESHOLD);
    Serial.print(")   hover key: ");
    Serial.println(hoverKey);
    peakDev = 0;
  }
}

// ============================================================
// Tap detection: a sudden jump away from the mic's resting level
bool detectTap() {
  int v    = analogRead(MIC_PIN);
  int base = baseline64 / 64;
  int dev  = abs(v - base);
  baseline64 += v - base;             // slow moving average follows the resting level
  if (dev > peakDev) peakDev = dev;

  return dev > TAP_THRESHOLD && millis() >= lockUntil;
}

// ============================================================
// Ultrasonic distance in cm, or -1 if nothing within ~60 cm
int measureCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long us = pulseIn(ECHO_PIN, HIGH, 3500UL);
  lastPing = millis();
  if (us == 0) return -1;
  return us / 58;
}

// Fresh reading right after a tap (your hand is on the desk, in the beam).
// Waits until the last ping's echoes have died out so readings don't get mixed up.
int measureKeyNow() {
  while (millis() - lastPing < 25) { }
  return keyFromCm(measureCm());
}

int keyFromCm(int cm) {
  if (cm < FIRST_KEY_CM) return -1;
  int k = (cm - FIRST_KEY_CM) / KEY_WIDTH_CM;
  return (k < NUM_KEYS) ? k : -1;
}

// ============================================================
void playKey(int k) {
  float f = C4_FREQS[k];
  if (octave == 3) f *= 0.5;
  else if (octave == 5) f *= 2.0;
  tone(BUZZER_PIN, (unsigned int)(f + 0.5), NOTE_MS);

  pressedKey   = k;
  pressedUntil = millis() + NOTE_MS;
  lockUntil    = millis() + LOCKOUT_MS;

  // Melody trail: shift left, add new letter on the right
  memmove(history, history + 1, 11);
  history[11] = NOTE_LETTERS[k];

  byte shownOct = octave + (k == NUM_KEYS - 1 ? 1 : 0);   // top C is the next octave
  snprintf(lastName, sizeof(lastName), "%c%d", NOTE_LETTERS[k], shownOct);

  drawKeyboard();
  if (!restoreInfoAt) drawInfo();
}

void changeOctave() {
  octave = (octave >= 5) ? 3 : octave + 1;
  lcd.setCursor(0, 1);
  char buf[17];
  snprintf(buf, sizeof(buf), "Octave %d (C%d-C%d)", octave, octave, octave + 1);
  lcd.print(buf);
  restoreInfoAt = millis() + 1000;
  tone(BUZZER_PIN, (unsigned int)(C4_FREQS[0] * (octave == 3 ? 0.5 : octave == 5 ? 2.0 : 1.0)), 80);
  lockUntil = millis() + 150;   // the click of the button shouldn't count as a tap
}

// ============================================================
void drawKeyboard() {
  lcd.setCursor(0, 0);
  for (int k = 0; k < NUM_KEYS; k++) {
    if (k == pressedKey) {
      lcd.write(byte(CH_PRESS));
      lcd.write(byte(CH_PRESS));
    } else if (k == hoverKey) {
      lcd.write(byte(CH_HOV_L));
      lcd.write(byte(CH_HOV_R));
    } else {
      lcd.write(byte(CH_KEY_L));
      lcd.write(byte(CH_KEY_R));
    }
  }
}

void drawInfo() {
  lcd.setCursor(0, 1);
  char buf[17];
  snprintf(buf, sizeof(buf), "%-3s|%s", lastName, history);  // e.g. "E4 |  CDECEGG"
  lcd.print(buf);
}
