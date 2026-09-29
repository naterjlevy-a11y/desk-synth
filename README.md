# Desk Synth

An air instrument on an Arduino UNO. An HC-SR04 ultrasonic sensor lies on the desk aimed along a paper keyboard. Put your hand down into the beam to play a note, and the distance picks which one. The LCD draws an 8-key keyboard with custom characters and lights up the key you're on.

Built the same night as [Sonar Guard](https://github.com/naterjlevy-a11y/sonar-guard), on the same Elegoo starter kit.

## Modes

| # | Mode | What it does |
| --- | --- | --- |
| 1 | Piano | C major scale, C to C |
| 2 | Blues | Blues scale |
| 3 | Chords | Each key is a chord (C Dm Em F G Am Bb C), played as a 45 ms arpeggio because one buzzer can only play one note at a time |
| 4 | Jam | 16-step drum machine (Rock, Funk, Disco) with a pentatonic scale on top |
| 5 | Theremin | Continuous pitch over 2 octaves that glides with your hand |
| 7 | Songs | Four public-domain songs. Listen plays one for you. Learn blinks the next key and scores your accuracy and time |

Keys start 6 cm from the sensor and are 6 cm wide, so the 8 keys cover 6 to 54 cm.

## Controls

| Input | Action |
| --- | --- |
| Remote `1`-`5`, `7` | Pick a mode |
| Remote `>>\|` / `\|<<` | Next / previous mode |
| Remote `0` or PLAY | Drums on or off |
| Remote EQ | Next drum pattern |
| Remote UP / DOWN | Tempo |
| Remote VOL+ / VOL- | Octave |
| Button tap | Octave (next song in Songs) |
| Button hold | Next mode |
| Hand right against the sensor for 1.5 s | Next mode |

In Songs: `|<<` and `>>|` pick a song, `1` or PLAY listens, `2` starts Learn, `7` or STOP exits.

## Wiring

| Part | Pins |
| --- | --- |
| LCD1602 | RS 7, E 8, D4 9, D5 10, D6 11, D7 12 |
| HC-SR04 | Trig 3, Echo 4 |
| Push button | 2 |
| IR receiver | 5 |
| Passive buzzer | 6 |

Needs the **IRremote** library by Armin Joachimsmeyer. The sketch puts IR decoding on Timer1 so it doesn't fight `tone()` for Timer2.

## Tuning

- **Echo timeout.** The clone HC-SR04 in the kit starts its echo late, so the sketch waits up to 30 ms for it. That keeps every key zone readable.
- **Stable notes.** Each key zone has ±2 cm of hysteresis, and readings go through a median-of-3 filter, so the note holds steady while your hand stays in one place.

## Archive

`archive/` has two earlier sketches from the same night:

- `desk_piano_v1_sound_sensor`: a tap-triggered version that uses a sound sensor taped to the desk. The final version plays when you put your hand down into the beam instead, so it needs no extra sensor.
- `hand_jump_dino`: an endless runner on the LCD that you play by waving your hand over the sensor.

MIT license.
