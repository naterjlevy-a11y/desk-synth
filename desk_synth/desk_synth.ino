/*
  DESK SYNTH v5
  Hand down in front of the ultrasonic sensor = play. Position = note.

  MODES
   1 PIANO     C major scale
   2 BLUES     blues scale
   3 CHORDS    each key is a chord (C Dm Em F G Am Bb C), as a fast arpeggio
   4 JAM       drum beat + pentatonic scale
   5 THEREMIN  smooth pitch that glides with your hand
   7 SONGS     listen to a song, or learn it with your hand

  REMOTE (everywhere): 1-5, 7 mode | >>| |<< next/prev mode | 0 drums on/off
                       EQ beat pattern | UP/DOWN tempo | VOL+/VOL- octave
  REMOTE (in SONGS):   |<< >>| pick song | 1 or PLAY listen | 2 learn | 7 or STOP exit
  BUTTON: tap = octave (next song in SONGS), hold = next mode
  GESTURE: hand right against the sensor for 1.5 s = next mode

  Wiring: LCD 7-12, button 2, HC-SR04 trig 3 / echo 4, IR receiver 5, passive buzzer 6.
  Needs the "IRremote" library by Armin Joachimsmeyer.
*/

#define DECODE_NEC
#define IR_USE_AVR_TIMER1        // keep IR off Timer2 so it doesn't clash with tone()
#include <IRremote.hpp>
#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);
const byte BUTTON_PIN = 2;
const byte TRIG_PIN   = 3;
const byte ECHO_PIN   = 4;   // read with a pin-change interrupt
const byte IR_PIN     = 5;
const byte BUZZER_PIN = 6;

// Tuning
const bool DEBUG = false;
const int  FIRST_KEY_CM  = 6;
const int  KEY_WIDTH_CM  = 6;
const int  HYSTERESIS_CM = 2;
const bool SLIDE_CHANGES_NOTE = true;
const byte OFF_READINGS = 2;
const unsigned int  PING_MS = 30;
const unsigned long ECHO_MAX_US = 30000UL;
const float MENU_ZONE_MAX_CM = 4.0;
const unsigned int MENU_HOLD_MS  = 1500;
const unsigned int LONG_PRESS_MS = 600;
const byte THEREMIN_OCTAVES = 2;
const unsigned int ARP_MS = 45;
const int START_BPM = 100;

// Modes
enum { M_PIANO, M_BLUES, M_CHORDS, M_JAM, M_THEREMIN, M_SONGS, NUM_MODES };
const char* const MODE_NAMES[NUM_MODES] = {"PIANO", "BLUES", "CHORDS", "JAM", "THEREMIN", "SONGS"};

const byte NUM_KEYS = 8;
const byte SCALE_MAJOR[NUM_KEYS] = {0, 2, 4, 5, 7,  9, 11, 12};
const byte SCALE_BLUES[NUM_KEYS] = {0, 3, 5, 6, 7, 10, 12, 15};
const byte SCALE_PENTA[NUM_KEYS] = {0, 2, 4, 7, 9, 12, 14, 16};

const byte CHORDS[NUM_KEYS][4] = {
  {0, 4, 7, 12}, {2, 5, 9, 14}, {4, 7, 11, 16}, {5, 9, 12, 17},
  {7, 11, 14, 19}, {9, 12, 16, 21}, {10, 14, 17, 22}, {12, 16, 19, 24}
};
const char* const CHORD_NAMES[NUM_KEYS] = {"C", "Dm", "Em", "F", "G", "Am", "Bb", "C"};
const byte ARP_ORDER[6] = {0, 1, 2, 3, 2, 1};
const char* const NOTE_NAMES[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

// Drums
enum { KICK = 1, SNARE = 2, HAT = 4, BLIP = 8 };
const byte NUM_PATTERNS = 3;
const char* const PATTERN_NAMES[NUM_PATTERNS] = {"ROCK", "FUNK", "DISCO"};
const byte PATTERNS[NUM_PATTERNS][16] = {
  {KICK|HAT, 0, HAT, 0, SNARE|HAT, 0, HAT, 0, KICK|HAT, 0, KICK|HAT, 0, SNARE|HAT, 0, HAT, 0},
  {KICK|HAT, HAT, HAT, KICK, SNARE|HAT, HAT, HAT, KICK|HAT, HAT, KICK, KICK|HAT, HAT, SNARE|HAT, HAT, HAT, SNARE},
  {KICK, 0, HAT, 0, KICK|SNARE, 0, HAT, 0, KICK, 0, HAT, 0, KICK|SNARE, 0, HAT, HAT}
};

// Songs (public domain). Each note = key (0-7 on the C major keyboard) + length in eighth notes.
enum { kC, kD, kE, kF, kG, kA, kB, kC2 };
#define SN(k, d) (byte)(((k) << 4) | (d))
const byte SONG_END = 0xFF;

const byte SONG_ODE[] PROGMEM = {
  SN(kE,2),SN(kE,2),SN(kF,2),SN(kG,2), SN(kG,2),SN(kF,2),SN(kE,2),SN(kD,2),
  SN(kC,2),SN(kC,2),SN(kD,2),SN(kE,2), SN(kE,3),SN(kD,1),SN(kD,4),
  SN(kE,2),SN(kE,2),SN(kF,2),SN(kG,2), SN(kG,2),SN(kF,2),SN(kE,2),SN(kD,2),
  SN(kC,2),SN(kC,2),SN(kD,2),SN(kE,2), SN(kD,3),SN(kC,1),SN(kC,4),
  SONG_END
};
const byte SONG_SAINTS[] PROGMEM = {
  SN(kC,2),SN(kE,2),SN(kF,2),SN(kG,8),
  SN(kC,2),SN(kE,2),SN(kF,2),SN(kG,8),
  SN(kC,2),SN(kE,2),SN(kF,2),SN(kG,4),SN(kE,4),SN(kC,4),SN(kE,4),SN(kD,8),
  SN(kE,2),SN(kE,2),SN(kD,2),SN(kC,6),SN(kC,2),SN(kE,4),SN(kG,4),SN(kG,2),SN(kF,8),
  SN(kE,2),SN(kF,2),SN(kG,4),SN(kE,4),SN(kC,4),SN(kD,4),SN(kC,8),
  SONG_END
};
const byte SONG_JINGLE[] PROGMEM = {
  SN(kE,2),SN(kE,2),SN(kE,4), SN(kE,2),SN(kE,2),SN(kE,4),
  SN(kE,2),SN(kG,2),SN(kC,3),SN(kD,1), SN(kE,8),
  SN(kF,2),SN(kF,2),SN(kF,3),SN(kF,1), SN(kF,2),SN(kE,2),SN(kE,2),SN(kE,1),SN(kE,1),
  SN(kE,2),SN(kD,2),SN(kD,2),SN(kE,2), SN(kD,4),SN(kG,4),
  SN(kE,2),SN(kE,2),SN(kE,4), SN(kE,2),SN(kE,2),SN(kE,4),
  SN(kE,2),SN(kG,2),SN(kC,3),SN(kD,1), SN(kE,8),
  SN(kF,2),SN(kF,2),SN(kF,3),SN(kF,1), SN(kF,2),SN(kE,2),SN(kE,2),SN(kE,1),SN(kE,1),
  SN(kG,2),SN(kG,2),SN(kF,2),SN(kD,2), SN(kC,8),
  SONG_END
};
const byte SONG_TWINKLE[] PROGMEM = {
  SN(kC,2),SN(kC,2),SN(kG,2),SN(kG,2),SN(kA,2),SN(kA,2),SN(kG,4),
  SN(kF,2),SN(kF,2),SN(kE,2),SN(kE,2),SN(kD,2),SN(kD,2),SN(kC,4),
  SN(kG,2),SN(kG,2),SN(kF,2),SN(kF,2),SN(kE,2),SN(kE,2),SN(kD,4),
  SN(kG,2),SN(kG,2),SN(kF,2),SN(kF,2),SN(kE,2),SN(kE,2),SN(kD,4),
  SN(kC,2),SN(kC,2),SN(kG,2),SN(kG,2),SN(kA,2),SN(kA,2),SN(kG,4),
  SN(kF,2),SN(kF,2),SN(kE,2),SN(kE,2),SN(kD,2),SN(kD,2),SN(kC,4),
  SONG_END
};

const byte NUM_SONGS = 4;
const byte* const SONG_DATA[NUM_SONGS] = {SONG_ODE, SONG_SAINTS, SONG_JINGLE, SONG_TWINKLE};
const char* const SONG_TITLES[NUM_SONGS] = {"Ode to Joy", "When the Saints", "Jingle Bells", "Twinkle Twinkle"};
const unsigned int SONG_UNIT_MS[NUM_SONGS] = {230, 180, 150, 250};

enum { S_MENU, S_LISTEN, S_LEARN, S_DONE };

// LCD characters
enum { CH_KEY_L, CH_KEY_R, CH_PRESS };
byte keyL[8]  = {0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
byte keyR[8]  = {0b11111, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b11111};
byte press[8] = {0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111};

// State
byte mode = M_PIANO;
byte octave = 4;
int  currentKey = -1;
bool thereminOn = false;
unsigned int lastFreq = 0;
int  lastBarCol = -1;
int  lastNearest = -100;
byte missCount = 0;
int  slideKey = -1;
byte slideCount = 0;

volatile unsigned long echoRise = 0;
volatile unsigned long echoWidth = 0;
volatile bool echoReady = false;
bool pingInFlight = false;
unsigned long pingSentAt = 0;
long readingsUs[3] = {-1, -1, -1};
byte readIdx = 0;

unsigned long menuStart = 0;
bool menuArmed = true;
int  lastMenuBars = -1;

unsigned int curFreq = 0;
unsigned int melFreqs[4];
byte melCount = 0;
byte arpPos = 0;
unsigned long lastArp = 0;
byte fxType = 0;
unsigned long fxStart = 0;
byte fxLen = 0;
unsigned int blipFreq = 0;
unsigned long lastFxUpdate = 0;

bool drumsOn = false;
byte pattern = 0;
int  bpm = START_BPM;
unsigned long stepMs = 15000UL / START_BPM;
byte step = 15;
unsigned long lastStep = 0;

byte songState = S_MENU;
byte songIdx = 0;
int  songPos = 0;
int  songLen = 0;
unsigned long songNoteAt = 0;
unsigned long songNoteMs = 0;
bool songGap = false;
unsigned long learnStart = 0;
int  learnWrong = 0;
unsigned long lastBlink = 0;
bool blinkOn = false;
unsigned long doneAt = 0;
int  doneAcc = 0;
unsigned int doneSecs = 0;

unsigned long restoreInfoAt = 0;
char lastName[4] = "--";
bool buttonWasDown = false;
bool longFired = false;
unsigned long lastButtonChange = 0;
unsigned long pressStart = 0;

ISR(PCINT2_vect) {
  unsigned long t = micros();
  if (PIND & _BV(4)) {
    echoRise = t;
  } else if (echoRise) {
    echoWidth = t - echoRise;
    echoRise = 0;
    echoReady = true;
  }
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  if (DEBUG) Serial.begin(115200);

  PCICR  |= _BV(PCIE2);
  PCMSK2 |= _BV(PCINT20);

  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);

  lcd.begin(16, 2);
  lcd.createChar(CH_KEY_L, keyL);
  lcd.createChar(CH_KEY_R, keyR);
  lcd.createChar(CH_PRESS, press);

  lcd.print("   DESK SYNTH");
  lcd.setCursor(0, 1);
  lcd.print("  v5 + songs");
  int hello[] = {262, 330, 392, 523};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, hello[i], 90);
    delay(110);
  }
  delay(800);

  lcd.clear();
  drawTop();
  drawInfo();
}

void loop() {
  sensorUpdate();
  songUpdate();
  audioUpdate();
  handleButton();
  handleIR();

  if (restoreInfoAt && millis() >= restoreInfoAt) {
    restoreInfoAt = 0;
    drawInfo();
  }
}

// ===== SENSOR =====
void sensorUpdate() {
  unsigned long now = millis();

  if (echoReady) {
    noInterrupts();
    unsigned long w = echoWidth;
    echoReady = false;
    interrupts();
    pingInFlight = false;
    newReading((w < 116 || w > ECHO_MAX_US) ? -1 : (long)w);
  } else if (pingInFlight && now - pingSentAt > 35) {
    pingInFlight = false;
    noInterrupts();
    echoRise = 0;
    interrupts();
    newReading(-1);
  }

  if (!pingInFlight && now - pingSentAt >= PING_MS && !(PIND & _BV(4))) {
    noInterrupts();
    echoRise = 0;
    interrupts();
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    pingSentAt = now;
    pingInFlight = true;
  }
}

void newReading(long us) {
  readingsUs[readIdx] = us;
  readIdx = (readIdx + 1) % 3;
  long med = median3(readingsUs[0], readingsUs[1], readingsUs[2]);
  float cm = (med < 0) ? -1.0 : med / 58.0;
  if (DEBUG) {
    Serial.print("distance: ");
    Serial.print(cm, 1);
    Serial.println(" cm");
  }
  handleHand(cm);
}

long median3(long a, long b, long c) {
  long t;
  if (a > b) { t = a; a = b; b = t; }
  if (b > c) { t = b; b = c; c = t; }
  if (a > b) { t = a; a = b; b = t; }
  return b;
}

// ===== HAND LOGIC =====
void handleHand(float cm) {
  if (cm >= 0 && cm <= MENU_ZONE_MAX_CM) {
    if (currentKey >= 0 || thereminOn) stopSound();
    if (menuArmed) {
      if (menuStart == 0) {
        menuStart = millis();
        lastMenuBars = -1;
      }
      unsigned long held = millis() - menuStart;
      if (held >= MENU_HOLD_MS) {
        menuArmed = false;
        menuStart = 0;
        nextMode();
      } else {
        showMenuProgress(held);
      }
    }
    return;
  }
  if (menuStart) {
    menuStart = 0;
    drawInfo();
  }
  menuArmed = true;

  if (mode == M_SONGS && songState != S_LEARN) return;
  if (mode == M_THEREMIN) handleTheremin(cm);
  else                    handleKeys(cm);
}

void handleKeys(float cm) {
  int k = keyFor(cm);
  if (k >= 0) {
    missCount = 0;
    if (currentKey < 0) {
      startKey(k);
    } else if (k != currentKey && SLIDE_CHANGES_NOTE) {
      if (k == slideKey) {
        if (++slideCount >= 2) startKey(k);
      } else {
        slideKey = k;
        slideCount = 1;
      }
    } else {
      slideKey = -1;
      slideCount = 0;
    }
  } else if (currentKey >= 0 && ++missCount >= OFF_READINGS) {
    stopSound();
  }
}

void handleTheremin(float cm) {
  float span = NUM_KEYS * KEY_WIDTH_CM;
  if (cm < FIRST_KEY_CM || cm > FIRST_KEY_CM + span) {
    if (thereminOn && ++missCount >= OFF_READINGS) stopSound();
    return;
  }
  missCount = 0;

  float pos = (cm - FIRST_KEY_CM) / span;
  float semis = pos * 12.0 * THEREMIN_OCTAVES;
  unsigned int f = freqFor(semis);
  if (!thereminOn || abs((int)f - (int)lastFreq) > (int)(lastFreq / 200)) {
    melFreqs[0] = f;
    melCount = 1;
    lastFreq = f;
  }
  thereminOn = true;

  int col = constrain((int)(pos * 16), 0, 15);
  if (col != lastBarCol) {
    lastBarCol = col;
    drawTop();
  }
  int nearest = (int)(semis + 0.5);
  if (nearest != lastNearest) {
    lastNearest = nearest;
    setNoteName(nearest);
    if (!restoreInfoAt) drawInfo();
  }
}

int keyFromCm(float cm) {
  if (cm < FIRST_KEY_CM) return -1;
  int k = (int)((cm - FIRST_KEY_CM) / KEY_WIDTH_CM);
  return (k < NUM_KEYS) ? k : -1;
}

int keyFor(float cm) {
  if (cm < 0) return -1;
  if (currentKey >= 0) {
    float lo = FIRST_KEY_CM + currentKey * KEY_WIDTH_CM - HYSTERESIS_CM;
    float hi = FIRST_KEY_CM + (currentKey + 1) * KEY_WIDTH_CM + HYSTERESIS_CM;
    if (cm >= lo && cm < hi) return currentKey;
  }
  return keyFromCm(cm);
}

const byte* scaleForMode() {
  if (mode == M_BLUES) return SCALE_BLUES;
  if (mode == M_JAM)   return SCALE_PENTA;
  return SCALE_MAJOR;
}

void startKey(int k) {
  currentKey = k;
  slideKey = -1;
  slideCount = 0;

  if (mode == M_CHORDS) {
    for (byte i = 0; i < 4; i++) melFreqs[i] = freqFor(CHORDS[k][i]);
    melCount = 4;
    arpPos = 0;
    lastArp = millis();
    strcpy(lastName, CHORD_NAMES[k]);
  } else {
    int semi = scaleForMode()[k];
    melFreqs[0] = freqFor(semi);
    melCount = 1;
    setNoteName(semi);
  }
  drawTop();
  if (mode == M_SONGS && songState == S_LEARN) learnKey(k);
  if (!restoreInfoAt) drawInfo();
}

void stopSound() {
  melCount = 0;
  currentKey = -1;
  thereminOn = false;
  missCount = 0;
  lastFreq = 0;
  lastBarCol = -1;
  lastNearest = -100;
  slideKey = -1;
  slideCount = 0;
  drawTop();
}

// ===== SOUND ENGINE =====
unsigned int freqFor(float semis) {
  float f = 261.63 * pow(2.0, (octave - 4) + semis / 12.0);
  return (unsigned int)(f + 0.5);
}

void setNoteName(int semi) {
  snprintf(lastName, sizeof(lastName), "%s%d", NOTE_NAMES[semi % 12], octave + semi / 12);
}

void outFreq(unsigned int f) {
  if (f == curFreq) return;
  if (f == 0) noTone(BUZZER_PIN);
  else        tone(BUZZER_PIN, f);
  curFreq = f;
}

void startFx(byte type, byte len) {
  fxType = type;
  fxLen = len;
  fxStart = millis();
  lastFxUpdate = fxStart - 1;
}

void playBlip(unsigned int f, byte len) {
  blipFreq = f;
  startFx(BLIP, len);
}

bool updateFx(unsigned long now) {
  if (!fxType) return false;
  unsigned long e = now - fxStart;
  if (e >= fxLen) {
    fxType = 0;
    return false;
  }
  if (now != lastFxUpdate) {
    lastFxUpdate = now;
    unsigned int f;
    if (fxType == KICK)       f = 170 - (unsigned int)(e * 120 / fxLen);
    else if (fxType == SNARE) f = random(1500, 5000);
    else if (fxType == HAT)   f = random(7000, 11000);
    else                      f = blipFreq;
    outFreq(f);
  }
  return true;
}

void audioUpdate() {
  unsigned long now = millis();

  if (drumsOn && now - lastStep >= stepMs) {
    lastStep += stepMs;
    if (now - lastStep > stepMs) lastStep = now;
    step = (step + 1) % 16;
    byte hit = PATTERNS[pattern][step];
    if (hit & SNARE)     startFx(SNARE, 40);
    else if (hit & KICK) startFx(KICK, 50);
    else if (hit & HAT)  startFx(HAT, 12);
  }

  if (updateFx(now)) return;

  if (melCount == 0) {
    outFreq(0);
    return;
  }
  if (melCount > 1 && now - lastArp >= ARP_MS) {
    lastArp = now;
    arpPos = (arpPos + 1) % 6;
  }
  outFreq(melCount > 1 ? melFreqs[ARP_ORDER[arpPos]] : melFreqs[0]);
}

// ===== SONGS =====
byte songByte(int i) { return pgm_read_byte(SONG_DATA[songIdx] + i); }
byte songKey(int i)  { return songByte(i) >> 4; }
byte songDur(int i)  { return songByte(i) & 0x0F; }

int songLength() {
  int n = 0;
  while (songByte(n) != SONG_END) n++;
  return n;
}

void songMenu() {
  stopSound();
  songState = S_MENU;
  drawTop();
  drawInfo();
}

void pickSong(int dir) {
  songIdx = (songIdx + NUM_SONGS + dir) % NUM_SONGS;
  songMenu();
  playBlip(1200, 25);
}

void startListen() {
  stopSound();
  songState = S_LISTEN;
  songLen = songLength();
  songPos = 0;
  playSongNote();
  drawInfo();
}

void playSongNote() {
  byte k = songKey(songPos);
  currentKey = k;
  melFreqs[0] = freqFor(SCALE_MAJOR[k]);
  melCount = 1;
  songNoteMs = (unsigned long)songDur(songPos) * SONG_UNIT_MS[songIdx];
  songNoteAt = millis();
  songGap = false;
  drawTop();
}

void startLearn() {
  stopSound();
  songState = S_LEARN;
  songLen = songLength();
  songPos = 0;
  learnWrong = 0;
  learnStart = 0;
  blinkOn = true;
  lastBlink = millis();
  drawTop();
  drawInfo();
}

void learnKey(int k) {
  if (k == songKey(songPos)) {
    if (songPos == 0) learnStart = millis();
    songPos++;
    if (songPos >= songLen) finishLearn();
  } else {
    learnWrong++;
  }
}

void finishLearn() {
  stopSound();
  songState = S_DONE;
  doneAt = millis();
  doneAcc = (int)((long)songLen * 100 / (songLen + learnWrong));
  doneSecs = (millis() - learnStart) / 1000;
  drawTop();
  playBlip(2093, 250);
}

void songUpdate() {
  if (mode != M_SONGS) return;
  unsigned long now = millis();

  if (songState == S_LISTEN) {
    unsigned long e = now - songNoteAt;
    if (!songGap && e >= songNoteMs * 85 / 100) {
      melCount = 0;
      songGap = true;
    }
    if (e >= songNoteMs) {
      songPos++;
      if (songPos >= songLen) songMenu();
      else playSongNote();
    }
  } else if (songState == S_LEARN) {
    if (now - lastBlink >= 250) {
      lastBlink = now;
      blinkOn = !blinkOn;
      drawTop();
    }
  } else if (songState == S_DONE) {
    if (now - doneAt >= 4000) songMenu();
  }
}

// ===== MODES / SETTINGS =====
void setMode(byte m) {
  stopSound();
  mode = m;
  songState = S_MENU;
  setDrums(m == M_JAM);
  strcpy(lastName, "--");
  restoreInfoAt = 0;
  drawTop();
  drawInfo();
  playBlip(1568, 90);
}

void nextMode() { setMode((mode + 1) % NUM_MODES); }
void prevMode() { setMode((mode + NUM_MODES - 1) % NUM_MODES); }

void setDrums(bool on) {
  drumsOn = on;
  if (on) {
    step = 15;
    lastStep = millis() - stepMs;
  }
  drawInfo();
}

void changeTempo(int delta) {
  bpm = constrain(bpm + delta, 60, 180);
  stepMs = 15000UL / bpm;
  char buf[17];
  snprintf(buf, sizeof(buf), "Tempo %d bpm", bpm);
  showTemp(buf);
}

void nextPattern() {
  pattern = (pattern + 1) % NUM_PATTERNS;
  if (!drumsOn) setDrums(true);
  char buf[17];
  snprintf(buf, sizeof(buf), "Beat: %s", PATTERN_NAMES[pattern]);
  showTemp(buf);
}

void changeOctave(int dir, bool wrap) {
  int o = octave + dir;
  if (o > 5) o = wrap ? 3 : 5;
  if (o < 3) o = wrap ? 5 : 3;
  octave = o;
  char buf[17];
  snprintf(buf, sizeof(buf), "Octave %d", octave);
  showTemp(buf);
  if (currentKey >= 0 && mode != M_SONGS) startKey(currentKey);
  lastFreq = 0;
}

void showMenuProgress(unsigned long held) {
  int bars = held * 10 / MENU_HOLD_MS;
  if (bars == lastMenuBars) return;
  lastMenuBars = bars;
  lcd.setCursor(0, 1);
  lcd.print("Next mode ");
  for (int i = 0; i < 6; i++) {
    if (i < bars * 6 / 10) lcd.write(byte(CH_PRESS));
    else                   lcd.write('.');
  }
  playBlip(2000 + bars * 100, 10);
}

// ===== INPUTS =====
void handleButton() {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down != buttonWasDown && millis() - lastButtonChange > 30) {
    lastButtonChange = millis();
    buttonWasDown = down;
    if (down) {
      pressStart = millis();
      longFired = false;
    } else if (!longFired) {
      if (mode == M_SONGS) pickSong(+1);
      else                 changeOctave(+1, true);
    }
  }
  if (buttonWasDown && !longFired && millis() - pressStart >= LONG_PRESS_MS) {
    longFired = true;
    nextMode();
  }
}

void handleIR() {
  if (!IrReceiver.decode()) return;
  uint8_t cmd = IrReceiver.decodedIRData.command;
  bool repeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
  IrReceiver.resume();
  if (repeat) return;

  if (mode == M_SONGS) {
    switch (cmd) {
      case 0x44: pickSong(-1); return;
      case 0x43: pickSong(+1); return;
      case 0x0C:
      case 0x40:
        if (songState == S_LISTEN) songMenu();
        else startListen();
        return;
      case 0x18: startLearn(); return;
      case 0x42:
      case 0x47: setMode(M_PIANO); return;
    }
  }

  switch (cmd) {
    case 0x0C: setMode(M_PIANO);    break;
    case 0x18: setMode(M_BLUES);    break;
    case 0x5E: setMode(M_CHORDS);   break;
    case 0x08: setMode(M_JAM);      break;
    case 0x1C: setMode(M_THEREMIN); break;
    case 0x42: setMode(M_SONGS);    break;
    case 0x43: nextMode();          break;
    case 0x44: prevMode();          break;
    case 0x16:
    case 0x40: setDrums(!drumsOn);  break;
    case 0x19: nextPattern();       break;
    case 0x09: changeTempo(+10);    break;
    case 0x07: changeTempo(-10);    break;
    case 0x46: changeOctave(+1, false); break;
    case 0x15: changeOctave(-1, false); break;
  }
}

// ===== DISPLAY =====
void drawKeys(int lit, int extra) {
  for (int k = 0; k < NUM_KEYS; k++) {
    if (k == lit || k == extra) {
      lcd.write(byte(CH_PRESS));
      lcd.write(byte(CH_PRESS));
    } else {
      lcd.write(byte(CH_KEY_L));
      lcd.write(byte(CH_KEY_R));
    }
  }
}

void drawSongTitle() {
  char buf[17];
  memset(buf, ' ', 16);
  buf[16] = 0;
  buf[0] = '<';
  buf[15] = '>';
  const char* t = SONG_TITLES[songIdx];
  byte len = strlen(t);
  if (len > 14) len = 14;
  memcpy(buf + 1 + (14 - len) / 2, t, len);
  lcd.print(buf);
}

void drawTop() {
  lcd.setCursor(0, 0);
  if (mode == M_THEREMIN) {
    if (!thereminOn) {
      lcd.print("  ~ THEREMIN ~  ");
      return;
    }
    for (int c = 0; c < 16; c++) {
      lcd.write(c <= lastBarCol ? byte(CH_PRESS) : byte(' '));
    }
    return;
  }
  if (mode == M_SONGS) {
    if (songState == S_MENU) { drawSongTitle(); return; }
    if (songState == S_DONE) { lcd.print("  SONG COMPLETE!"); return; }
    int target = (songState == S_LEARN && blinkOn) ? songKey(songPos) : -1;
    drawKeys(currentKey, target);
    return;
  }
  drawKeys(currentKey, -1);
}

void drawInfo() {
  char buf[17];
  if (mode == M_SONGS) {
    char tmp[17];
    if (songState == S_MENU) {
      strcpy(tmp, "1:Listen 2:Learn");
    } else if (songState == S_LISTEN) {
      snprintf(tmp, sizeof(tmp), "> %s", SONG_TITLES[songIdx]);
    } else if (songState == S_LEARN) {
      snprintf(tmp, sizeof(tmp), "Next: %-2s   %d/%d",
               NOTE_NAMES[SCALE_MAJOR[songKey(songPos)] % 12], songPos + 1, songLen);
    } else {
      snprintf(tmp, sizeof(tmp), "Acc %d%%  %u sec", doneAcc, doneSecs);
    }
    snprintf(buf, sizeof(buf), "%-16s", tmp);
  } else {
    char label[14];
    if (drumsOn) snprintf(label, sizeof(label), "%s %d", MODE_NAMES[mode], bpm);
    else         snprintf(label, sizeof(label), "%s", MODE_NAMES[mode]);
    snprintf(buf, sizeof(buf), "%-3s%13s", lastName, label);
  }
  lcd.setCursor(0, 1);
  lcd.print(buf);
}

void showTemp(const char* msg) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", msg);
  lcd.setCursor(0, 1);
  lcd.print(buf);
  restoreInfoAt = millis() + 1200;
}
