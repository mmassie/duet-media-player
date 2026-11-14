// duet_link.h — the I2C protocol between the Inkplate (reader) and the
// HUZZAH32 (audio board). Keep this file identical in both sketches.
#pragma once
#include <stdint.h>

// The audio board listens as an I2C slave at this address on the Qwiic bus.
// The Inkplate's own expanders sit at 0x20-0x22 and its RTC at 0x51, so 0x42 is free.
#define DUET_AUDIO_ADDR 0x42

// Every exchange is a request and a reply: the Inkplate writes one command byte
// and 0-2 argument bytes, waits a few ms, then reads DUET_REPLY_LEN bytes. The
// audio board prepares the reply as soon as the command arrives. (An original
// ESP32 cannot hold the bus while it thinks, so the reply has to be waiting in
// its transmit buffer before the Inkplate starts to read.)
enum DuetCmd : uint8_t {
  CMD_NONE = 0,
  CMD_PLAY_PAUSE = 1,
  CMD_NEXT = 2,
  CMD_PREV = 3,           // restarts the track if more than 3 s in, else previous track
  CMD_VOL_UP = 4,
  CMD_VOL_DOWN = 5,
  CMD_TOGGLE_OUTPUT = 6,  // jack <-> Bluetooth (audio board restarts, ~2 s, and resumes)
  CMD_PLAY_INDEX = 7,     // + uint16 track index (little-endian): play that track
  CMD_SET_OUTPUT = 8,     // + 0 = headphone jack, 1 = Bluetooth
  CMD_BT_PAIR = 9,        // forget the remembered headphones and pair with new ones
  CMD_SHUFFLE = 10,       // + 0 = off, 1 = on
  CMD_GET_TRACK = 11,     // + uint16 track index: the next read returns its name (DuetText)
  CMD_GET_BT_NAME = 12,   // the next read returns the headphones' name (DuetText)
  CMD_GET_FOLDER = 13,    // + uint16 folder index, or DUET_FOLDER_CURRENT for the folder of
                          // the current track: the next read returns a DuetFolder
  CMD_GET_STATUS = 14,    // the next read returns a fresh DuetStatus
};
#define DUET_FOLDER_CURRENT 0xFFFF

// Every read returns DUET_REPLY_LEN bytes: a DuetStatus (after any command that is
// not a GET_ request), a DuetText or a DuetFolder. The first byte says which; the
// last byte is a checksum (duetCheck) so a garbled or half-updated reply is ignored.
#define DUET_REPLY_LEN 48
#define DUET_MAGIC_STATUS 0xF2
#define DUET_MAGIC_TEXT 0xF3
#define DUET_MAGIC_FOLDER 0xF4
#define DUET_TITLE_LEN 35
#define DUET_TEXT_LEN 43
#define DUET_FOLDER_LEN 35

#define FLAG_PLAYING 0x01
#define FLAG_BT_MODE 0x02       // output is Bluetooth (otherwise headphone jack)
#define FLAG_BT_CONNECTED 0x04  // headphones are connected (BT mode only)
#define FLAG_NO_CARD 0x08       // no music card / no MP3s found
#define FLAG_BT_PAIRING 0x10    // searching for headphones in pairing mode
#define FLAG_SHUFFLE 0x20
#define FLAG_BT_KNOWN 0x40      // headphones are remembered (their name: CMD_GET_BT_NAME)
#define FLAG_STARTING 0x80      // just switched on or restarted: still reading the card

struct __attribute__((packed)) DuetStatus {
  uint8_t magic;        // DUET_MAGIC_STATUS
  uint8_t flags;        // FLAG_*
  uint8_t volume;       // 0..10
  uint8_t progress;     // 0..255 through the current track
  uint16_t track;       // index of the current track, 0-based
  uint16_t trackCount;  // tracks on the card
  uint16_t elapsed;     // seconds into the track
  uint16_t duration;    // seconds, estimated; 0 = not known yet
  char title[DUET_TITLE_LEN];  // null-terminated; ID3 title or file name
  uint8_t check;        // duetCheck()
};

struct __attribute__((packed)) DuetText {
  uint8_t magic;        // DUET_MAGIC_TEXT
  uint8_t kind;         // the command this answers
  uint16_t index;       // track index (CMD_GET_TRACK)
  char text[DUET_TEXT_LEN];  // null-terminated. Track: "Folder/Name" without ".mp3"
  uint8_t check;
};

// A folder of tracks (an album). Tracks are numbered folder by folder, so a
// folder is the run of tracks first .. first + count - 1.
struct __attribute__((packed)) DuetFolder {
  uint8_t magic;        // DUET_MAGIC_FOLDER
  uint8_t kind;         // CMD_GET_FOLDER
  uint16_t index;       // the index that was asked for (may be DUET_FOLDER_CURRENT)
  uint16_t folder;      // this folder's index, 0-based
  uint16_t first;       // its first track
  uint16_t count;       // how many tracks it holds
  uint16_t folders;     // folders on the card
  char text[DUET_FOLDER_LEN];  // its name, null-terminated; empty = tracks not in a folder
  uint8_t check;
};

// Checksum over the first DUET_REPLY_LEN - 1 bytes of a reply. An all-zero or
// all-0xFF buffer (nothing on the bus) never passes.
static inline uint8_t duetCheck(const uint8_t* reply) {
  uint8_t sum = 0x5A;
  for (int i = 0; i < DUET_REPLY_LEN - 1; i++) sum = (uint8_t)(sum + reply[i]);
  return sum;
}

#if defined(__cplusplus)
static_assert(sizeof(DuetStatus) == DUET_REPLY_LEN, "DuetStatus size");
static_assert(sizeof(DuetText) == DUET_REPLY_LEN, "DuetText size");
static_assert(sizeof(DuetFolder) == DUET_REPLY_LEN, "DuetFolder size");
#endif
