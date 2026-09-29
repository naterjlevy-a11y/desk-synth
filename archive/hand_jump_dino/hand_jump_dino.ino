/*
  HAND-JUMP DINO
  An endless runner on the LCD1602 that you play by WAVING YOUR HAND
  over an ultrasonic sensor (a push button works too).

  - Cacti on the ground: jump over them.
  - Birds in the air: DON'T jump, or you fly straight into them.
  - Game speeds up and gets busier as your score climbs.
  - High score is saved to EEPROM (survives power-off).
    Hold the button while powering on to reset it.

  Hardware (Elegoo Super Starter Kit, UNO R3):
  LCD1602, 10k potentiometer, HC-SR04, push button, PASSIVE buzzer, 220 ohm resistor.
*/

#include <LiquidCrystal.h>
#include <EEPROM.h>

// ---------------- Pins ----------------
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);   // RS, E, D4, D5, D6, D7
const byte BUTTON_PIN = 2;
const byte TRIG_PIN   = 3;
const byte ECHO_PIN   = 4;
const byte BUZZER_PIN = 6;                // passive buzzer

// ---------------- Tuning ----------------
const int  HAND_CM        = 12;   // hand closer than this = jump
const byte JUMP_TICKS     = 3;    // frames the dino stays in the air
const int  START_INTERVAL = 220;  // ms per frame at the start
const int  MIN_INTERVAL   = 70;   // ms per frame at top speed
const byte DINO_COL       = 1;
const byte MIN_GAP        = 4;    // min frames between obstacles (keeps it beatable)

// ---------------- Custom characters ----------------
enum { CH_RUN1, CH_RUN2, CH_JUMP, CH_CACTUS, CH_BIRD1, CH_BIRD2, CH_BOOM };

byte runA[8]   = {0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b01000};
byte runB[8]   = {0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b01010, 0b00010};
byte jumpC[8]  = {0b00111, 0b00101, 0b00111, 0b10110, 0b11111, 0b01110, 0b10001, 0b00000};
byte cactus[8] = {0b00100, 0b10101, 0b10101, 0b11111, 0b00100, 0b00100, 0b00100, 0b00100};
byte birdA[8]  = {0b00000, 0b00000, 0b01100, 0b11111, 0b00110, 0b00000, 0b00000, 0b00000};
byte birdB[8]  = {0b01000, 0b01100, 0b01100, 0b11111, 0b00110, 0b00000, 0b00000, 0b00000};
byte boom[8]   = {0b00000, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101, 0b00000, 0b00000};

// ---------------- Game state ----------------
enum Obstacle { EMPTY, CACTUS, BIRD };
enum State { TITLE, PLAYING, OVER };

State state = TITLE;
byte world[2][16];            // [row][col], row 0 = air, row 1 = ground
unsigned int score = 0;
unsigned int best  = 0;
unsigned long frame = 0;
unsigned long lastTick = 0;
unsigned long overAt = 0;
int  interval = START_INTERVAL;
byte jumpTicks = 0;
byte gap = 0;

// Input
bool jumpRequested = false;
bool handWasNear = false;
bool buttonWasDown = false;
unsigned long lastPing = 0;

// Title screen animation
unsigned long lastTitleAnim = 0;
byte titlePhase = 0;

// ============================================================
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.begin(16, 2);
  lcd.createChar(CH_RUN1, runA);
  lcd.createChar(CH_RUN2, runB);
  lcd.createChar(CH_JUMP, jumpC);
  lcd.createChar(CH_CACTUS, cactus);
  lcd.createChar(CH_BIRD1, birdA);
  lcd.createChar(CH_BIRD2, birdB);
  lcd.createChar(CH_BOOM, boom);

  randomSeed(analogRead(A5));   // floating pin = random noise

  EEPROM.get(0, best);
  if (best == 0xFFFF) best = 0; // fresh EEPROM reads as 0xFFFF

  // Hold the button at power-on to wipe the high score
  if (digitalRead(BUTTON_PIN) == LOW) {
    best = 0;
    EEPROM.put(0, best);
    lcd.print("High score reset");
    tone(BUZZER_PIN, 400, 300);
    delay(1500);
  }

  showTitle();
}

void loop() {
  pollInput();

  switch (state) {
    case TITLE:
      animateTitle();
      if (jumpRequested) startGame();
      break;

    case PLAYING:
      if (millis() - lastTick >= (unsigned long)interval) tick();
      break;

    case OVER:
      // Ignore input briefly so a panicked wave doesn't skip the score screen
      if (millis() - overAt < 1500) jumpRequested = false;
      else if (jumpRequested) startGame();
      break;
  }
}

// ============================================================
// Input: button (edge) + hand wave over ultrasonic sensor (edge)
void pollInput() {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !buttonWasDown) jumpRequested = true;
  buttonWasDown = down;

  if (millis() - lastPing >= 50) {
    lastPing = millis();
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long us = pulseIn(ECHO_PIN, HIGH, 6000UL); // ~1 m max, 0 = nothing
    bool near = (us > 0) && (us / 58 < (unsigned long)HAND_CM);
    if (near && !handWasNear) jumpRequested = true;     // one jump per wave
    handWasNear = near;
  }
}

// ============================================================
void showTitle() {
  state = TITLE;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" HAND-JUMP DINO");
  titlePhase = 0;
  lastTitleAnim = 0;
  jumpRequested = false;
}

void animateTitle() {
  if (millis() - lastTitleAnim < 250) return;
  lastTitleAnim = millis();
  titlePhase++;

  lcd.setCursor(0, 1);
  lcd.write(byte(titlePhase & 1 ? CH_RUN2 : CH_RUN1));

  // Alternate the bottom line every ~1.5 s
  lcd.setCursor(1, 1);
  if ((titlePhase / 6) % 2 == 0) {
    lcd.print(" Wave to play! ");
  } else {
    char buf[16];
    snprintf(buf, sizeof(buf), " Best: %-8u", best);
    lcd.print(buf);
  }
}

// ============================================================
void startGame() {
  memset(world, EMPTY, sizeof(world));
  score = 0;
  frame = 0;
  jumpTicks = 0;
  gap = 0;
  interval = START_INTERVAL;
  jumpRequested = false;
  state = PLAYING;
  lcd.clear();

  tone(BUZZER_PIN, 784, 60);
  delay(80);
  tone(BUZZER_PIN, 1047, 80);
  lastTick = millis();
}

void tick() {
  lastTick = millis();
  frame++;

  // Start a jump if requested and we're on the ground
  if (jumpRequested && jumpTicks == 0) {
    jumpTicks = JUMP_TICKS;
    tone(BUZZER_PIN, 1047, 40);
  }
  jumpRequested = false;
  bool inAir = jumpTicks > 0;

  // Scroll the world left
  for (byte r = 0; r < 2; r++) {
    for (byte c = 0; c < 15; c++) world[r][c] = world[r][c + 1];
    world[r][15] = EMPTY;
  }
  spawn();

  // Collision: in the air you can hit birds, on the ground you can hit cacti
  bool hit = inAir ? (world[0][DINO_COL] == BIRD)
                   : (world[1][DINO_COL] == CACTUS);

  drawWorld(inAir, hit);
  if (hit) {
    gameOver();
    return;
  }

  if (jumpTicks > 0) jumpTicks--;

  score++;
  if (score % 50 == 0) tone(BUZZER_PIN, 1568, 30);   // milestone blip
  interval = max(MIN_INTERVAL, START_INTERVAL - (int)(score / 3));
}

void spawn() {
  gap++;
  if (gap < MIN_GAP) return;

  int chance = 30 + min((int)(score / 20), 30);      // busier over time
  if (random(100) >= chance) return;

  if (score > 60 && random(100) < 30) world[0][15] = BIRD;   // birds unlock at 60
  else                                world[1][15] = CACTUS;
  gap = 0;
}

void drawWorld(bool inAir, bool crashed) {
  for (byte r = 0; r < 2; r++) {
    lcd.setCursor(0, r);
    for (byte c = 0; c < 16; c++) {
      bool dinoHere = (c == DINO_COL) && ((r == 0) == inAir);
      if (dinoHere) {
        if (crashed)    lcd.write(byte(CH_BOOM));
        else if (inAir) lcd.write(byte(CH_JUMP));
        else            lcd.write(byte(frame & 1 ? CH_RUN2 : CH_RUN1));
      } else if (world[r][c] == CACTUS) {
        lcd.write(byte(CH_CACTUS));
      } else if (world[r][c] == BIRD) {
        lcd.write(byte(frame & 1 ? CH_BIRD2 : CH_BIRD1));
      } else {
        lcd.write(' ');
      }
    }
  }
}

// ============================================================
void gameOver() {
  state = OVER;

  // Falling crash sound
  for (int f = 600; f > 150; f -= 50) {
    tone(BUZZER_PIN, f, 25);
    delay(28);
  }
  noTone(BUZZER_PIN);
  delay(700);

  bool newBest = score > best;
  if (newBest) {
    best = score;
    EEPROM.put(0, best);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(newBest ? "NEW HIGH SCORE!" : "   GAME  OVER");
  lcd.setCursor(0, 1);
  char buf[17];
  snprintf(buf, sizeof(buf), "S:%-5u Best:%u", score, best);
  lcd.print(buf);

  if (newBest) {
    int notes[] = {523, 659, 784, 1047};
    for (int i = 0; i < 4; i++) {
      tone(BUZZER_PIN, notes[i], 110);
      delay(130);
    }
    noTone(BUZZER_PIN);
  }

  overAt = millis();
  jumpRequested = false;
}
