/*
  Duet — audio board firmware (Adafruit HUZZAH32 / ESP32 Feather)   v0.5

  Plays MP3s from a microSD card to EITHER:
    - the UDA1334A DAC's 3.5 mm headphone jack (I2S), or
    - Bluetooth headphones (A2DP source; original ESP32 only — not the S3/C3/C6)
  and takes commands from the Inkplate over I2C (see duet_link.h).

  Board:      "Adafruit ESP32 Feather" (esp32 core by Espressif, 2.x or 3.x)
  Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"  <- Bluetooth + MP3 needs the room
  Libraries:  arduino-audio-tools   https://github.com/pschatzmann/arduino-audio-tools
              arduino-libhelix      https://github.com/pschatzmann/arduino-libhelix
              ESP32-A2DP            https://github.com/pschatzmann/ESP32-A2DP

  Music card (FAT32):
    /music/NAME.mp3            tracks
    /music/ALBUM/NAME.mp3      tracks in folders (up to 3 levels deep)
    /bt.txt                    OPTIONAL: one line, the start of your headphones'
                               Bluetooth name. Only needed if Duet keeps pairing
                               with the wrong device.
  Tracks play in name order, folder by folder ("2 - x" sorts before "10 - x").
  Each folder is an album on the Inkplate's Music screen.
  Bluetooth needs 44.1 kHz stereo MP3s: other files play at the wrong speed
  there (they are fine on the headphone jack).

  Bluetooth pairing:
    The first time you switch to Bluetooth (or after "Pair new headphones"),
    Duet searches for headphones in pairing mode and connects to the first pair
    that is close by (about arm's length). After 45 s without one it accepts
    anything in the room. It remembers the pair and reconnects to it directly
    afterwards, retrying for as long as it is switched on.

  Buttons (to GND):  PREV = GPIO27, PLAY = GPIO32, NEXT = GPIO14
    PLAY short = play/pause, PLAY hold 2 s = switch jack <-> Bluetooth
    PREV/NEXT short = previous/next track, hold = volume down/up
    PREV restarts the track when it is more than 3 s in.
  The play/pause/next/previous buttons on Bluetooth headphones work too.

  The track, position, volume and output are remembered across restarts.
*/

#include <Wire.h>
#include <SD.h>
#include <Preferences.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/A2DPStream.h"
#include "duet_link.h"

// ---------- pins (must match the wiring sheet) ----------
const int PIN_I2S_BCLK = 26;  // A0 -> UDA1334A BCLK
const int PIN_I2S_WSEL = 25;  // A1 -> UDA1334A WSEL
const int PIN_I2S_DIN  = 4;   // A5 -> UDA1334A DIN
const int PIN_SD_CS    = 33;  // -> microSD CS   (SCK 5, MOSI 18, MISO 19 = Feather SPI)
const int PIN_I2C_SDA  = 23;  // -> Qwiic SDA (blue)
const int PIN_I2C_SCL  = 22;  // -> Qwiic SCL (yellow)
const int PIN_BTN_PREV = 27;
const int PIN_BTN_PLAY = 32;
const int PIN_BTN_NEXT = 14;

// ---------- limits ----------
const int MAX_TRACKS = 600;             // tracks indexed from the card
const uint32_t ARENA_MAX = 28 * 1024;   // bytes of RAM for their paths
const int MAX_DEPTH = 3;                // folder levels below /music
const int MAX_FOLDERS = 200;            // folders (albums) the Inkplate can browse
const int PAIR_RSSI_NEAR = -60;         // pairing: take headphones held this close straight away...
const int PAIR_RSSI_ROOM = -78;         // ...and anything in the room after PAIR_WIDEN_MS
const uint32_t PAIR_WIDEN_MS = 45000;
const char* MUSIC_DIR = "/music";

Preferences prefs;

// A chin key. (Declared up here because the Arduino IDE writes its own list of
// function prototypes above the first function, and they must know this type.)
struct Button {
  int pin;
  bool down = false;
  bool held = false;
  uint32_t since = 0;
  uint32_t lastRepeat = 0;
};

bool btMode = false;
bool haveMusic = false;
bool shuffleOn = false;
int volumeStep = 5;                     // 0..10
char title[DUET_TITLE_LEN] = "";

// =====================================================================
// Track index: every MP3 path under /music, sorted, held in RAM
// =====================================================================
char* arena = nullptr;                  // paths relative to /music, e.g. "Album/01 Song.mp3"
uint32_t arenaUsed = 0, arenaCap = 0;
uint32_t* trackOff = nullptr;           // offset of each track's path in the arena
volatile int trackCount = 0;            // set once the index is complete

bool isMp3(const char* name) {
  size_t n = strlen(name);
  return n > 4 && strcasecmp(name + n - 4, ".mp3") == 0;
}

// "Track 2" before "Track 10"; case-insensitive. Compares two name pieces.
int naturalCompare(const char* a, size_t an, const char* b, size_t bn) {
  size_t i = 0, j = 0;
  while (i < an && j < bn) {
    if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
      unsigned long x = 0, y = 0;
      while (i < an && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
      while (j < bn && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
      if (x != y) return x < y ? -1 : 1;
      continue;
    }
    int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[j]);
    if (ca != cb) return ca < cb ? -1 : 1;
    i++; j++;
  }
  if (i < an) return 1;
  if (j < bn) return -1;
  return 0;
}
// Folder by folder: a folder's own tracks first, then its sub-folders, so every
// folder is one unbroken run of tracks.
int comparePaths(const char* a, const char* b) {
  while (true) {
    const char* sa = strchr(a, '/');
    const char* sb = strchr(b, '/');
    if (!sa || !sb) {
      if (sa) return 1;                          // b is a track here; a is in a sub-folder
      if (sb) return -1;
      int c = naturalCompare(a, strlen(a), b, strlen(b));
      return c ? c : strcmp(a, b);
    }
    size_t an = sa - a, bn = sb - b;
    int c = naturalCompare(a, an, b, bn);
    if (!c) c = an != bn ? (an < bn ? -1 : 1) : memcmp(a, b, an);   // "01" and "1" are two folders
    if (c) return c;
    a = sa + 1; b = sb + 1;
  }
}
int compareTracks(const void* x, const void* y) {
  return comparePaths(arena + *(const uint32_t*)x, arena + *(const uint32_t*)y);
}

// Walks a folder. count-only when `store` is false.
void scanDir(const char* dirPath, const char* rel, int depth, bool store, int& n, uint32_t& bytes) {
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  while (true) {
    File f = dir.openNextFile();
    if (!f) break;
    const char* nm = f.name();
    const char* slash = strrchr(nm, '/');       // older cores return the full path
    if (slash) nm = slash + 1;
    bool isDir = f.isDirectory();
    char name[96];
    strncpy(name, nm, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    f.close();
    if (name[0] == '.' || name[0] == 0) continue;  // hidden files, macOS "._" resource forks

    char relPath[160];
    snprintf(relPath, sizeof(relPath), "%s%s%s", rel, rel[0] ? "/" : "", name);
    if (isDir) {
      if (depth < MAX_DEPTH) {
        char sub[176];
        snprintf(sub, sizeof(sub), "%s/%s", MUSIC_DIR, relPath);
        scanDir(sub, relPath, depth + 1, store, n, bytes);
      }
    } else if (isMp3(name)) {
      uint32_t len = strlen(relPath) + 1;
      if (n >= MAX_TRACKS || bytes + len > ARENA_MAX) continue;
      if (store) {
        trackOff[n] = arenaUsed;
        memcpy(arena + arenaUsed, relPath, len);
        arenaUsed += len;
      }
      n++;
      bytes += len;
    }
  }
  dir.close();
}

int buildIndex() {
  int n = 0;
  uint32_t bytes = 0;
  scanDir(MUSIC_DIR, "", 1, false, n, bytes);  // pass 1: how much room?
  if (n == 0) return 0;
  arena = (char*)malloc(bytes);
  trackOff = (uint32_t*)malloc(n * sizeof(uint32_t));
  if (!arena || !trackOff) return 0;
  arenaCap = bytes;
  int m = 0;
  uint32_t b2 = 0;
  arenaUsed = 0;
  scanDir(MUSIC_DIR, "", 1, true, m, b2);      // pass 2: store
  if (m > n) m = n;
  qsort(trackOff, m, sizeof(uint32_t), compareTracks);
  return m;
}

const char* trackPath(int i) { return arena + trackOff[i]; }
const char* baseName(const char* p) { const char* sl = strrchr(p, '/'); return sl ? sl + 1 : p; }

// ---------- folders (albums): each is a run of tracks in the sorted index ----------
uint16_t* folderFirst = nullptr;        // first track of each folder
volatile int folderCount = 0;

size_t dirLength(const char* p) { const char* sl = strrchr(p, '/'); return sl ? (size_t)(sl - p) : 0; }
bool sameFolder(int i, int j) {
  const char* a = trackPath(i);
  const char* b = trackPath(j);
  size_t n = dirLength(a);
  return n == dirLength(b) && memcmp(a, b, n) == 0;
}
void buildFolders(int n) {
  folderFirst = (uint16_t*)malloc(MAX_FOLDERS * sizeof(uint16_t));
  if (!folderFirst) return;
  int count = 0;
  for (int i = 0; i < n && count < MAX_FOLDERS; i++)
    if (i == 0 || !sameFolder(i, i - 1)) folderFirst[count++] = i;   // past MAX_FOLDERS the last one takes the rest
  folderCount = count;
}
int folderOf(int track) {
  int f = 0;
  while (f + 1 < folderCount && folderFirst[f + 1] <= track) f++;
  return f;
}
int folderEnd(int f) { return f + 1 < folderCount ? folderFirst[f + 1] : (int)trackCount; }
// "Artist/Album" -> "Album"; empty for tracks that sit directly in /music
void folderName(int f, char* out, size_t cap) {
  const char* p = trackPath(folderFirst[f]);
  size_t n = dirLength(p), start = 0;
  for (size_t k = 0; k < n; k++) if (p[k] == '/') start = k + 1;
  size_t len = n - start;
  bool cut = len > cap - 1;
  if (cut) len = cap - 4;
  memcpy(out, p + start, len);
  strcpy(out + len, cut ? "..." : "");
}

// "Folder/Name" without ".mp3", cut to fit `cap`. A long path loses its folders first.
void trackDisplayName(int i, char* out, size_t cap) {
  const char* p = trackPath(i);
  size_t len = strlen(p);
  if (len >= 4) len -= 4;                        // drop ".mp3"
  if (len > cap - 1) {
    const char* base = strrchr(p, '/');
    if (base) { len -= (base + 1 - p); p = base + 1; }
    if (len > cap - 1) len = cap - 1;
  }
  memcpy(out, p, len);
  out[len] = 0;
}

// =====================================================================
// Audio source: hands the player one file at a time from the index
// =====================================================================
uint32_t resumePos = 0;                 // byte offset to continue from (set once at boot)
int resumeIndex = -1;

// Shuffle plays every track once in a random order, so "previous" really goes back
// and nothing repeats until the whole card has played.
uint16_t* playOrder = nullptr;
int playPos = 0;                        // where we are in playOrder

void makeShuffle(int current) {         // a new order that starts with the current track
  if (!playOrder) return;
  int n = trackCount;
  for (int i = 0; i < n; i++) playOrder[i] = i;
  for (int i = n - 1; i > 0; i--) {
    int j = random(i + 1);
    uint16_t t = playOrder[i]; playOrder[i] = playOrder[j]; playOrder[j] = t;
  }
  for (int i = 0; i < n; i++)
    if (playOrder[i] == current) { playOrder[i] = playOrder[0]; playOrder[0] = current; break; }
  playPos = 0;
}

class DuetSource : public AudioSource {
 public:
  bool begin() override { return trackCount > 0; }

  Stream* nextStream(int offset) override {
    if (trackCount == 0) return nullptr;
    int n = trackCount, next;
    if (shuffleOn && playOrder && n > 2) {
      playPos = ((playPos + offset) % n + n) % n;
      next = playOrder[playPos];
    } else {
      next = ((cur + offset) % n + n) % n;       // wraps: repeat all
    }
    return open(next);
  }
  Stream* selectStream(int index) override {
    if (trackCount == 0) return nullptr;
    index = constrain(index, 0, trackCount - 1);
    if (shuffleOn && playOrder)                   // a track picked by hand: carry on from its place in the order
      for (int k = 0; k < trackCount; k++) if (playOrder[k] == index) { playPos = k; break; }
    return open(index);
  }
  Stream* selectStream(const char* path) override { return nullptr; }
  int index() override { return cur; }
  const char* toStr() override { return cur >= 0 ? trackPath(cur) : nullptr; }

  uint32_t position() { return file ? file.position() : 0; }
  uint32_t size() { return fileSize; }
  uint32_t audioStart = 0;              // first byte after the ID3v2 tag

 protected:
  File file;
  int cur = -1;
  uint32_t fileSize = 0;

  Stream* open(int i);
};
DuetSource source;

MP3DecoderHelix decoder;
I2SStream i2s;
A2DPStream a2dp;
AudioPlayer player(source, i2s, decoder);  // output is swapped to a2dp in Bluetooth mode

// ---------- playback position / time ----------
uint32_t elapsedMs = 0;                 // audio time played in this track
uint32_t baseMs = 0, basePos = 0;       // reference point for the duration estimate
uint16_t durationS = 0;
bool trackChanged = false;

uint16_t toU16(uint64_t v) { return v > 65535 ? 65535 : (uint16_t)v; }

// "07 - Song" -> where "Song" starts; null when the name has no track number
const char* afterTrackNumber(const char* name) {
  const char* p = name;
  while (isdigit((unsigned char)*p) && p - name < 3) p++;
  if (p == name || isdigit((unsigned char)*p)) return nullptr;    // no number, or a year
  const char* q = p;
  while (*q == ' ' || *q == '.' || *q == '_' || *q == '-') q++;
  return (q == p || !*q) ? nullptr : q;
}

// The title when the file has no ID3 tag: its name without ".mp3", and without the
// track number when the folder's files are numbered ("99 Luftballons" keeps its 99).
void setTitleFromIndex(int i) {
  char name[96];
  strncpy(name, baseName(trackPath(i)), sizeof(name) - 1);
  name[sizeof(name) - 1] = 0;
  size_t len = strlen(name);
  if (len > 4 && isMp3(name)) name[len - 4] = 0;
  const char* shown = name;
  const char* rest = afterTrackNumber(name);
  if (rest) {
    bool numbered = name[0] == '0';
    if (!numbered && i > 0 && sameFolder(i, i - 1)) numbered = afterTrackNumber(baseName(trackPath(i - 1))) != nullptr;
    if (!numbered && i + 1 < trackCount && sameFolder(i, i + 1)) numbered = afterTrackNumber(baseName(trackPath(i + 1))) != nullptr;
    if (numbered) shown = rest;
  }
  strncpy(title, shown, DUET_TITLE_LEN - 1);
  title[DUET_TITLE_LEN - 1] = 0;
}

Stream* DuetSource::open(int i) {
  if (file) file.close();
  char path[200];
  snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, trackPath(i));
  file = SD.open(path);
  cur = i;
  fileSize = file ? file.size() : 0;
  audioStart = 0;
  elapsedMs = 0;
  durationS = 0;
  setTitleFromIndex(i);
  trackChanged = true;
  if (!file) { resumeIndex = -1; return nullptr; }   // card pulled? the player skips on after a timeout

  // Size of the ID3v2 tag (album art can be hundreds of kB): progress ignores it
  uint8_t h[10];
  if (file.read(h, 10) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
    audioStart = 10 + (((uint32_t)h[6] & 0x7F) << 21 | ((uint32_t)h[7] & 0x7F) << 14 |
                       ((uint32_t)h[8] & 0x7F) << 7 | ((uint32_t)h[9] & 0x7F));
    if (audioStart >= fileSize) audioStart = 0;
  }
  uint32_t startAt = 0;
  if (i == resumeIndex && resumePos > audioStart && resumePos < fileSize) {
    startAt = resumePos;                   // continue where we stopped
    elapsedMs = prefs.getUInt("ela", 0) * 1000UL;
    trackChanged = false;                  // keep the saved position as it is
    char saved[DUET_TITLE_LEN];            // the ID3 tag is at the start of the file: we are past it
    if (prefs.getString("ttl", saved, sizeof(saved)) > 0 && saved[0]) strcpy(title, saved);
  }
  resumeIndex = -1;                        // only once
  file.seek(startAt);
  baseMs = elapsedMs;
  basePos = max(startAt, audioStart);
  return &file;
}

// ID3 title wins over the file name, if it is plain text
void onMetadata(MetaDataType type, const char* str, int len) {
  if (type != Title || len <= 0) return;
  int n = min(len, DUET_TITLE_LEN - 1);
  for (int k = 0; k < n; k++) if ((uint8_t)str[k] < 0x20) return;   // UTF-16 or binary: keep the file name
  memcpy(title, str, n);
  title[n] = 0;
}

void updateProgress(bool playing, uint32_t dtMs) {
  if (playing) elapsedMs += dtMs;
  uint32_t pos = source.position(), size = source.size();
  if (size == 0 || pos <= basePos) return;
  uint32_t played = elapsedMs - baseMs;
  if (played >= 10000) {
    // audio time per byte so far, applied to the rest of the file
    uint64_t rest = (uint64_t)(size - basePos) * played / (pos - basePos);
    durationS = toU16((baseMs + rest) / 1000);
  } else if (durationS == 0) {
    int br = decoder.audioInfoEx().bitrate;                          // first guess: constant bitrate
    if (br > 8000) durationS = toU16((uint64_t)(size - source.audioStart) * 8 / br);
  }
}

// =====================================================================
// Saved state (NVS)
// =====================================================================
uint32_t volSaveAt = 0;                 // millis() when the volume should be written (0 = clean)

void saveResume() {
  if (!haveMusic) return;
  prefs.putInt("idx", source.index());
  prefs.putUInt("pos", source.position());
  prefs.putUInt("ela", elapsedMs / 1000);
  prefs.putString("ttl", title);
  prefs.putBool("play", player.isActive());
}

void applyVolume() {
  volumeStep = constrain(volumeStep, 0, 10);
  float v = volumeStep / 10.0f;
  player.setVolume(v * v);              // squared: even-sounding steps, fine control when quiet
  volSaveAt = millis() + 2000;          // write once the key is let go, not on every repeat
  if (volSaveAt == 0) volSaveAt = 1;
}

void restart() {
  saveResume();
  if (volSaveAt) prefs.putInt("vol", volumeStep);
  player.end();
  delay(100);
  ESP.restart();                        // cleanest way to swap between the I2S and Bluetooth stacks
}

// =====================================================================
// Bluetooth: pairing, reconnecting, and knowing when the headphones take audio
// =====================================================================
char btName[DUET_TEXT_LEN] = "";        // remembered headphones (or the name being paired)
char btWanted[48] = "";                 // from /bt.txt: only pair with a name starting like this
uint8_t btAddr[6] = {0};
bool btKnown = false;                   // we have remembered headphones
bool btPairingSession = false;          // this boot started by searching rather than reconnecting
volatile bool btCandidate = false;      // the search picked a device (name/address below)
char candName[DUET_TEXT_LEN] = "";
uint8_t candAddr[6] = {0};
uint32_t btSearchSince = 0;             // millis() when Bluetooth started
uint32_t btConnectedAt = 0;             // millis() when the headphones connected (0 = not connected)
volatile bool btAudioOn = false;        // the headphones are taking audio (A2DP media has started)
volatile bool btAudioSeen = false;      // ...and the library has reported an audio state at least once

void readBluetoothName() {
  File f = SD.open("/bt.txt");
  if (!f) return;
  String s = f.readStringUntil('\n');
  s.trim();
  f.close();
  if (s.length() > 0) { strncpy(btWanted, s.c_str(), sizeof(btWanted) - 1); }
}

// Called by the Bluetooth stack for every audio device its search finds.
// Return true to connect to it.
bool onBluetoothDevice(const char* name, esp_bd_addr_t address, int rssi) {
  if (btCandidate) return false;                       // already chose one
  bool take;
  if (btKnown) take = memcmp(address, btAddr, 6) == 0; // only our own headphones
  else if (btWanted[0]) take = name && strncasecmp(name, btWanted, strlen(btWanted)) == 0;
  else {
    // Headphones held close are taken at once. A TV or speaker across the room is
    // also "an audio device in pairing mode", so those only count after a while.
    bool widened = millis() - btSearchSince > PAIR_WIDEN_MS;
    take = rssi >= (widened ? PAIR_RSSI_ROOM : PAIR_RSSI_NEAR);
  }
  if (take) {
    strncpy(candName, (name && name[0]) ? name : "Headphones", sizeof(candName) - 1);
    memcpy(candAddr, address, 6);
    btCandidate = true;
  }
  return take;
}

// Called by the Bluetooth stack when the audio stream to the headphones starts or stops
void onBluetoothAudio(esp_a2d_audio_state_t state, void*) {
  btAudioSeen = true;
  btAudioOn = state == ESP_A2D_AUDIO_STATE_STARTED;
}

// Connected AND taking audio. The stream starts a few seconds after the connection;
// audio written before that just fills a buffer and then blocks loop(), keys included.
bool btReady() {
  if (!a2dp.isConnected()) return false;
  if (btAudioOn) return true;
  // a library version that never reports the audio state: go by time instead
  return !btAudioSeen && btConnectedAt && millis() - btConnectedAt > 15000;
}

void postCommand(uint8_t cmd, uint16_t arg);

// Buttons on the headphones (AVRCP). Runs in the Bluetooth task.
void onHeadphoneButton(uint8_t key, bool released) {
  if (released) return;
  switch (key) {
    case 0x44: case 0x46: postCommand(CMD_PLAY_PAUSE, key); break;  // PLAY, PAUSE
    case 0x4B: postCommand(CMD_NEXT, 0); break;                     // FORWARD
    case 0x4C: postCommand(CMD_PREV, 0); break;                     // BACKWARD
  }
}

void startBluetooth() {
  btKnown = prefs.getBytes("btaddr", btAddr, 6) == 6;
  if (btKnown) prefs.getString("btname", btName, sizeof(btName));
  btPairingSession = !btKnown;
  btSearchSince = millis();

  BluetoothA2DPSource& src = a2dp.source();
  src.set_local_name("Duet");
  src.set_ssid_callback(onBluetoothDevice);
  src.set_avrc_passthru_command_callback(onHeadphoneButton);
  src.set_on_audio_state_changed(onBluetoothAudio);
  if (btKnown) src.set_auto_reconnect(btAddr);   // connect straight to the remembered address

  auto cfg = a2dp.defaultConfig(TX_MODE);
  cfg.name = "Duet";                    // not used for matching: onBluetoothDevice decides
  cfg.silence_on_nodata = true;         // start the stream as soon as the headphones connect
  cfg.wait_for_connection = false;      // don't block; we check btReady()
  cfg.auto_reconnect = btKnown;         // keeps retrying the remembered headphones (every ~10 s)
  cfg.delay_ms = 0;                     // no pause after each write: pausing writes 1000 tiny ones
  cfg.tx_write_timeout_ms = 1000;       // never let a stalled stream freeze loop() for good
  a2dp.begin(cfg);
  player.setOutput(a2dp);
  player.setDelayIfOutputFull(0);       // a2dp.write() waits for room by itself
}

// Called from loop(): remember newly paired headphones; recover from a dropped link
void serviceBluetooth() {
  static bool wasConnected = false;
  static uint32_t candidateSince = 0;
  bool connected = a2dp.isConnected();
  if (connected && !wasConnected) {
    btConnectedAt = millis() | 1;
    if (btCandidate && !btKnown) {
      memcpy(btAddr, candAddr, 6);
      strncpy(btName, candName, sizeof(btName) - 1);
      prefs.putBytes("btaddr", btAddr, 6);
      prefs.putString("btname", btName);
      btKnown = true;
    }
  }
  if (!connected && wasConnected) {
    btConnectedAt = 0;
    btAudioOn = false;
    saveResume();
    // A session that began by searching can't reconnect by address. Restart into
    // reconnect mode: it then retries the remembered headphones until they answer.
    if (btPairingSession && btKnown) restart();
    btCandidate = false;
  }
  // The search chose a device but the connection never came up (it left pairing
  // mode, or refused). The Bluetooth library does not go back to searching after
  // that, so start again from the top.
  if (btCandidate && !connected) {
    if (candidateSince == 0) candidateSince = millis() | 1;
    else if (millis() - candidateSince > 25000) restart();
  } else candidateSince = 0;
  wasConnected = connected;
}

// =====================================================================
// Commands: from I2C, the chin keys and the headphones, run in loop()
// =====================================================================
portMUX_TYPE linkMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t cmdQueue[8];          // cmd | arg << 8
volatile uint8_t cmdHead = 0, cmdTail = 0;

void postCommand(uint8_t cmd, uint16_t arg) {
  portENTER_CRITICAL(&linkMux);
  uint8_t next = (cmdHead + 1) & 7;
  if (next != cmdTail) { cmdQueue[cmdHead] = cmd | ((uint32_t)arg << 8); cmdHead = next; }
  portEXIT_CRITICAL(&linkMux);
}

bool takeCommand(uint8_t& cmd, uint16_t& arg) {
  bool got = false;
  portENTER_CRITICAL(&linkMux);
  if (cmdTail != cmdHead) {
    uint32_t v = cmdQueue[cmdTail];
    cmdTail = (cmdTail + 1) & 7;
    cmd = v & 0xFF; arg = v >> 8; got = true;
  }
  portEXIT_CRITICAL(&linkMux);
  return got;
}

void handleCommand(uint8_t cmd, uint16_t arg) {
  switch (cmd) {
    case CMD_PLAY_PAUSE:
      if (!haveMusic) break;
      // from the headphones: 0x44 = play, 0x46 = pause; otherwise toggle
      if (arg == 0x44 && player.isActive()) break;
      if (arg == 0x46 && !player.isActive()) break;
      player.setActive(!player.isActive());
      saveResume();
      break;
    case CMD_NEXT: if (haveMusic) player.next(); break;
    case CMD_PREV:
      if (!haveMusic) break;
      if (elapsedMs > 3000) player.setIndex(source.index());   // back to the start of this track
      else player.previous();
      break;
    case CMD_PLAY_INDEX: if (haveMusic && arg < trackCount) player.setIndex(arg); break;
    case CMD_VOL_UP:     volumeStep++; applyVolume(); break;
    case CMD_VOL_DOWN:   volumeStep--; applyVolume(); break;
    case CMD_SHUFFLE:
      if ((arg != 0) == shuffleOn) break;
      shuffleOn = arg != 0;
      if (shuffleOn && haveMusic) makeShuffle(source.index());
      prefs.putBool("shuf", shuffleOn);
      break;
    case CMD_TOGGLE_OUTPUT: prefs.putBool("bt", !btMode); restart(); break;
    case CMD_SET_OUTPUT:
      if ((arg != 0) != btMode) { prefs.putBool("bt", arg != 0); restart(); }
      break;
    case CMD_BT_PAIR:                   // forget the headphones, then search again
      prefs.remove("btaddr");
      prefs.remove("btname");
      prefs.putBool("bt", true);
      restart();
      break;
  }
}

// =====================================================================
// I2C link (callbacks run in the I2C driver's task: RAM only, no SD, no NVS)
//
// On an original ESP32 the driver calls onRequest AFTER a read has finished, not
// when it starts, so a reply written there is only sent by the NEXT read. Every
// reply is therefore prepared when the command arrives (onI2CReceive) and handed
// to the driver at once; the Inkplate reads it a few milliseconds later.
// =====================================================================
DuetStatus status;                      // the latest status, checksum included
uint8_t nextReply[DUET_REPLY_LEN];      // what the next read returns
bool starting = true;                   // setup() is still reading the card
bool haveSource = false;                // source.index() is valid

void onI2CReceive(int n) {
  uint8_t b[3] = {0, 0, 0};
  int got = 0;
  while (Wire.available()) { uint8_t c = Wire.read(); if (got < 3) b[got++] = c; }
  if (got == 0) return;
  uint16_t arg = b[1] | (b[2] << 8);
  uint8_t out[DUET_REPLY_LEN] = {0};

  if (b[0] == CMD_GET_TRACK || b[0] == CMD_GET_BT_NAME) {
    DuetText t = {};
    t.magic = DUET_MAGIC_TEXT;
    t.kind = b[0];
    t.index = arg;
    if (b[0] == CMD_GET_TRACK) { if (arg < trackCount) trackDisplayName(arg, t.text, sizeof(t.text)); }
    else strncpy(t.text, btKnown ? btName : (btCandidate ? candName : ""), sizeof(t.text) - 1);  // paired, or being paired
    memcpy(out, &t, sizeof(out));
  } else if (b[0] == CMD_GET_FOLDER) {
    DuetFolder f = {};
    f.magic = DUET_MAGIC_FOLDER;
    f.kind = b[0];
    f.index = arg;
    f.folders = folderCount;
    int k = arg == DUET_FOLDER_CURRENT ? (haveSource ? folderOf(source.index()) : 0) : arg;
    if (k < folderCount) {
      f.folder = k;
      f.first = folderFirst[k];
      f.count = folderEnd(k) - folderFirst[k];
      folderName(k, f.text, sizeof(f.text));
    }
    memcpy(out, &f, sizeof(out));
  } else {                              // a command (or CMD_GET_STATUS): answered with the status
    if (b[0] >= CMD_PLAY_PAUSE && b[0] <= CMD_SHUFFLE) postCommand(b[0], arg);
    portENTER_CRITICAL(&linkMux);
    memcpy(out, &status, sizeof(out));
    portEXIT_CRITICAL(&linkMux);
  }
  out[DUET_REPLY_LEN - 1] = duetCheck(out);
  portENTER_CRITICAL(&linkMux);
  memcpy(nextReply, out, sizeof(out));
  portEXIT_CRITICAL(&linkMux);
#if CONFIG_IDF_TARGET_ESP32
  Wire.slaveWrite(out, sizeof(out));    // into the transmit buffer now, ready for the read
#endif
}

void onI2CRequest() {
  uint8_t out[DUET_REPLY_LEN];
  portENTER_CRITICAL(&linkMux);
#if CONFIG_IDF_TARGET_ESP32
  memcpy(out, &status, sizeof(out));    // a read has just finished: leave a status for a bare read
#else
  memcpy(out, nextReply, sizeof(out));  // chips that call this when the read starts
#endif
  portEXIT_CRITICAL(&linkMux);
  Wire.write(out, sizeof(out));
}

void publishStatus() {
  DuetStatus s = {};
  s.magic = DUET_MAGIC_STATUS;
  bool linked = btMode && a2dp.isConnected();
  bool ready = btMode && btReady();     // "connected" for the Inkplate means music can play
  if (haveMusic && player.isActive()) s.flags |= FLAG_PLAYING;
  if (btMode) s.flags |= FLAG_BT_MODE;
  if (ready) s.flags |= FLAG_BT_CONNECTED;
  if (starting) s.flags |= FLAG_STARTING;
  else if (!haveMusic) s.flags |= FLAG_NO_CARD;
  if (btMode && !btKnown && !linked) s.flags |= FLAG_BT_PAIRING;
  if (btKnown) s.flags |= FLAG_BT_KNOWN;
  if (shuffleOn) s.flags |= FLAG_SHUFFLE;
  s.volume = volumeStep;
  s.trackCount = trackCount;
  if (haveMusic) {
    s.track = source.index();
    uint32_t pos = source.position(), size = source.size(), a0 = source.audioStart;
    if (size > a0 && pos > a0) {
      uint64_t p = (uint64_t)(pos - a0) * 255 / (size - a0);
      s.progress = p > 255 ? 255 : (uint8_t)p;
    }
    s.elapsed = toU16(elapsedMs / 1000);
    s.duration = durationS;
  }
  strncpy(s.title, title, DUET_TITLE_LEN - 1);
  s.check = duetCheck((const uint8_t*)&s);
  portENTER_CRITICAL(&linkMux);
  status = s;
  portEXIT_CRITICAL(&linkMux);
}

// =====================================================================
// Chin keys: short press + hold
// =====================================================================
Button bPrev{PIN_BTN_PREV}, bPlay{PIN_BTN_PLAY}, bNext{PIN_BTN_NEXT};

// returns 1 = short press (on release), 2 = hold event, 0 = nothing
int pollButton(Button& b, uint32_t holdMs, bool repeat) {
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (pressed && !b.down) { b.down = true; b.held = false; b.since = now; return 0; }
  if (!pressed && b.down) {
    b.down = false;
    if (!b.held && now - b.since > 30) return 1;  // 30 ms debounce
    return 0;
  }
  if (pressed && now - b.since > holdMs) {
    if (!b.held) { b.held = true; b.lastRepeat = now; return 2; }
    if (repeat && now - b.lastRepeat > 250) { b.lastRepeat = now; return 2; }
  }
  return 0;
}

void pollButtons() {
  int p = pollButton(bPrev, 600, true);
  if (p == 1) handleCommand(CMD_PREV, 0);
  if (p == 2) handleCommand(CMD_VOL_DOWN, 0);

  int n = pollButton(bNext, 600, true);
  if (n == 1) handleCommand(CMD_NEXT, 0);
  if (n == 2) handleCommand(CMD_VOL_UP, 0);

  int y = pollButton(bPlay, 2000, false);
  if (y == 1) handleCommand(CMD_PLAY_PAUSE, 0);
  if (y == 2) handleCommand(CMD_TOGGLE_OUTPUT, 0);
}

// =====================================================================
// setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);

  prefs.begin("duet", false);
  btMode = prefs.getBool("bt", false);
  volumeStep = constrain(prefs.getInt("vol", 5), 0, 10);
  shuffleOn = prefs.getBool("shuf", false);
  randomSeed(esp_random());

  // I2C slave first, so the Inkplate gets answers (FLAG_STARTING) while we read the card
  strncpy(title, "Starting...", DUET_TITLE_LEN - 1);
  publishStatus();
  memcpy(nextReply, &status, sizeof(nextReply));
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);
  Wire.begin((uint8_t)DUET_AUDIO_ADDR, PIN_I2C_SDA, PIN_I2C_SCL, 100000);

  int found = 0;
  if (SD.begin(PIN_SD_CS)) {
    readBluetoothName();
    found = buildIndex();
  }
  trackCount = found;
  if (found > 0) {
    buildFolders(found);
    playOrder = (uint16_t*)malloc(found * sizeof(uint16_t));
  }

  if (btMode) {
    startBluetooth();
  } else {
    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.pin_bck = PIN_I2S_BCLK;
    cfg.pin_ws = PIN_I2S_WSEL;
    cfg.pin_data = PIN_I2S_DIN;
    i2s.begin(cfg);
    player.setSilenceOnInactive(true);   // feed the DAC zeros while paused: no buzz
  }

  // ID3v2 only: the old ID3v1 tag sits at the END of a file and would swap in a
  // 30-character title just as the track finishes
  player.setMetadataCallback(onMetadata, SELECT_ID3V2);
  float v = volumeStep / 10.0f;
  player.setVolume(v * v);

  // Continue where we stopped. After a restart of our own (output switch, pairing)
  // keep the play/pause state; after power-on, start playing.
  int idx = constrain(prefs.getInt("idx", 0), 0, max(found - 1, 0));
  bool wasPlaying = prefs.getBool("play", true);
  bool ownRestart = esp_reset_reason() == ESP_RST_SW;
  resumeIndex = idx;
  resumePos = prefs.getUInt("pos", 0);
  if (shuffleOn && found > 0) makeShuffle(idx);
  haveMusic = found > 0 && player.begin(idx, ownRestart ? wasPlaying : true);
  haveSource = haveMusic;
  if (!haveMusic) strncpy(title, "No music card", DUET_TITLE_LEN - 1);
  starting = false;
  publishStatus();
}

void loop() {
  uint8_t cmd; uint16_t arg;
  while (takeCommand(cmd, arg)) handleCommand(cmd, arg);
  pollButtons();

  // In Bluetooth mode only feed audio once the headphones are taking it
  bool btOk = btMode && btReady();
  bool canPlay = haveMusic && (!btMode || btOk);
  if (canPlay && (!btMode || player.isActive())) {
    player.copy();
  } else if (btOk) {
    // Paused (or no card): keep the stream fed with silence. An empty stream stalls
    // the Bluetooth side, and some headphones switch themselves off.
    static const uint8_t silence[1024] = {0};
    a2dp.write(silence, sizeof(silence));
  } else delay(5);

  static uint32_t lastTick = 0;
  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;
  if (haveMusic) updateProgress(canPlay && player.isActive(), dt);

  if (trackChanged) {                  // remember the track; position restarts at 0
    trackChanged = false;
    prefs.putInt("idx", source.index());
    prefs.putUInt("pos", 0);
    prefs.putUInt("ela", 0);
  }

  static uint32_t lastPublish = 0, lastSave = 0;
  if (now - lastPublish > 200) {
    lastPublish = now;
    if (btMode) serviceBluetooth();
    publishStatus();
    if (volSaveAt && (int32_t)(now - volSaveAt) >= 0) { volSaveAt = 0; prefs.putInt("vol", volumeStep); }
    if (canPlay && player.isActive() && now - lastSave > 30000) { lastSave = now; saveResume(); }
  }
}
