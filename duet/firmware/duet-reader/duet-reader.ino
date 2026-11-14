/*
  Duet — reader firmware (Soldered Inkplate 6)   v0.5

  An e-book reader (EPUB and plain text) that also shows and controls the music
  playing on the audio board (HUZZAH32) over the Qwiic I2C cable. See duet_link.h.

  Board:      Inkplate Boards -> "Soldered Inkplate 6" (Inkplate board package)
  Library:    InkplateLibrary (Library Manager)

  Book card layout (FAT32):
    /books/NAME.epub     EPUB books (DRM-free)
    /books/NAME.txt      plain-text books (e.g. Project Gutenberg "Plain Text UTF-8")
    /books/.duet         created automatically: last book + position
    /books/.settings     created automatically: sleep time, text size, refresh
    /books/.cache/       created automatically: converted EPUB text, page indexes and
                         each book's position. Safe to delete; it is rebuilt.

  The first time a book is opened the reader prepares it (EPUB -> text, then a
  page index). That takes a few seconds with a progress screen; afterwards the
  book opens instantly and turning back a page is as fast as turning forward.

  Side keys (to GND) on IO expander 2 pads:  L = P1-0 (B0), R = P1-1 (B1)
    Reading     L / R = page back / forward      hold L = Library    hold R = Now playing
    Lists       L / R = move up / down           hold R = open / change
                                                 hold L = back one level
    Now playing L / R = previous / next track    hold L = albums and tracks
    Anywhere    hold L + R = Home
  A tap made while the screen is still redrawing is not lost: it is acted on next.
  The three chin keys belong to the audio board and always run the music.
  Inkplate's own WAKE button wakes it from sleep.
*/

#include "Inkplate.h"
#include "Fonts/FreeSerif9pt7b.h"
#include "Fonts/FreeSerifBold9pt7b.h"
#include "Fonts/FreeSerif12pt7b.h"
#include "Fonts/FreeSerifBold12pt7b.h"
#include "Fonts/FreeSerif18pt7b.h"
#include "Fonts/FreeSerifBold18pt7b.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/FreeSansBold9pt7b.h"
#include "Fonts/FreeSans12pt7b.h"
#include "Fonts/FreeSansBold12pt7b.h"
#include "Fonts/FreeSansBold18pt7b.h"
#include "Fonts/FreeSansBold24pt7b.h"
#include "duet_link.h"
#include "duet_logo.h"
#include "src/epub/epub.h"

#if !defined(ARDUINO_INKPLATE6V2) && !defined(ARDUINO_INKPLATE6)
#error "Select Soldered Inkplate 6 in Tools -> Board"
#endif

Inkplate display(INKPLATE_1BIT);

const char* DUET_VERSION = "0.5";

// ---------- fixed settings ----------
const uint8_t ROTATION = 1;           // portrait, matches the Duet case (3 = upside-down)
const uint8_t BTN_BACK = IO_PIN_B0;   // L key: expander 2, P1-0
const uint8_t BTN_FWD  = IO_PIN_B1;   // R key: expander 2, P1-1
const uint32_t HOLD_MS = 600;         // a press longer than this is a "hold"
// The panel takes 0.3-2 s to redraw and the keys can't be read meanwhile. The IO
// expander remembers a key that went down in that time, so a quick second tap still
// turns the page. Set this to false if pages ever turn by themselves.
const bool CATCH_TAPS_DURING_REFRESH = true;
const uint32_t MUSIC_GRACE_MS = 4000; // the audio board may be silent this long (a restart) before it counts as off
const int MARGIN_X = 36;
const int TOP = 56;                   // first text baseline area starts below the header
const int FOOTER_H = 58;
const GFXfont* BODY = &FreeSerif12pt7b;        // book text; changed by the Text size setting
const GFXfont* BOLD = &FreeSerifBold12pt7b;    // book headings
const GFXfont* UI = &FreeSans9pt7b;            // small labels
const GFXfont* UI_BOLD = &FreeSansBold9pt7b;
const GFXfont* MENU = &FreeSans12pt7b;         // list rows
const GFXfont* MENU_BOLD = &FreeSansBold12pt7b;
const GFXfont* BIG_BOLD = &FreeSansBold18pt7b; // Home tiles, screen headings
const GFXfont* TITLE_MED = &FreeSansBold18pt7b;
const GFXfont* TITLE = &FreeSansBold24pt7b;    // track title, sleep screen
// Every screen is sans-serif; only the text of the book itself (BODY, BOLD) is serif.
// Bump when anything above that changes where pages break: old page indexes are rebuilt.
const uint32_t LAYOUT_VERSION = 4;

const char* CACHE_DIR = "/books/.cache";

// ---------- settings the user can change (Settings screen) ----------
const uint16_t SLEEP_MINUTES[] = {5, 10, 30, 60};
const char* const TEXT_SIZE_NAMES[] = {"Small", "Medium", "Large"};
const uint8_t REFRESH_PAGES[] = {5, 10, 20};
struct DuetSettings {
  uint8_t sleepIdx = 1;               // 10 min
  uint8_t textIdx = 1;                // Medium
  uint8_t refreshIdx = 1;             // full refresh every 10 page turns
};
DuetSettings settings;

// ---------- screens and input ----------
enum DuetScreen : uint8_t { SCR_READER, SCR_HOME, SCR_LIBRARY, SCR_PLAYING, SCR_TRACKS, SCR_SETTINGS, SCR_ALBUMS };
enum DuetEvent : uint8_t { EV_NONE, EV_L, EV_R, EV_HOLD_L, EV_HOLD_R, EV_HOLD_BOTH };
DuetScreen screen = SCR_HOME;
bool dirty = true;                    // the screen needs redrawing
bool needFull = true;                 // ...with a full (flashing) refresh

// ---------- book state ----------
SdFile book;                          // the text being read (a .txt, or an EPUB's cached text)
char bookName[64] = "";               // file name in /books, e.g. "Moby-Dick.epub"
char bookTitle[96] = "";              // shown in the header
char bookAuthor[48] = "";
uint32_t bookSize = 0;
uint32_t bookSeq = 0;                 // "last read" order: bigger = more recent
uint32_t readSeq = 0;                 // highest bookSeq handed out so far
uint32_t pageStart = 0, pageEnd = 0;
uint32_t* pages = nullptr;            // page index: byte offset where each page starts
int pageCount = 0, currentPage = 0;
bool stateDirty = false;              // position changed since it was last saved
bool layoutStale = false;             // text size changed: re-paginate before reading on

struct BookEntry {                    // one row of the Library
  char file[64];
  char title[56];
  char author[32];
  int8_t pct;                         // -1 = never opened
  uint32_t seq;
};
const int MAX_BOOKS = 120;
BookEntry* lib = nullptr;
int libCount = 0, libSel = 0;
bool libLoaded = false;

// ---------- music state from the audio board ----------
DuetStatus music = {};
bool musicOnline = false;
uint32_t musicSeenAt = 0;             // millis() of its last good reply
uint32_t musicFastUntil = 0;          // poll quickly until then (after a command / restart)
uint32_t musicSwitchUntil = 0;        // an output switch / pairing was asked for; 0 = none under way
char btName[DUET_TEXT_LEN] = "";      // headphones' name
int npTrack = -1;                     // which track the three names below belong to
char npPath[DUET_TEXT_LEN] = "", npNext1[DUET_TEXT_LEN] = "", npNext2[DUET_TEXT_LEN] = "";
const int TRACK_ROWS = 9;
int tlFirst = 0, tlCount = 0;         // the tracks the list shows: one album, or the whole card
char tlAlbum[DUET_FOLDER_LEN] = "";   // that album's name ("" = the whole card)
int tlSel = 0;                        // selected track; -1 = the Shuffle row (whole-card list only)
int tlPage = -1;                      // first track of the page tlNames holds
char tlNames[TRACK_ROWS][DUET_TEXT_LEN];
// Albums = the folders on the music card
struct AlbumRow { char name[DUET_FOLDER_LEN]; uint16_t first, count; };
int albumTotal = -1;                  // folders on the card (-1 = not asked yet)
int alSel = 0;                        // selected album; -1 = the Shuffle row
int alPage = -1;                      // which page alRows holds
AlbumRow alRows[TRACK_ROWS];
DuetEvent pendingTap = EV_NONE;       // a tap that arrived while the panel was redrawing

int homeSel = 0, setSel = 0;
float battVolts = 0;
int battPct = -1;                     // -1 = not read yet / no battery
uint32_t battAt = 0;

uint32_t lastActivity = 0;

// =====================================================================
// Text measurement using the font's own advance widths
// =====================================================================
int glyphAdvance(const GFXfont* f, char c) {
  if (c < f->first || c > f->last) return 0;
  return f->glyph[c - f->first].xAdvance;
}

int textWidth(const GFXfont* f, const char* s) {
  int w = 0;
  while (*s) w += glyphAdvance(f, *s++);
  return w;
}

bool endsWith(const char* s, const char* suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

// The built-in fonts are ASCII only. Map one UTF-8 sequence at p to ASCII: accented
// Latin letters lose their accent, typographic quotes/dashes become plain ones.
// Returns bytes consumed; writes 0-3 chars to out.
int utf8ToAscii(const uint8_t* p, int avail, char* out, int* outLen) {
  *outLen = 0;
  uint8_t c = p[0];
  if (c < 0x80) { out[0] = (char)c; *outLen = 1; return 1; }
  int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
  if (n > avail) return avail;
  uint32_t cp = 0;
  if (n == 2) cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
  else if (n == 3) cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
  else return n;                                        // emoji and other 4-byte: dropped

  // U+00C0..U+00FF: Latin-1 letters -> base letter
  static const char LATIN1[] =
    "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"              // C0-DF
    "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";             // E0-FF
  if (cp >= 0xC0 && cp <= 0xFF) {
    if (cp == 0xC6) { out[0] = 'A'; out[1] = 'E'; *outLen = 2; }
    else if (cp == 0xE6) { out[0] = 'a'; out[1] = 'e'; *outLen = 2; }
    else if (cp == 0xDF) { out[0] = 's'; out[1] = 's'; *outLen = 2; }
    else { out[0] = LATIN1[cp - 0xC0]; *outLen = 1; }
    return n;
  }
  switch (cp) {
    case 0xA0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: out[0] = ' '; *outLen = 1; break;
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: case 0xB4: out[0] = '\''; *outLen = 1; break;
    case 0x201C: case 0x201D: case 0x201E: case 0xAB: case 0xBB: case 0x2033: out[0] = '"'; *outLen = 1; break;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: out[0] = '-'; *outLen = 1; break;
    case 0x2014: case 0x2015: out[0] = '-'; out[1] = '-'; *outLen = 2; break;
    case 0x2026: out[0] = out[1] = out[2] = '.'; *outLen = 3; break;
    case 0x152: out[0] = 'O'; out[1] = 'E'; *outLen = 2; break;
    case 0x153: out[0] = 'o'; out[1] = 'e'; *outLen = 2; break;
    case 0xB7: case 0x2022: out[0] = '*'; *outLen = 1; break;
    case 0xD7: out[0] = 'x'; *outLen = 1; break;
    case 0xB0: out[0] = 'o'; *outLen = 1; break;
    case 0x2020: out[0] = '+'; *outLen = 1; break;
    default: break;                                     // soft hyphen, BOM, zero-width, others: dropped
  }
  return n;
}

// =====================================================================
// Page layout: lays out text from `start`; draws it if `draw`. Returns the
// file offset where the next page begins.
//
// Text format (plain .txt files and converted EPUBs):
//   blank line            paragraph break (a single newline is just a space)
//   '\f'                  chapter break: the chapter starts on a new page
//   '\x01' at para start  heading: bold, centred
//   '\x02' at para start  centred line (scene break)
// =====================================================================
const int BUF_SIZE = 6144;
uint8_t buf[BUF_SIZE];
bool lastLayoutHadText = false;        // did the last layoutPage() place any words?

struct Line {                          // words waiting to be drawn on the current line
  char text[200];
  int used = 0;
  int n = 0;
  int16_t x[48];
  uint8_t off[48];
  int width = 0;
  void clear() { used = 0; n = 0; width = 0; }
};

uint32_t layoutPage(uint32_t start, bool draw) {
  const int lineH = BODY->yAdvance;
  const int paraGap = lineH / 3;
  const int maxW = display.width() - 2 * MARGIN_X;
  const int bottom = display.height() - FOOTER_H;

  lastLayoutHadText = false;
  if (!book.seekSet(start)) return start;
  int len = book.read(buf, BUF_SIZE);
  if (len <= 0) return start;
  bool atEof = (start + len >= bookSize);

  static Line line;
  line.clear();
  lastLayoutHadText = false;
  const GFXfont* font = BODY;
  bool centered = false, paraStart = true, pageHasText = false;
  int y = TOP + lineH;                 // baseline of the current line
  int newlines = 0;

  auto flushLine = [&]() {
    if (draw && line.n) {
      display.setFont(font);
      int xo = centered ? (maxW - line.width) / 2 : 0;
      for (int k = 0; k < line.n; k++) {
        display.setCursor(MARGIN_X + xo + line.x[k], y);
        display.print(line.text + line.off[k]);
      }
    }
    if (line.n) pageHasText = lastLayoutHadText = true;
    line.clear();
  };

  int i = 0;
  // A page never starts with blank space or a chapter break
  while (i < len && (buf[i] == ' ' || buf[i] == '\r' || buf[i] == '\n' || buf[i] == '\t' || buf[i] == '\f')) i++;

  while (i < len) {
    uint8_t c = buf[i];
    if (c == '\r' || c == ' ' || c == '\t') { i++; continue; }
    if (c == '\n') { newlines++; i++; continue; }
    if (c == '\f') {                                   // chapter break: end the page here
      flushLine();
      if (pageHasText) return start + i;
      i++; continue;
    }

    // A blank line (2+ newlines) ends the paragraph
    if (newlines >= 2 && (line.n || pageHasText)) {
      flushLine();
      y += lineH + paraGap;
      font = BODY; centered = false; paraStart = true;
      if (y > bottom) return start + i;
    }
    newlines = 0;

    if (paraStart) {
      paraStart = false;
      if (c == 0x01 || c == 0x02) {
        centered = true;
        if (c == 0x01) {
          font = BOLD;
          if (pageHasText) { y += lineH / 2; if (y > bottom) return start + i; }
        }
        i++;
        continue;
      }
    }

    // Read one word, converting UTF-8 to the font's ASCII as we go
    int wordStart = i;
    char word[96];
    int wlen = 0;
    while (i < len && buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t' && buf[i] != '\f') {
      char tmp[3]; int tn;
      int used = utf8ToAscii(buf + i, len - i, tmp, &tn);
      for (int k = 0; k < tn && wlen < (int)sizeof(word) - 1; k++) word[wlen++] = tmp[k];
      i += used;
    }
    word[wlen] = 0;
    if (i >= len && !atEof) {                          // word cut by the buffer: next page
      flushLine();
      return start + wordStart;
    }
    if (wlen == 0) continue;

    int w = textWidth(font, word);
    int spaceW = glyphAdvance(font, ' ');

    if (w > maxW) {                                    // a "word" wider than the page (a URL): break it
      int lines = (w + maxW - 1) / maxW + 1;
      if ((line.n || pageHasText) && y + (lines - 1) * lineH > bottom) { flushLine(); return start + wordStart; }
      if (line.n) { flushLine(); y += lineH; }
      int pos = 0;
      while (pos < wlen) {
        int pw = 0, end = pos;
        while (end < wlen && pw + glyphAdvance(font, word[end]) <= maxW) pw += glyphAdvance(font, word[end++]);
        if (end == pos) end++;
        line.x[0] = 0; line.off[0] = 0; line.n = 1; line.width = pw;
        memcpy(line.text, word + pos, end - pos); line.text[end - pos] = 0; line.used = end - pos + 1;
        pos = end;
        if (pos < wlen) {
          flushLine(); y += lineH;
          if (y > bottom) return start + i;            // only if the word is longer than a whole page
        }
      }
      continue;                                        // the last piece stays on the line
    }
    bool fits = line.n == 0 || line.width + spaceW + w <= maxW;
    if (!fits || line.n >= 48 || line.used + wlen + 1 > (int)sizeof(line.text)) {
      flushLine();
      y += lineH;
      if (y > bottom) return start + wordStart;
    }
    int x = line.n ? line.width + spaceW : 0;
    line.x[line.n] = x;
    line.off[line.n] = line.used;
    memcpy(line.text + line.used, word, wlen + 1);
    line.used += wlen + 1;
    line.n++;
    line.width = x + w;
  }
  flushLine();
  return start + len;
}

// =====================================================================
// Progress and message screens
// =====================================================================
bool screenFresh = false;              // next screen must be a full refresh (after the grey splash)

void progressScreen(const char* heading, const char* name, int pct, bool first) {
  display.clearDisplay();
  display.setTextColor(BLACK);
  display.setFont(MENU_BOLD);
  int cx = display.width() / 2;
  display.setCursor(cx - textWidth(MENU_BOLD, heading) / 2, 330);
  display.print(heading);
  display.setFont(UI);
  char t[64];
  strncpy(t, name, sizeof(t) - 1); t[sizeof(t) - 1] = 0;
  for (char* p = t; *p; p++) if (*p < 0x20 || *p > 0x7E) *p = ' ';
  while (strlen(t) > 0 && textWidth(UI, t) > display.width() - 2 * MARGIN_X) t[strlen(t) - 1] = 0;
  display.setCursor(cx - textWidth(UI, t) / 2, 370);
  display.print(t);
  int bw = 360, bx = cx - bw / 2, by = 400;
  display.drawRect(bx, by, bw, 16, BLACK);
  display.drawRect(bx + 1, by + 1, bw - 2, 14, BLACK);
  display.fillRect(bx + 3, by + 3, (bw - 6) * pct / 100, 10, BLACK);
  char p[12];
  snprintf(p, sizeof(p), "%d%%", pct);
  display.setCursor(cx - textWidth(UI, p) / 2, 448);
  display.print(p);
  if (first || screenFresh) { display.display(); screenFresh = false; }
  else display.partialUpdate();
}

void messageScreen(const char* heading, const char* detail) {
  display.clearDisplay();
  display.setTextColor(BLACK);
  display.setFont(MENU_BOLD);
  display.setCursor((display.width() - textWidth(MENU_BOLD, heading)) / 2, 340);
  display.print(heading);
  display.setFont(UI);
  display.setCursor((display.width() - textWidth(UI, detail)) / 2, 380);
  display.print(detail);
  const char* hint = "Press a page key to continue";
  display.setCursor((display.width() - textWidth(UI, hint)) / 2, 440);
  display.print(hint);
  display.display();
  screenFresh = false;
}

bool readButton(uint8_t pin) { return display.expander2.digitalRead(pin) == LOW; }

void waitForKey() {
  while (readButton(BTN_BACK) || readButton(BTN_FWD)) delay(10);
  while (!readButton(BTN_BACK) && !readButton(BTN_FWD)) delay(15);
  while (readButton(BTN_BACK) || readButton(BTN_FWD)) delay(10);
}

// =====================================================================
// Cache: converted EPUB text, metadata and page indexes in /books/.cache
// =====================================================================
void ensureCacheDir() {
  if (!display.getSdFat().exists(CACHE_DIR)) display.getSdFat().mkdir(CACHE_DIR);
}

void cachePath(char* out, size_t cap, const char* name, const char* ext) {
  snprintf(out, cap, "%s/%s%s", CACHE_DIR, name, ext);
}

// Title / author for an EPUB from its .meta file (written after a successful conversion)
bool readMeta(const char* name, uint32_t epubSize, char* title, size_t tcap, char* author, size_t acap) {
  char path[128];
  cachePath(path, sizeof(path), name, ".meta");
  SdFile f;
  if (!f.open(path, O_RDONLY)) return false;
  char data[256] = {0};
  f.read(data, sizeof(data) - 1);
  f.close();
  char* lines[4] = {data, nullptr, nullptr, nullptr};
  for (int k = 1; k < 4; k++) {
    char* nl = lines[k - 1] ? strchr(lines[k - 1], '\n') : nullptr;
    if (!nl) return false;
    *nl = 0;
    lines[k] = nl + 1;
  }
  char* nl = strchr(lines[3], '\n'); if (nl) *nl = 0;   // author line
  if (strcmp(lines[0], "DUET1") != 0) return false;
  if (epubSize && strtoul(lines[1], nullptr, 10) != epubSize) return false;
  if (title) { strncpy(title, lines[2], tcap - 1); title[tcap - 1] = 0; }
  if (author) { strncpy(author, lines[3], acap - 1); author[acap - 1] = 0; }
  return true;
}

struct SdIn : EpubIn {
  SdFile* f;
  uint32_t size() override { return f->fileSize(); }
  bool seek(uint32_t p) override { return f->seekSet(p); }
  int read(void* b, uint32_t l) override { return f->read(b, l); }
};
struct SdOut : EpubOut {
  SdFile* f;
  bool write(const void* b, uint32_t l) override { return f->write((const uint8_t*)b, l) == l; }
};

struct ProgressCtx { const char* heading; const char* name; int lastPct; };
void epubProgress(int done, int total, void* ctx) {
  ProgressCtx* p = (ProgressCtx*)ctx;
  int pct = total ? done * 100 / total : 100;
  if (pct - p->lastPct >= 10) { p->lastPct = pct; progressScreen(p->heading, p->name, pct, false); }
  lastActivity = millis();
  delay(1);
}

// Convert an EPUB to cached text if needed. On success fills bookTitle.
bool prepareEpub(const char* name) {
  char epubPath[96];
  snprintf(epubPath, sizeof(epubPath), "/books/%s", name);
  SdFile epub;
  if (!epub.open(epubPath, O_RDONLY)) { messageScreen("Can't open this book", name); waitForKey(); return false; }
  uint32_t esize = epub.fileSize();
  char txtPath[128], metaPath[128], pagesPath[128];
  cachePath(txtPath, sizeof(txtPath), name, ".txt");
  if (readMeta(name, esize, bookTitle, sizeof(bookTitle), bookAuthor, sizeof(bookAuthor)) && display.getSdFat().exists(txtPath)) { epub.close(); return true; }

  ensureCacheDir();
  cachePath(metaPath, sizeof(metaPath), name, ".meta");
  cachePath(pagesPath, sizeof(pagesPath), name, ".pages");
  display.getSdFat().remove(metaPath);
  display.getSdFat().remove(pagesPath);

  SdFile out;
  if (!out.open(txtPath, O_WRONLY | O_CREAT | O_TRUNC)) { epub.close(); messageScreen("Can't write to the card", "Is it full or locked?"); waitForKey(); return false; }
  ProgressCtx ctx = {"Preparing book", name, 0};
  progressScreen(ctx.heading, name, 0, true);
  SdIn in; in.f = &epub;
  SdOut o; o.f = &out;
  EpubInfo info;
  EpubResult r = epubConvert(in, o, info, epubProgress, &ctx);
  out.close();
  epub.close();
  if (r != EPUB_OK) {
    display.getSdFat().remove(txtPath);
    messageScreen("Can't open this book", epubErrorText(r));
    waitForKey();
    return false;
  }
  if (!info.title[0]) { strncpy(info.title, name, sizeof(info.title) - 1); char* d = strrchr(info.title, '.'); if (d) *d = 0; }
  SdFile meta;
  if (meta.open(metaPath, O_WRONLY | O_CREAT | O_TRUNC)) {
    char line[256];
    int n = snprintf(line, sizeof(line), "DUET1\n%lu\n%s\n%s\n", (unsigned long)esize, info.title, info.author);
    meta.write(line, n);
    meta.close();
  }
  strncpy(bookTitle, info.title, sizeof(bookTitle) - 1);
  bookTitle[sizeof(bookTitle) - 1] = 0;
  strncpy(bookAuthor, info.author, sizeof(bookAuthor) - 1);
  bookAuthor[sizeof(bookAuthor) - 1] = 0;
  return true;
}

// Page index: where every page starts. Built once per book (and again if the layout changes).
struct PagesHeader { uint32_t magic, layout, textSize, count; };
const uint32_t PAGES_MAGIC = 0x31475044;               // "DPG1"

uint32_t layoutKey() {
  return LAYOUT_VERSION ^ (MARGIN_X << 8) ^ (TOP << 16) ^ (FOOTER_H << 24) ^ (ROTATION << 4) ^ (display.width() * 7919u) ^
         (settings.textIdx * 0x9E3779B1u);   // each text size has its own page breaks
}

bool loadOrBuildIndex() {
  free(pages); pages = nullptr; pageCount = 0;
  char path[128];
  cachePath(path, sizeof(path), bookName, ".pages");
  SdFile f;
  if (f.open(path, O_RDONLY)) {
    PagesHeader h;
    if (f.read(&h, sizeof(h)) == (int)sizeof(h) && h.magic == PAGES_MAGIC && h.layout == layoutKey() &&
        h.textSize == bookSize && h.count > 0 && h.count < 200000) {
      pages = (uint32_t*)malloc(h.count * sizeof(uint32_t));
      if (pages && f.read(pages, h.count * sizeof(uint32_t)) == (int)(h.count * sizeof(uint32_t))) {
        pageCount = h.count;
        f.close();
        return true;
      }
      free(pages); pages = nullptr;
    }
    f.close();
  }

  // Build it: lay out every page without drawing
  int cap = 256;
  pages = (uint32_t*)malloc(cap * sizeof(uint32_t));
  if (!pages) return false;
  uint32_t s = 0;
  int lastPct = -100;
  progressScreen("Paginating", bookTitle, 0, true);
  while (true) {
    if (pageCount == cap) {
      uint32_t* np = (uint32_t*)realloc(pages, cap * 2 * sizeof(uint32_t));
      if (!np) return false;
      pages = np; cap *= 2;
    }
    pages[pageCount++] = s;
    uint32_t e = layoutPage(s, false);
    if (!lastLayoutHadText && pageCount > 1) { pageCount--; break; }   // only blank space was left
    if (e <= s || e >= bookSize) break;
    s = e;
    int pct = (int)((uint64_t)s * 100 / bookSize);
    if (pct - lastPct >= 10) { lastPct = pct; progressScreen("Paginating", bookTitle, pct, false); lastActivity = millis(); }
  }
  ensureCacheDir();
  if (f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
    PagesHeader h = {PAGES_MAGIC, layoutKey(), bookSize, (uint32_t)pageCount};
    f.write((const uint8_t*)&h, sizeof(h));
    f.write((const uint8_t*)pages, pageCount * sizeof(uint32_t));
    f.close();
  }
  return true;
}

int pageForOffset(uint32_t off) {
  int lo = 0, hi = pageCount - 1;
  while (lo < hi) {                                    // last page whose start <= off
    int mid = (lo + hi + 1) / 2;
    if (pages[mid] <= off) lo = mid; else hi = mid - 1;
  }
  return lo;
}

// =====================================================================
// Drawing helpers shared by every screen
// =====================================================================
// Any text (UTF-8) -> the fonts' printable ASCII
void asciiClean(const char* src, char* out, size_t cap) {
  size_t o = 0;
  int n = strlen(src), i = 0;
  while (i < n && o + 4 < cap) {
    char tmp[3]; int tn;
    int used = utf8ToAscii((const uint8_t*)src + i, n - i, tmp, &tn);
    if (used <= 0) used = 1;
    for (int k = 0; k < tn; k++) out[o++] = (tmp[k] < 0x20 || tmp[k] > 0x7E) ? ' ' : tmp[k];
    i += used;
  }
  out[o] = 0;
}

// Clean `src` and cut it to `maxW` pixels, ending in "..." when cut
void fitText(const GFXfont* f, const char* src, int maxW, char* out, size_t cap) {
  asciiClean(src, out, cap);
  if (textWidth(f, out) <= maxW) return;
  int n = strlen(out);
  int dots = 3 * glyphAdvance(f, '.');
  while (n > 0 && textWidth(f, out) + dots > maxW) out[--n] = 0;
  while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
  if ((size_t)n + 4 <= cap) strcat(out, "...");
}

void drawText(const GFXfont* f, int x, int y, const char* s) {
  display.setFont(f);
  display.setCursor(x, y);
  display.print(s);
}
void drawTextRight(const GFXfont* f, int xRight, int y, const char* s) { drawText(f, xRight - textWidth(f, s), y, s); }
void drawTextCenter(const GFXfont* f, int cx, int y, const char* s) { drawText(f, cx - textWidth(f, s) / 2, y, s); }
// Fitted to maxW; returns the width drawn
int drawFitted(const GFXfont* f, int x, int y, int maxW, const char* s) {
  char t[100];
  fitText(f, s, maxW, t, sizeof(t));
  drawText(f, x, y, t);
  return textWidth(f, t);
}

// Word-wraps `s` into at most maxLines lines; the last line is cut with "...".
// Returns the number of lines drawn.
int drawWrapped(const GFXfont* f, const char* s, int x, int y, int maxW, int maxLines, bool centered) {
  char clean[120];
  asciiClean(s, clean, sizeof(clean));
  const char* p = clean;
  int lines = 0;
  while (*p && lines < maxLines) {
    while (*p == ' ') p++;
    char line[120];
    int len = 0, lastBreak = 0;
    const char* q = p;
    const char* breakAt = nullptr;
    while (*q) {                                      // take words while they fit
      line[len++] = *q; line[len] = 0;
      if (textWidth(f, line) > maxW) { len--; line[len] = 0; break; }
      q++;
      if (*q == ' ' || *q == 0) { lastBreak = len; breakAt = q; }
    }
    if (*q && lines < maxLines - 1 && breakAt) { line[lastBreak] = 0; q = breakAt; }
    else if (*q) { fitText(f, p, maxW, line, sizeof(line)); q = p + strlen(p); }   // last line: cut
    if (centered) drawTextCenter(f, x + maxW / 2, y, line);
    else drawText(f, x, y, line);
    y += f->yAdvance;
    lines++;
    p = q;
  }
  return lines;
}

void thickLine(int x0, int y0, int x1, int y1, int t, uint16_t c) {
  for (int a = 0; a < t; a++) for (int b = 0; b < t; b++) display.drawLine(x0 + a, y0 + b, x1 + a, y1 + b, c);
}
void boxOutline(int x, int y, int w, int h, int t, uint16_t c) {
  for (int k = 0; k < t; k++) display.drawRect(x + k, y + k, w - 2 * k, h - 2 * k, c);
}

// ---------- icons ----------
void iconBluetooth(int x, int y, int h, int t, uint16_t c) {     // the rune, in a box h tall
  int w = h * 2 / 3;
  int mx = x + w / 2;
  thickLine(mx, y, mx, y + h, t, c);
  thickLine(mx, y, x + w, y + h / 4, t, c);
  thickLine(x + w, y + h / 4, x, y + h * 3 / 4, t, c);
  thickLine(mx, y + h, x + w, y + h * 3 / 4, t, c);
  thickLine(x + w, y + h * 3 / 4, x, y + h / 4, t, c);
}
void iconPlay(int x, int y, int h, uint16_t c) { display.fillTriangle(x, y, x, y + h, x + h * 5 / 6, y + h / 2, c); }
void iconPause(int x, int y, int h, uint16_t c) {
  int bw = h / 4 < 3 ? 3 : h / 4;
  display.fillRect(x, y, bw, h, c);
  display.fillRect(x + h / 2, y, bw, h, c);
}
void iconSkip(int x, int y, int h, bool forward, uint16_t c) {   // two triangles
  int w = h * 2 / 3;
  for (int k = 0; k < 2; k++) {
    int x0 = x + k * (w + 2);
    if (forward) display.fillTriangle(x0, y, x0, y + h, x0 + w, y + h / 2, c);
    else display.fillTriangle(x0 + w, y, x0 + w, y + h, x0, y + h / 2, c);
  }
}
void iconChevron(int x, int y, int h, uint16_t c) {
  thickLine(x, y, x + h / 2, y + h / 2, 3, c);
  thickLine(x + h / 2, y + h / 2, x, y + h, 3, c);
}
void iconBook(int x, int y, uint16_t c) {                        // 48 x 40 open book
  boxOutline(x, y + 2, 23, 36, 3, c);
  boxOutline(x + 25, y + 2, 23, 36, 3, c);
  for (int k = 0; k < 3; k++) {
    display.drawFastHLine(x + 6, y + 12 + k * 8, 11, c);
    display.drawFastHLine(x + 31, y + 12 + k * 8, 11, c);
  }
}
void iconNote(int x, int y, uint16_t c) {                        // 48 x 44 pair of notes
  display.fillCircle(x + 10, y + 36, 7, c);
  display.fillCircle(x + 36, y + 30, 7, c);
  display.fillRect(x + 15, y + 6, 3, 30, c);
  display.fillRect(x + 41, y, 3, 30, c);
  thickLine(x + 15, y + 6, x + 41, y, 4, c);
}
void iconSliders(int x, int y, uint16_t c, uint16_t bg) {        // 48 x 40 three sliders
  const int knob[3] = {12, 30, 20};
  for (int k = 0; k < 3; k++) {
    int yy = y + 6 + k * 14;
    display.fillRect(x, yy - 1, 48, 3, c);
    display.fillCircle(x + knob[k], yy, 6, c);
    display.fillCircle(x + knob[k], yy, 3, bg);
  }
}
void iconBattery(int x, int y, int pct, uint16_t c) {            // 26 x 14
  boxOutline(x, y, 22, 14, 2, c);
  display.fillRect(x + 22, y + 4, 3, 6, c);
  int w = pct * 16 / 100;
  if (w > 0) display.fillRect(x + 3, y + 3, w, 8, c);
}

// ---------- battery ----------
int percentFromVolts(float v) {
  static const float V[] = {3.30f, 3.50f, 3.60f, 3.70f, 3.80f, 3.90f, 4.00f, 4.10f, 4.16f};
  static const int P[] = {0, 8, 20, 40, 60, 75, 88, 97, 100};
  if (v <= V[0]) return 0;
  for (int k = 1; k < 9; k++) if (v < V[k]) return P[k - 1] + (int)((v - V[k - 1]) / (V[k] - V[k - 1]) * (P[k] - P[k - 1]));
  return 100;
}
void updateBattery(bool force) {
  if (!force && battAt != 0 && millis() - battAt < 60000) return;
  battAt = millis() | 1;
  battVolts = display.readBattery();
  battPct = battVolts > 2.5f ? percentFromVolts(battVolts) : -1;     // below 2.5 V: no battery fitted
}

// ---------- the bar across the top of every menu screen ----------
const char* outputName() { return (music.flags & FLAG_BT_MODE) ? "Bluetooth" : "Jack"; }

void drawTopBar(const char* title, const char* extra) {
  display.setTextColor(BLACK);
  drawText(UI_BOLD, 20, 27, title);
  int x = display.width() - 20;
  updateBattery(false);
  if (battPct >= 0) {
    char p[12];
    snprintf(p, sizeof(p), "%d%%", battPct);
    x -= textWidth(UI, p);
    drawText(UI, x, 27, p);
    x -= 32;
    iconBattery(x, 13, battPct, BLACK);
    x -= 18;
  }
  if (extra && extra[0]) {
    x -= textWidth(UI, extra);
    drawText(UI, x, 27, extra);
    x -= 18;
  } else if (musicOnline && !(music.flags & FLAG_NO_CARD)) {
    const char* o = outputName();
    x -= textWidth(UI, o);
    drawText(UI, x, 27, o);
    if (music.flags & FLAG_BT_MODE) iconBluetooth(x - 21, 11, 18, 2, BLACK);
  }
  display.fillRect(0, 39, display.width(), 2, BLACK);
}

// ---------- the key hints across the bottom ----------
// A hint is text with key caps in brackets: "hold [R] open"
int hintWidth(const char* s) {
  int w = 0;
  for (const char* p = s; *p; p++) {
    if (*p == '[' && p[1] && p[2] == ']') { w += 26; p += 2; }
    else w += glyphAdvance(UI, *p);
  }
  return w;
}
void drawHint(int x, int y, const char* s) {
  display.setFont(UI);
  for (const char* p = s; *p; p++) {
    if (*p == '[' && p[1] && p[2] == ']') {
      display.drawRoundRect(x, y - 17, 22, 23, 4, BLACK);
      display.drawRoundRect(x + 1, y - 16, 20, 21, 3, BLACK);
      char k[2] = {p[1], 0};
      drawText(UI_BOLD, x + 11 - textWidth(UI_BOLD, k) / 2, y, k);
      display.setFont(UI);
      x += 26;
      p += 2;
    } else {
      display.setCursor(x, y);
      display.print(*p);
      x += glyphAdvance(UI, *p);
    }
  }
}
void drawHints(const char* a, const char* b, const char* c) {
  int y0 = display.height() - 52;
  display.fillRect(0, y0, display.width(), 2, BLACK);
  int base = y0 + 33;
  const char* h[3] = {a, b, c};
  int n = 0;
  for (int k = 0; k < 3; k++) if (h[k] && h[k][0]) n++;
  if (n == 1) { drawHint((display.width() - hintWidth(a)) / 2, base, a); return; }
  if (a && a[0]) drawHint(20, base, a);
  if (n == 3) drawHint((display.width() - hintWidth(b)) / 2, base, b);
  const char* last = n == 3 ? c : b;
  if (last && last[0]) drawHint(display.width() - 20 - hintWidth(last), base, last);
}

void pageIndicator(int page, int pages_, int y) {
  if (pages_ <= 1) return;
  char t[40];
  snprintf(t, sizeof(t), "Page %d of %d", page + 1, pages_);
  drawTextRight(UI, display.width() - 28, y, t);
}

void pushScreen() {                   // send the drawn screen to the panel
  if (needFull || screenFresh) display.display();
  else display.partialUpdate();
  needFull = false;
  screenFresh = false;
  dirty = false;
}

// =====================================================================
// Audio board link
// =====================================================================
// One request and its reply. False if the board didn't answer or the reply was garbled.
bool musicRequest(uint8_t cmd, uint16_t arg, uint8_t* raw) {
  Wire.beginTransmission(DUET_AUDIO_ADDR);
  Wire.write(cmd);
  Wire.write((uint8_t)(arg & 0xFF));
  Wire.write((uint8_t)(arg >> 8));
  if (Wire.endTransmission() != 0) return false;
  delay(4);                            // the board gets its answer ready
  int got = Wire.requestFrom((uint8_t)DUET_AUDIO_ADDR, (uint8_t)DUET_REPLY_LEN);
  if (got != DUET_REPLY_LEN) {
    while (Wire.available()) Wire.read();
    return false;
  }
  Wire.readBytes(raw, DUET_REPLY_LEN);
  return raw[DUET_REPLY_LEN - 1] == duetCheck(raw);
}

void sendMusicArg(uint8_t cmd, uint16_t arg) {
  Wire.beginTransmission(DUET_AUDIO_ADDR);
  Wire.write(cmd);
  Wire.write((uint8_t)(arg & 0xFF));
  Wire.write((uint8_t)(arg >> 8));
  Wire.endTransmission();
  musicFastUntil = millis() + 4000;   // the answer shows up in the next few polls
}
void sendMusic(uint8_t cmd) { sendMusicArg(cmd, 0); }
// A command that makes the audio board restart (output switch, pairing): give it time
void sendMusicRestart(uint8_t cmd, uint16_t arg) {
  sendMusicArg(cmd, arg);
  musicSeenAt = millis();
  musicFastUntil = millis() + 8000;
  musicSwitchUntil = (millis() + 8000) | 1;
}
bool musicSwitching() { return musicSwitchUntil && (int32_t)(musicSwitchUntil - millis()) > 0; }

// Asks for a track's name or the headphones' name. False if the board didn't answer.
bool musicText(uint8_t cmd, uint16_t index, char* out, size_t cap) {
  out[0] = 0;
  if (!musicOnline) return false;
  for (int attempt = 0; attempt < 2; attempt++) {
    uint8_t raw[DUET_REPLY_LEN];
    if (!musicRequest(cmd, index, raw)) continue;
    DuetText t;
    memcpy(&t, raw, sizeof(t));
    if (t.magic == DUET_MAGIC_TEXT && t.kind == cmd && t.index == index) {
      t.text[DUET_TEXT_LEN - 1] = 0;
      strncpy(out, t.text, cap - 1);
      out[cap - 1] = 0;
      return true;
    }
  }
  return false;
}

// Asks about one folder (album); DUET_FOLDER_CURRENT = the one that is playing
bool musicFolder(uint16_t index, DuetFolder& f) {
  if (!musicOnline) return false;
  for (int attempt = 0; attempt < 2; attempt++) {
    uint8_t raw[DUET_REPLY_LEN];
    if (!musicRequest(CMD_GET_FOLDER, index, raw)) continue;
    memcpy(&f, raw, sizeof(f));
    if (f.magic == DUET_MAGIC_FOLDER && f.kind == CMD_GET_FOLDER && f.index == index) {
      f.text[DUET_FOLDER_LEN - 1] = 0;
      return true;
    }
  }
  return false;
}

void forgetMusicLists() { npTrack = -1; tlPage = -1; tlCount = 0; alPage = -1; albumTotal = -1; }

// What changed since the last poll
const uint8_t CH_STATE = 1;           // play/pause, output, connection, volume, online
const uint8_t CH_TRACK = 2;           // a different track or title
const uint8_t CH_TIME = 4;            // only the position moved

uint8_t pollMusic() {
  uint8_t raw[DUET_REPLY_LEN];
  bool ok = musicRequest(CMD_GET_STATUS, 0, raw) && raw[0] == DUET_MAGIC_STATUS;
  uint32_t now = millis();
  if (!ok) {
    if (!musicOnline || now - musicSeenAt < MUSIC_GRACE_MS) return 0;   // a restart takes a moment: no flicker
    musicOnline = false;
    forgetMusicLists();
    return CH_STATE;
  }
  musicSeenAt = now;
  DuetStatus s;
  memcpy(&s, raw, sizeof(s));
  s.title[DUET_TITLE_LEN - 1] = 0;
  // Back after a restart of its own and still reading the card: keep showing what we had
  if ((s.flags & FLAG_STARTING) && musicOnline && !(music.flags & FLAG_STARTING)) { musicFastUntil = now + 3000; return 0; }
  uint8_t ch = 0;
  if (!musicOnline || s.flags != music.flags || s.volume != music.volume) ch |= CH_STATE;
  if (!musicOnline || s.track != music.track || s.trackCount != music.trackCount ||
      strncmp(s.title, music.title, DUET_TITLE_LEN) != 0) ch |= CH_TRACK;
  if (s.elapsed != music.elapsed || s.duration != music.duration) ch |= CH_TIME;
  if (!musicOnline || s.trackCount != music.trackCount) forgetMusicLists();
  if (s.flags & FLAG_STARTING) musicFastUntil = now + 3000;            // it will be ready in a moment
  else if (musicSwitchUntil) { musicSwitchUntil = 0; ch |= CH_STATE; }  // the switch has happened
  music = s;
  musicOnline = true;
  return ch;
}

bool musicStarting() { return musicOnline && (music.flags & FLAG_STARTING); }
bool musicReady() { return musicOnline && !(music.flags & (FLAG_NO_CARD | FLAG_STARTING)) && music.trackCount > 0; }
bool btWaiting() { return musicOnline && (music.flags & FLAG_BT_MODE) && !(music.flags & FLAG_BT_CONNECTED); }

// The part of "Folder/Name" after the last slash, and the folder before it
const char* trackBase(const char* path) { const char* s = strrchr(path, '/'); return s ? s + 1 : path; }
void trackFolder(const char* path, char* out, size_t cap) {
  const char* s = strrchr(path, '/');
  size_t n = s ? (size_t)(s - path) : 0;
  if (n > cap - 1) n = cap - 1;
  memcpy(out, path, n);
  out[n] = 0;
  const char* inner = strrchr(out, '/');              // "Artist/Album" -> "Album"
  if (inner) memmove(out, inner + 1, strlen(inner + 1) + 1);
}

void loadNowPlaying() {               // names for the Now playing screen, fetched once per track
  if (!musicReady() || npTrack == music.track) return;
  musicText(CMD_GET_TRACK, music.track, npPath, sizeof(npPath));
  npNext1[0] = npNext2[0] = 0;
  if (music.trackCount > 1) musicText(CMD_GET_TRACK, (music.track + 1) % music.trackCount, npNext1, sizeof(npNext1));
  if (music.trackCount > 2) musicText(CMD_GET_TRACK, (music.track + 2) % music.trackCount, npNext2, sizeof(npNext2));
  npTrack = music.track;
}

// =====================================================================
// Reading screen
// =====================================================================
int bookPercent() {
  if (pageCount <= 1) return pageCount == 1 ? 100 : 0;
  return currentPage * 100 / (pageCount - 1);
}

void drawHeader() {
  char pct[12];
  snprintf(pct, sizeof(pct), "%d%%", bookPercent());
  int pw = textWidth(UI, pct);
  int w = display.width() - 2 * MARGIN_X;
  drawFitted(UI, MARGIN_X, 30, w - pw - 20, bookTitle);
  drawTextRight(UI, display.width() - MARGIN_X, 30, pct);
  // a hairline with the part already read drawn thicker
  display.fillRect(MARGIN_X, 43, w, 2, BLACK);
  display.fillRect(MARGIN_X, 41, w * bookPercent() / 100, 6, BLACK);
}

void drawFooter() {
  int y0 = display.height() - FOOTER_H + 14;
  display.fillRect(MARGIN_X, y0, display.width() - 2 * MARGIN_X, 2, BLACK);
  int base = y0 + 28;
  int x = MARGIN_X;
  if (!musicOnline || musicStarting()) return;         // music switched off (or just on): a clean page
  if (music.flags & FLAG_NO_CARD) { drawText(UI, x, base, "No music card"); return; }

  char right[24];
  snprintf(right, sizeof(right), "vol %d", music.volume);
  int rx = display.width() - MARGIN_X - textWidth(UI, right);
  drawText(UI, rx, base, right);
  if (music.flags & FLAG_BT_MODE) {
    rx -= 20;
    iconBluetooth(rx, base - 15, 16, 2, BLACK);
  }
  if (btWaiting()) {
    drawText(UI, x, base, (music.flags & FLAG_BT_PAIRING) ? "Searching for headphones" : "Connecting to headphones");
    return;
  }
  if (music.flags & FLAG_PLAYING) iconPlay(x, base - 12, 12, BLACK);
  else iconPause(x, base - 12, 12, BLACK);
  x += 20;
  drawFitted(UI, x, base, rx - 14 - x, music.title);
}

void drawReader() {
  display.clearDisplay();
  display.setTextColor(BLACK);
  drawHeader();
  pageEnd = layoutPage(pageStart, true);
  drawFooter();
}

// =====================================================================
// Settings file
// =====================================================================
void applySettings() {
  if (settings.sleepIdx > 3) settings.sleepIdx = 1;
  if (settings.textIdx > 2) settings.textIdx = 1;
  if (settings.refreshIdx > 2) settings.refreshIdx = 1;
  static const GFXfont* const body[] = {&FreeSerif9pt7b, &FreeSerif12pt7b, &FreeSerif18pt7b};
  static const GFXfont* const bold[] = {&FreeSerifBold9pt7b, &FreeSerifBold12pt7b, &FreeSerifBold18pt7b};
  BODY = body[settings.textIdx];
  BOLD = bold[settings.textIdx];
  display.setFullUpdateThreshold(REFRESH_PAGES[settings.refreshIdx]);
}

void loadSettings() {
  SdFile f;
  if (f.open("/books/.settings", O_RDONLY)) {
    char data[48] = {0};
    f.read(data, sizeof(data) - 1);
    f.close();
    int a, b, c;
    if (sscanf(data, "DSET1 %d %d %d", &a, &b, &c) == 3) {
      settings.sleepIdx = a; settings.textIdx = b; settings.refreshIdx = c;
    }
  }
  applySettings();
}

void saveSettings() {
  SdFile f;
  if (!f.open("/books/.settings", O_WRONLY | O_CREAT | O_TRUNC)) return;
  char line[48];
  int n = snprintf(line, sizeof(line), "DSET1 %d %d %d\n", settings.sleepIdx, settings.textIdx, settings.refreshIdx);
  f.write(line, n);
  f.close();
}

// =====================================================================
// Books: positions, open, library list
// =====================================================================
// Every book keeps its own place in /books/.cache/NAME.pos
bool readPos(const char* name, uint32_t* off, int* pct, uint32_t* seq) {
  char path[128];
  cachePath(path, sizeof(path), name, ".pos");
  SdFile f;
  if (!f.open(path, O_RDONLY)) return false;
  char data[64] = {0};
  f.read(data, sizeof(data) - 1);
  f.close();
  unsigned long o, s; int p;
  if (sscanf(data, "DPOS1 %lu %d %lu", &o, &p, &s) != 3) return false;
  if (off) *off = o;
  if (pct) *pct = p;
  if (seq) *seq = s;
  return true;
}

void saveState() {
  stateDirty = false;
  if (!book.isOpen() || !bookName[0]) return;
  SdFile f;
  char line[128];
  if (f.open("/books/.duet", O_WRONLY | O_CREAT | O_TRUNC)) {
    int n = snprintf(line, sizeof(line), "%s\n%lu\n%lu\n", bookName, (unsigned long)pageStart, (unsigned long)readSeq);
    f.write(line, n);
    f.close();
  }
  char path[128];
  cachePath(path, sizeof(path), bookName, ".pos");
  ensureCacheDir();
  if (f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
    int n = snprintf(line, sizeof(line), "DPOS1 %lu %d %lu\n", (unsigned long)pageStart, bookPercent(), (unsigned long)bookSeq);
    f.write(line, n);
    f.close();
  }
  // keep the Library's row for this book current
  for (int k = 0; libLoaded && k < libCount; k++)
    if (strcmp(lib[k].file, bookName) == 0) { lib[k].pct = bookPercent(); lib[k].seq = bookSeq; }
}

// Opens a book at its saved place (or `fallbackOffset` if it has none yet)
bool openBook(const char* name, uint32_t fallbackOffset) {
  if (stateDirty) saveState();
  if (book.isOpen()) book.close();
  char keep[64];
  strncpy(keep, name, sizeof(keep) - 1);               // `name` may point at bookName
  keep[sizeof(keep) - 1] = 0;
  bookName[0] = 0;
  bookAuthor[0] = 0;
  char path[128];
  bool epub = endsWith(keep, ".epub");
  if (epub) {
    if (!prepareEpub(keep)) return false;
    cachePath(path, sizeof(path), keep, ".txt");
  } else {
    snprintf(path, sizeof(path), "/books/%s", keep);
    strncpy(bookTitle, keep, sizeof(bookTitle) - 1);
    bookTitle[sizeof(bookTitle) - 1] = 0;
    char* dot = strrchr(bookTitle, '.'); if (dot) *dot = 0;
  }
  if (!book.open(path, O_RDONLY)) { messageScreen("Can't open this book", keep); waitForKey(); return false; }
  bookSize = book.fileSize();
  strcpy(bookName, keep);
  if (!loadOrBuildIndex() || pageCount == 0) {
    book.close();
    bookName[0] = 0;
    messageScreen("Not enough memory", "for this book's page index");
    waitForKey();
    return false;
  }
  layoutStale = false;
  uint32_t offset = fallbackOffset;
  readPos(keep, &offset, nullptr, nullptr);
  currentPage = pageForOffset(offset);
  pageStart = pages[currentPage];
  bookSeq = ++readSeq;
  saveState();
  return true;
}

bool loadState() {
  SdFile f;
  if (!f.open("/books/.duet", O_RDONLY)) return false;
  char data[128] = {0};
  f.read(data, sizeof(data) - 1);
  f.close();
  char* nl = strchr(data, '\n');
  if (!nl) return false;
  *nl = 0;
  uint32_t off = strtoul(nl + 1, nullptr, 10);
  char* nl2 = strchr(nl + 1, '\n');
  if (nl2) readSeq = strtoul(nl2 + 1, nullptr, 10);
  if (!data[0]) return false;
  return openBook(data, off);
}

// An EPUB's title and author for the Library: from the converted book's .meta if it
// has been opened before, else read from the EPUB once and kept in a small .info file.
bool epubTitle(const char* name, char* title, size_t tcap, char* author, size_t acap) {
  if (readMeta(name, 0, title, tcap, author, acap)) return true;
  char path[128];
  cachePath(path, sizeof(path), name, ".info");
  SdFile f;
  if (f.open(path, O_RDONLY)) {
    char data[200] = {0};
    f.read(data, sizeof(data) - 1);
    f.close();
    char* l1 = strchr(data, '\n');
    char* l2 = l1 ? strchr(l1 + 1, '\n') : nullptr;
    char* l3 = l2 ? strchr(l2 + 1, '\n') : nullptr;
    if (l3 && strncmp(data, "DINF1", 5) == 0) {
      *l2 = 0; *l3 = 0;
      strncpy(title, l1 + 1, tcap - 1); title[tcap - 1] = 0;
      strncpy(author, l2 + 1, acap - 1); author[acap - 1] = 0;
      return title[0] != 0;
    }
  }
  char epubPath[96];
  snprintf(epubPath, sizeof(epubPath), "/books/%s", name);
  SdFile epub;
  if (!epub.open(epubPath, O_RDONLY)) return false;
  SdIn in; in.f = &epub;
  EpubInfo info;
  bool ok = epubReadInfo(in, info) == EPUB_OK && info.title[0];
  epub.close();
  if (!ok) info.title[0] = info.author[0] = 0;       // remember the failure too: don't retry every time
  ensureCacheDir();
  if (f.open(path, O_WRONLY | O_CREAT | O_TRUNC)) {
    char line[200];
    int n = snprintf(line, sizeof(line), "DINF1\n%s\n%s\n", info.title, info.author);
    f.write(line, n);
    f.close();
  }
  if (!ok) return false;
  strncpy(title, info.title, tcap - 1); title[tcap - 1] = 0;
  strncpy(author, info.author, acap - 1); author[acap - 1] = 0;
  return true;
}

int compareBooks(const void* a, const void* b) {       // most recently read first, then by title
  const BookEntry* x = (const BookEntry*)a;
  const BookEntry* y = (const BookEntry*)b;
  if (x->seq != y->seq) return x->seq > y->seq ? -1 : 1;
  return strcasecmp(x->title, y->title);
}

void loadLibrary() {
  if (!lib) lib = (BookEntry*)malloc(MAX_BOOKS * sizeof(BookEntry));
  libCount = 0;
  libLoaded = true;
  if (!lib) return;
  SdFile dir, entry;
  if (!dir.open("/books", O_RDONLY)) return;
  while (libCount < MAX_BOOKS && entry.openNext(&dir, O_RDONLY)) {
    char name[64];
    entry.getName(name, sizeof(name));
    bool isBook = !entry.isDir() && name[0] != '.' && (endsWith(name, ".txt") || endsWith(name, ".epub"));
    entry.close();
    if (!isBook) continue;
    BookEntry& b = lib[libCount++];
    strncpy(b.file, name, sizeof(b.file) - 1); b.file[sizeof(b.file) - 1] = 0;
    b.author[0] = 0;
    char title[96];
    if (!(endsWith(name, ".epub") && epubTitle(name, title, sizeof(title), b.author, sizeof(b.author)))) {
      strncpy(title, name, sizeof(title) - 1); title[sizeof(title) - 1] = 0;
      char* dot = strrchr(title, '.'); if (dot) *dot = 0;
      for (char* p = title; *p; p++) if (*p == '_') *p = ' ';
    }
    asciiClean(title, b.title, sizeof(b.title));
    int pct = -1; uint32_t seq = 0;
    if (!readPos(name, nullptr, &pct, &seq)) pct = -1;
    b.pct = pct; b.seq = seq;
  }
  dir.close();
  qsort(lib, libCount, sizeof(BookEntry), compareBooks);
  libSel = 0;
  for (int k = 0; k < libCount; k++) if (strcmp(lib[k].file, bookName) == 0) libSel = k;
}

const int LIB_ROWS = 10;

// Same layout as the music lists: one row per book, what you are reading marked
void drawLibrary() {
  display.clearDisplay();
  drawTopBar("Books", nullptr);
  int x0 = 28, w = display.width() - 56;
  drawText(BIG_BOLD, 28, 92, "Library");
  char t[40];
  snprintf(t, sizeof(t), libCount == 1 ? "%d book" : "%d books", libCount);
  drawTextRight(UI, display.width() - 28, 92, t);
  display.fillRect(x0, 108, w, 2, BLACK);
  if (libCount == 0) {
    drawText(MENU, 28, 160, "No books yet.");
    drawText(UI, 28, 196, "Copy .epub or .txt files into the /books folder");
    drawText(UI, 28, 220, "on the card in the top slot.");
  }
  int page = libSel / LIB_ROWS;
  for (int r = 0; r < LIB_ROWS; r++) {
    int k = page * LIB_ROWS + r;
    if (k >= libCount) break;
    const BookEntry& b = lib[k];
    int y = 110 + r * 56;
    bool on = k == libSel;
    uint16_t ink = on ? WHITE : BLACK;
    if (on) display.fillRect(x0, y, w, 55, BLACK);
    display.drawFastHLine(x0, y + 55, w, BLACK);
    display.setTextColor(ink);

    // right: progress; the open book says so
    bool open = book.isOpen() && strcmp(b.file, bookName) == 0;
    char st[20];
    if (b.pct < 0) strcpy(st, "New");
    else if (b.pct >= 100) strcpy(st, "Finished");
    else snprintf(st, sizeof(st), "%d%%", b.pct);
    const GFXfont* rf = open ? UI_BOLD : UI;
    int rightW = textWidth(rf, st);
    drawTextRight(rf, x0 + w - 14, y + 35, st);
    if (open) {                                           // a small open book
      int ix = x0 + w - 14 - rightW - 24, iy = y + 23;
      boxOutline(ix, iy, 9, 13, 2, ink);
      boxOutline(ix + 8, iy, 9, 13, 2, ink);
      rightW += 24;
    }
    // left: title, then the author in small type if there is room
    int maxW = w - 28 - rightW - 14;
    int used = drawFitted(MENU, x0 + 14, y + 36, maxW, b.title);
    if (b.author[0] && maxW - used > 90) drawFitted(UI, x0 + 14 + used + 14, y + 36, maxW - used - 14, b.author);
  }
  display.setTextColor(BLACK);
  pageIndicator(page, (libCount + LIB_ROWS - 1) / LIB_ROWS, 700);
  drawHints("[L][R] move", "hold [R] open", "hold [L] back");
}

// =====================================================================
// Home
// =====================================================================
void drawHome() {
  display.clearDisplay();
  drawTopBar("Home", nullptr);
  drawText(TITLE, 32, 108, "Duet");
  char sub[3][96];
  if (book.isOpen()) {
    char t[72];
    fitText(UI, bookTitle, 300, t, sizeof(t));
    snprintf(sub[0], sizeof(sub[0]), "%s  -  %d%%", t, bookPercent());
  } else strcpy(sub[0], "Choose a book");
  if (!musicOnline) strcpy(sub[1], "Off - flip the music switch");
  else if (music.flags & FLAG_NO_CARD) strcpy(sub[1], "No music card");
  else if (musicStarting() && !(music.flags & FLAG_BT_MODE)) strcpy(sub[1], "Starting...");
  else if (btWaiting()) strcpy(sub[1], (music.flags & FLAG_BT_PAIRING) ? "Searching for headphones" : "Connecting to headphones");
  else snprintf(sub[1], sizeof(sub[1]), "%s  -  %s", (music.flags & FLAG_PLAYING) ? "Playing" : "Paused", music.title);
  strcpy(sub[2], "Output, headphones, sleep, text size");
  const char* names[3] = {"Books", "Music", "Settings"};

  for (int k = 0; k < 3; k++) {
    int x = 32, w = display.width() - 64, y = 140 + k * 172, h = 156;
    bool on = k == homeSel;
    uint16_t ink = on ? WHITE : BLACK;
    if (on) display.fillRect(x, y, w, h, BLACK);
    else boxOutline(x, y, w, h, 2, BLACK);
    if (k == 0) iconBook(x + 24, y + 58, ink);
    if (k == 1) iconNote(x + 24, y + 54, ink);
    if (k == 2) iconSliders(x + 24, y + 58, ink, on ? BLACK : WHITE);
    display.setTextColor(ink);
    drawText(BIG_BOLD, x + 94, y + 78, names[k]);
    drawFitted(UI, x + 94, y + 110, w - 94 - 56, sub[k]);
    iconChevron(x + w - 36, y + h / 2 - 12, 24, ink);
  }
  display.setTextColor(BLACK);
  drawHints("[L][R] move", "hold [R] open", book.isOpen() ? "hold [L] back to page" : "");
}

// =====================================================================
// Music: track list
// =====================================================================
void loadTrackPage(int firstOnPage) {
  if (tlPage == firstOnPage) return;
  for (int r = 0; r < TRACK_ROWS; r++) {
    int k = firstOnPage + r;
    tlNames[r][0] = 0;
    if (k < tlFirst + tlCount) musicText(CMD_GET_TRACK, k, tlNames[r], sizeof(tlNames[r]));
  }
  tlPage = firstOnPage;
}

void drawMusicUnavailable(const char* heading, const char* line1, const char* line2) {
  drawTextCenter(MENU_BOLD, display.width() / 2, 330, heading);
  drawTextCenter(UI, display.width() / 2, 370, line1);
  if (line2) drawTextCenter(UI, display.width() / 2, 396, line2);
}

bool musicProblem() {                 // nothing to show but a message
  if (!musicOnline || (music.flags & FLAG_NO_CARD)) return true;
  return musicStarting() && !(music.flags & FLAG_BT_MODE);   // in Bluetooth mode the connecting screen covers it
}
bool drawMusicProblem() {             // true if it drew a "no music" message instead of a screen
  if (!musicOnline) { drawMusicUnavailable("Music is off", "Flip the music switch on the bottom edge.", "It takes a few seconds to start."); return true; }
  if (music.flags & FLAG_NO_CARD) { drawMusicUnavailable("No music found", "Put MP3 files in a /music folder on the card", "in the side slot, then switch music off and on."); return true; }
  if (musicProblem()) { drawMusicUnavailable("Music is starting", "Reading the music card...", nullptr); return true; }
  return false;
}

// The Shuffle row that heads the album list (or the track list of a card without folders)
void drawShuffleRow(int x0, int w, bool on) {
  uint16_t ink = on ? WHITE : BLACK;
  if (on) display.fillRect(x0, 110, w, 54, BLACK);
  display.fillRect(x0, 164, w, 2, BLACK);
  display.setTextColor(ink);
  thickLine(x0 + 14, 128, x0 + 34, 146, 2, ink);          // crossed arrows
  thickLine(x0 + 14, 146, x0 + 34, 128, 2, ink);
  drawText(MENU_BOLD, x0 + 48, 145, "Shuffle");
  drawTextRight(MENU, x0 + w - 14, 145, (music.flags & FLAG_SHUFFLE) ? "On" : "Off");
  display.setTextColor(BLACK);
}

void drawTracks() {
  display.clearDisplay();
  drawTopBar("Music", nullptr);
  if (drawMusicProblem()) { drawHints("hold [L] back", "", ""); return; }
  bool wholeCard = albumTotal <= 1;                        // no folders: one list with the Shuffle row on top
  if (tlCount == 0 || tlFirst + tlCount > music.trackCount) { tlFirst = 0; tlCount = music.trackCount; tlAlbum[0] = 0; tlPage = -1; }
  if (tlSel >= tlFirst + tlCount) tlSel = tlFirst + tlCount - 1;
  if (tlSel < tlFirst && !(wholeCard && tlSel == -1)) tlSel = tlFirst;
  int x0 = 28, w = display.width() - 56;
  char t[40];
  snprintf(t, sizeof(t), tlCount == 1 ? "%d track" : "%d tracks", tlCount);
  drawTextRight(UI, display.width() - 28, 92, t);
  drawFitted(BIG_BOLD, 28, 92, w - textWidth(UI, t) - 20, wholeCard ? "All tracks" : tlAlbum[0] ? tlAlbum : "Loose tracks");
  display.fillRect(x0, 108, w, 2, BLACK);
  int top = 110;                                           // rows start here; lower when the Shuffle row is shown
  if (wholeCard) { drawShuffleRow(x0, w, tlSel < 0); top = 166; }

  int sel = tlSel < 0 ? tlFirst : tlSel;
  int page = (sel - tlFirst) / TRACK_ROWS;
  int firstOnPage = tlFirst + page * TRACK_ROWS;
  loadTrackPage(firstOnPage);
  char prevFolder[DUET_TEXT_LEN] = "\x01";
  for (int r = 0; r < TRACK_ROWS; r++) {
    int k = firstOnPage + r;
    if (k >= tlFirst + tlCount) break;
    int y = top + r * 56;
    bool on = k == tlSel;
    uint16_t ink = on ? WHITE : BLACK;
    if (on) display.fillRect(x0, y, w, 55, BLACK);
    display.drawFastHLine(x0, y + 55, w, BLACK);
    display.setTextColor(ink);
    const char* name = trackBase(tlNames[r]);
    int nameX = x0 + 14;
    if (!isdigit((unsigned char)name[0])) {              // names without their own number get the list position
      char n[12];
      snprintf(n, sizeof(n), "%02d", k - tlFirst + 1);
      drawText(UI, x0 + 14, y + 35, n);
      nameX += textWidth(UI, "000") + 10;
    }
    int rightW = 0;
    char folder[DUET_TEXT_LEN];
    trackFolder(tlNames[r], folder, sizeof(folder));
    if (k == music.track) {                                 // the track that is loaded now
      const char* tag = (music.flags & FLAG_PLAYING) ? "playing" : "paused";
      rightW = textWidth(UI_BOLD, tag) + 18;
      drawTextRight(UI_BOLD, x0 + w - 14, y + 35, tag);
      if (music.flags & FLAG_PLAYING) iconPlay(x0 + w - 14 - rightW + 2, y + 24, 11, ink);
      else iconPause(x0 + w - 14 - rightW + 2, y + 24, 11, ink);
    } else if (wholeCard && folder[0] && strcmp(folder, prevFolder) != 0) {   // first row of a folder: name it
      char f[40];
      fitText(UI, folder, 170, f, sizeof(f));
      rightW = textWidth(UI, f);
      drawTextRight(UI, x0 + w - 14, y + 35, f);
    }
    strcpy(prevFolder, folder);
    drawFitted(MENU, nameX, y + 36, x0 + w - 14 - rightW - 14 - nameX, name);
  }
  display.setTextColor(BLACK);
  pageIndicator(page, (tlCount + TRACK_ROWS - 1) / TRACK_ROWS, 700);
  drawHints("[L][R] move", tlSel < 0 ? "hold [R] switch" : "hold [R] play", "hold [L] back");
}

// =====================================================================
// Music: albums (the folders on the music card)
// =====================================================================
void loadAlbumPage(int page) {
  if (alPage == page) return;
  for (int r = 0; r < TRACK_ROWS; r++) {
    int k = page * TRACK_ROWS + r;
    alRows[r].name[0] = 0;
    alRows[r].first = alRows[r].count = 0;
    DuetFolder f;
    if (k < albumTotal && musicFolder(k, f)) {
      strncpy(alRows[r].name, f.text, DUET_FOLDER_LEN - 1);
      alRows[r].name[DUET_FOLDER_LEN - 1] = 0;
      alRows[r].first = f.first;
      alRows[r].count = f.count;
    }
  }
  alPage = page;
}

void drawAlbums() {
  display.clearDisplay();
  drawTopBar("Music", nullptr);
  if (drawMusicProblem()) { drawHints("hold [L] back", "", ""); return; }
  if (albumTotal < 0) {                                    // the audio board restarted: ask again
    DuetFolder f;
    bool ok = musicFolder(DUET_FOLDER_CURRENT, f);
    albumTotal = ok ? f.folders : 0;
    alSel = ok ? f.folder : -1;
    alPage = -1;
  }
  if (alSel >= albumTotal) alSel = albumTotal - 1;
  int x0 = 28, w = display.width() - 56;
  drawText(BIG_BOLD, 28, 92, "Albums");
  char t[40];
  snprintf(t, sizeof(t), "%d albums  -  %d tracks", albumTotal, music.trackCount);
  drawTextRight(UI, display.width() - 28, 92, t);
  display.fillRect(x0, 108, w, 2, BLACK);
  drawShuffleRow(x0, w, alSel < 0);

  int page = (alSel < 0 ? 0 : alSel) / TRACK_ROWS;
  loadAlbumPage(page);
  for (int r = 0; r < TRACK_ROWS; r++) {
    int k = page * TRACK_ROWS + r;
    if (k >= albumTotal) break;
    const AlbumRow& a = alRows[r];
    int y = 166 + r * 56;
    bool on = k == alSel;
    uint16_t ink = on ? WHITE : BLACK;
    if (on) display.fillRect(x0, y, w, 55, BLACK);
    display.drawFastHLine(x0, y + 55, w, BLACK);
    display.setTextColor(ink);
    char right[24];
    bool current = a.count && music.track >= a.first && music.track < a.first + a.count;
    if (current) strcpy(right, (music.flags & FLAG_PLAYING) ? "playing" : "paused");
    else snprintf(right, sizeof(right), a.count == 1 ? "%d track" : "%d tracks", a.count);
    const GFXfont* rf = current ? UI_BOLD : UI;
    int rightW = textWidth(rf, right);
    drawTextRight(rf, x0 + w - 14, y + 35, right);
    if (current) {
      rightW += 18;
      if (music.flags & FLAG_PLAYING) iconPlay(x0 + w - 14 - rightW + 2, y + 24, 11, ink);
      else iconPause(x0 + w - 14 - rightW + 2, y + 24, 11, ink);
    }
    drawFitted(MENU, x0 + 14, y + 36, w - 28 - rightW - 14, a.name[0] ? a.name : "Loose tracks");
  }
  display.setTextColor(BLACK);
  pageIndicator(page, (albumTotal + TRACK_ROWS - 1) / TRACK_ROWS, 700);
  drawHints("[L][R] move", alSel < 0 ? "hold [R] switch" : "hold [R] open", "hold [L] back");
}

// Shows one album's tracks (or the whole card when count covers it)
void showTracks(int first, int count, const char* album) {
  tlFirst = first;
  tlCount = count;
  strncpy(tlAlbum, album, sizeof(tlAlbum) - 1);
  tlAlbum[sizeof(tlAlbum) - 1] = 0;
  tlPage = -1;
  tlSel = (music.track >= first && music.track < first + count) ? music.track : first;
}

// Where "browse" lands: the album list with the playing album selected, or straight
// in the track list when the card has no folders.
DuetScreen musicBrowser() {
  if (!musicReady()) return SCR_TRACKS;                   // shows the reason
  DuetFolder f;
  if (musicFolder(DUET_FOLDER_CURRENT, f) && f.folders > 1) {
    albumTotal = f.folders;
    alSel = f.folder;
    return SCR_ALBUMS;
  }
  albumTotal = 1;
  showTracks(0, music.trackCount, "");
  return SCR_TRACKS;
}

// =====================================================================
// Music: Now playing (and the Bluetooth screen while headphones connect)
// =====================================================================
void formatTime(uint16_t s, char* out, size_t cap) { snprintf(out, cap, "%d:%02d", s / 60, s % 60); }

void drawBluetoothWait() {
  bool pairing = music.flags & FLAG_BT_PAIRING;
  musicText(CMD_GET_BT_NAME, 0, btName, sizeof(btName));
  int cx = display.width() / 2;
  for (int k = 0; k < 3; k++) display.drawCircle(cx, 150, 70 - k, BLACK);
  iconBluetooth(cx - 20, 118, 64, 3, BLACK);
  if (btName[0]) {
    drawTextCenter(MENU, cx, 270, "Connecting to");
    char t[64];
    fitText(BIG_BOLD, btName, display.width() - 88, t, sizeof(t));
    drawTextCenter(BIG_BOLD, cx, 318, t);
  } else {
    drawTextCenter(BIG_BOLD, cx, 300, "Looking for headphones");
  }
  const char* steps[3];
  if (pairing) {
    steps[0] = "Turn your headphones on.";
    steps[1] = "Put them in pairing mode.";
    steps[2] = "Hold them next to Duet. It connects by itself.";
  } else {
    steps[0] = "Turn your headphones on.";
    steps[1] = "They reconnect by themselves.";
    steps[2] = "A different pair? Hold R to pair it.";
  }
  int x0 = 44, w = display.width() - 88;
  display.fillRect(x0, 356, w, 2, BLACK);
  for (int k = 0; k < 3; k++) {
    int y = 398 + k * 50;
    for (int j = 0; j < 2; j++) display.drawCircle(x0 + 15, y - 7, 15 - j, BLACK);
    char n[2] = {(char)('1' + k), 0};
    drawTextCenter(UI_BOLD, x0 + 15, y - 1, n);
    drawFitted(MENU, x0 + 44, y, w - 44, steps[k]);
  }
  display.fillRect(x0, 524, w, 2, BLACK);
  drawTextCenter(UI, cx, 562, pairing ? "This can take half a minute. Pairing is only needed once."
                                      : "This can take 20 seconds. Music starts when they connect.");
  drawHints("hold [L] use the jack", "hold [R] pair new", "");
}

void drawPlaying() {
  display.clearDisplay();
  if (btWaiting() && !(music.flags & FLAG_NO_CARD)) { drawTopBar("Music", nullptr); drawBluetoothWait(); return; }
  if (drawMusicProblem()) { drawTopBar("Music", nullptr); drawHints("hold [L] back", "", ""); return; }
  drawTopBar("Now playing", nullptr);
  loadNowPlaying();
  int x0 = 32, w = display.width() - 64;
  char t[64];

  // album (folder), position in the list, title
  char folder[DUET_TEXT_LEN];
  trackFolder(npPath, folder, sizeof(folder));
  for (char* p = folder; *p; p++) *p = toupper((unsigned char)*p);
  if (folder[0]) drawFitted(UI_BOLD, x0, 78, w, folder);
  snprintf(t, sizeof(t), "Track %d of %d%s", music.track + 1, music.trackCount, (music.flags & FLAG_SHUFFLE) ? "  -  shuffle" : "");
  drawText(UI, x0, folder[0] ? 102 : 90, t);
  drawWrapped(TITLE, music.title, x0, 160, w, 2, false);

  // progress
  boxOutline(x0, 242, w, 16, 2, BLACK);
  display.fillRect(x0, 242, (int)((long)w * music.progress / 255), 16, BLACK);
  formatTime(music.elapsed, t, sizeof(t));
  drawText(UI, x0, 282, t);
  if (music.duration) { formatTime(music.duration, t, sizeof(t)); drawTextRight(UI, x0 + w, 282, t); }

  // what the three chin keys do
  bool playing = music.flags & FLAG_PLAYING;
  int bw = (w - 24) / 3;
  const char* labels[3] = {"previous", playing ? "pause" : "play", "next"};
  for (int k = 0; k < 3; k++) {
    int bx = x0 + k * (bw + 12), by = 304;
    bool mid = k == 1;
    uint16_t ink = mid ? WHITE : BLACK;
    if (mid) display.fillRect(bx, by, bw, 84, BLACK);
    else boxOutline(bx, by, bw, 84, 2, BLACK);
    int cx = bx + bw / 2;
    if (k == 0) iconSkip(cx - 16, by + 16, 22, false, ink);
    if (k == 2) iconSkip(cx - 16, by + 16, 22, true, ink);
    if (k == 1) { if (playing) iconPause(cx - 9, by + 15, 24, ink); else iconPlay(cx - 9, by + 15, 24, ink); }
    display.setTextColor(ink);
    drawTextCenter(UI, cx, by + 66, labels[k]);
  }
  display.setTextColor(BLACK);
  drawTextCenter(UI, display.width() / 2, 412, "The three keys below the screen. Hold the outer two for volume.");

  // volume
  drawText(UI_BOLD, x0, 454, "Volume");
  int vx = x0 + 84, vw = w - 84 - 56;
  int seg = (vw - 9 * 5) / 10;
  for (int k = 0; k < 10; k++) {
    int sx = vx + k * (seg + 5);
    if (k < music.volume) display.fillRect(sx, 438, seg, 18, BLACK);
    else boxOutline(sx, 438, seg, 18, 2, BLACK);
  }
  snprintf(t, sizeof(t), "%d/10", music.volume);
  drawTextRight(UI, x0 + w, 454, t);

  // output
  boxOutline(x0, 476, w, 50, 2, BLACK);
  if (music.flags & FLAG_BT_MODE) {
    musicText(CMD_GET_BT_NAME, 0, btName, sizeof(btName));
    iconBluetooth(x0 + 14, 490, 22, 2, BLACK);
    drawText(UI_BOLD, x0 + 42, 507, "Bluetooth");
    drawTextRight(UI, x0 + w - 14, 507, "connected");
    int nx = x0 + 42 + textWidth(UI_BOLD, "Bluetooth") + 14;
    drawFitted(UI, nx, 507, x0 + w - 14 - textWidth(UI, "connected") - 14 - nx, btName);
  } else {
    drawText(UI_BOLD, x0 + 14, 507, "Headphone jack");
    drawTextRight(UI, x0 + w - 14, 507, "change in Settings");
  }

  // up next
  drawText(UI_BOLD, x0, 562, "UP NEXT");
  display.fillRect(x0, 572, w, 2, BLACK);
  if (music.flags & FLAG_SHUFFLE) {
    drawText(MENU, x0, 606, "Shuffle picks the next track");
  } else {
    const char* nx[2] = {npNext1, npNext2};
    for (int k = 0; k < 2; k++) {
      if (!nx[k][0]) break;
      int y = 606 + k * 44;
      drawFitted(MENU, x0, y, w, trackBase(nx[k]));
      if (k == 0 && npNext2[0]) display.drawFastHLine(x0, y + 16, w, BLACK);
    }
  }
  drawHints("[L][R] track", "hold [L] browse", "hold [L][R] home");
}

// =====================================================================
// Settings
// =====================================================================
const int SET_ROWS = 8;

void drawSettings() {
  display.clearDisplay();
  drawTopBar("Settings", " ");
  drawText(BIG_BOLD, 28, 92, "Settings");
  int x0 = 28, w = display.width() - 56;
  display.fillRect(x0, 108, w, 2, BLACK);
  updateBattery(true);
  if (musicOnline && (music.flags & (FLAG_BT_KNOWN | FLAG_BT_MODE))) musicText(CMD_GET_BT_NAME, 0, btName, sizeof(btName));
  else btName[0] = 0;

  for (int k = 0; k < SET_ROWS; k++) {
    const char* label = "";
    const char* note = nullptr;
    char value[48] = "";
    bool changeable = false;
    switch (k) {
      case 0:
        label = "Audio output";
        if (musicOnline) { strcpy(value, musicSwitching() ? "Switching..." : (music.flags & FLAG_BT_MODE) ? "Bluetooth" : "Headphone jack"); changeable = true; note = "Switching restarts the music board"; }
        else strcpy(value, "Music is off");
        break;
      case 1:
        label = "Headphones";
        if (!musicOnline) strcpy(value, "-");
        else {
          if (btName[0]) fitText(MENU, btName, 240, value, sizeof(value));
          else strcpy(value, (music.flags & FLAG_BT_PAIRING) ? "Searching..." : "None yet");
          changeable = true;
          note = "Hold R to pair new Bluetooth headphones";
        }
        break;
      case 2: label = "Sleep after"; snprintf(value, sizeof(value), "%d min", SLEEP_MINUTES[settings.sleepIdx]); changeable = true; break;
      case 3: label = "Text size"; strcpy(value, TEXT_SIZE_NAMES[settings.textIdx]); changeable = true; break;
      case 4: label = "Full refresh every"; snprintf(value, sizeof(value), "%d pages", REFRESH_PAGES[settings.refreshIdx]); changeable = true; note = "Clears e-ink ghosting"; break;
      case 5:
        label = "Battery";
        if (battPct >= 0) snprintf(value, sizeof(value), "%d%%  -  %d.%02d V", battPct, (int)battVolts, (int)(battVolts * 100) % 100);
        else strcpy(value, "Not connected");
        break;
      case 6:
        label = "Music card";
        if (!musicOnline) strcpy(value, "Music is off");
        else if (musicStarting()) strcpy(value, "Reading...");
        else if (music.flags & FLAG_NO_CARD) strcpy(value, "No MP3s found");
        else snprintf(value, sizeof(value), "%d tracks", music.trackCount);
        break;
      case 7: label = "About"; snprintf(value, sizeof(value), "Duet %s", DUET_VERSION); break;
    }
    int y = 110 + k * 70;
    bool on = k == setSel;
    uint16_t ink = on ? WHITE : BLACK;
    if (on) display.fillRect(x0, y, w, 69, BLACK);
    display.drawFastHLine(x0, y + 69, w, BLACK);
    display.setTextColor(ink);
    drawText(MENU_BOLD, x0 + 14, note ? y + 32 : y + 43, label);
    if (note) drawText(UI, x0 + 14, y + 56, note);
    int rx = x0 + w - 14;
    if (changeable) { iconChevron(rx - 8, note ? y + 17 : y + 28, 16, ink); rx -= 22; }
    drawTextRight(MENU, rx, note ? y + 32 : y + 43, value);
  }
  display.setTextColor(BLACK);
  drawHints("[L][R] move", "hold [R] change", "hold [L] back");
}

void changeSetting(int row) {
  switch (row) {
    case 0: if (musicOnline && !musicSwitching()) sendMusicRestart(CMD_SET_OUTPUT, (music.flags & FLAG_BT_MODE) ? 0 : 1); break;
    case 1: if (musicOnline && !musicSwitching()) { sendMusicRestart(CMD_BT_PAIR, 0); screen = SCR_PLAYING; } break;   // shows the pairing steps
    case 2: settings.sleepIdx = (settings.sleepIdx + 1) % 4; break;
    case 3: settings.textIdx = (settings.textIdx + 1) % 3; layoutStale = true; break;
    case 4: settings.refreshIdx = (settings.refreshIdx + 1) % 3; break;
    default: return;
  }
  if (row >= 2) { applySettings(); saveSettings(); }
  dirty = true;
}

// =====================================================================
// Screens: switching and drawing
// =====================================================================
void gotoScreen(DuetScreen s) {
  if (stateDirty) saveState();
  if (s == SCR_READER && !book.isOpen()) s = SCR_LIBRARY;
  if (s == SCR_READER && layoutStale) {                // the text size changed: find the same place again
    uint32_t at = pageStart;
    if (loadOrBuildIndex() && pageCount > 0) {
      currentPage = pageForOffset(at);
      pageStart = pages[currentPage];
    }
    layoutStale = false;
    needFull = true;
  }
  if (s == SCR_LIBRARY && !libLoaded) loadLibrary();
  if (s == SCR_READER || screen == SCR_READER) needFull = true;   // a text page leaves ghosts: flash it clean
  screen = s;
  dirty = true;
}

void drawScreen() {
  display.setTextColor(BLACK);
  switch (screen) {
    case SCR_READER:   drawReader(); break;
    case SCR_HOME:     drawHome(); break;
    case SCR_LIBRARY:  drawLibrary(); break;
    case SCR_PLAYING:  drawPlaying(); break;
    case SCR_TRACKS:   drawTracks(); break;
    case SCR_ALBUMS:   drawAlbums(); break;
    case SCR_SETTINGS: drawSettings(); break;
  }
  pushScreen();
}

// The keys can't be read while the panel redraws, but the IO expander latches a key
// that goes down. After a redraw, turn such a press into a tap (if the key is up
// again, and wasn't already held when the redraw began).
void redraw() {
  if (!CATCH_TAPS_DURING_REFRESH) { drawScreen(); return; }
  bool lBefore = readButton(BTN_BACK), rBefore = readButton(BTN_FWD);   // reading also clears the latch
  drawScreen();
  uint16_t went = display.expander2.getInt();
  readButton(BTN_BACK);                                // a latched input reads "down" once more: discard
  bool l = readButton(BTN_BACK), r = readButton(BTN_FWD);
  bool tapL = (went & (1 << BTN_BACK)) && !lBefore && !l;
  bool tapR = (went & (1 << BTN_FWD)) && !rBefore && !r;
  if (tapL != tapR) pendingTap = tapR ? EV_R : EV_L;
}

// =====================================================================
// Side keys
// =====================================================================
// Short presses are reported on release; a hold is reported as soon as it has
// lasted HOLD_MS, so the screen reacts while the key is still down.
DuetEvent pollInput() {
  static bool active = false, fired = false, sawL = false, sawR = false;
  static uint32_t t0 = 0;
  bool l = readButton(BTN_BACK), r = readButton(BTN_FWD);
  uint32_t now = millis();
  if (!active) {
    if (l || r) { active = true; fired = false; sawL = l; sawR = r; t0 = now; }
    return EV_NONE;
  }
  sawL |= l; sawR |= r;
  if (!l && !r) {
    active = false;
    if (fired || now - t0 < 30) return EV_NONE;        // already handled, or contact bounce
    if (sawL && sawR) return EV_NONE;                  // both tapped: nothing
    return sawR ? EV_R : EV_L;
  }
  if (!fired && now - t0 >= HOLD_MS) {
    fired = true;
    if (l && r) return EV_HOLD_BOTH;
    return l ? EV_HOLD_L : EV_HOLD_R;
  }
  return EV_NONE;
}

void turnPage(int by) {
  int target = currentPage + by;
  if (target < 0 || target >= pageCount) return;
  currentPage = target;
  pageStart = pages[currentPage];
  stateDirty = true;
  dirty = true;
}

void moveSel(int& sel, int by, int lo, int hi) {       // wraps around
  if (hi < lo) return;
  sel += by;
  if (sel > hi) sel = lo;
  if (sel < lo) sel = hi;
  dirty = true;
}

void handleEvent(DuetEvent ev) {
  if (ev == EV_HOLD_BOTH) { gotoScreen(SCR_HOME); return; }
  int step = ev == EV_R ? 1 : ev == EV_L ? -1 : 0;
  switch (screen) {
    case SCR_READER:
      if (step) turnPage(step);
      else if (ev == EV_HOLD_L) gotoScreen(SCR_LIBRARY);
      else if (ev == EV_HOLD_R) gotoScreen(SCR_PLAYING);
      break;

    case SCR_HOME:
      if (step) moveSel(homeSel, step, 0, 2);
      else if (ev == EV_HOLD_R) gotoScreen(homeSel == 0 ? SCR_LIBRARY : homeSel == 1 ? SCR_PLAYING : SCR_SETTINGS);
      else if (ev == EV_HOLD_L && book.isOpen()) gotoScreen(SCR_READER);
      break;

    case SCR_LIBRARY:
      if (step) moveSel(libSel, step, 0, libCount - 1);
      else if (ev == EV_HOLD_L) gotoScreen(SCR_HOME);
      else if (ev == EV_HOLD_R && libCount > 0) {
        if (strcmp(lib[libSel].file, bookName) == 0 && book.isOpen()) { gotoScreen(SCR_READER); break; }
        char name[64];
        strcpy(name, lib[libSel].file);
        bool ok = openBook(name, 0);
        libLoaded = false;                             // titles, order and progress have changed
        lastActivity = millis();
        needFull = true;
        if (ok) gotoScreen(SCR_READER);
        else { loadLibrary(); dirty = true; }
      }
      break;

    case SCR_PLAYING:
      if (btWaiting() && !(music.flags & FLAG_NO_CARD)) {   // the Bluetooth screen is showing
        if (ev == EV_HOLD_L) sendMusicRestart(CMD_SET_OUTPUT, 0);
        else if (ev == EV_HOLD_R) sendMusicRestart(CMD_BT_PAIR, 0);
        break;
      }
      if (musicProblem()) { if (ev == EV_HOLD_L) gotoScreen(SCR_HOME); break; }
      if (step) sendMusic(step > 0 ? CMD_NEXT : CMD_PREV);
      else if (ev == EV_HOLD_L) gotoScreen(musicBrowser());
      break;

    case SCR_ALBUMS:
      if (!musicReady()) { if (ev == EV_HOLD_L) gotoScreen(SCR_HOME); break; }
      if (step) moveSel(alSel, step, -1, albumTotal - 1);
      else if (ev == EV_HOLD_L) gotoScreen(SCR_HOME);
      else if (ev == EV_HOLD_R) {
        if (alSel < 0) { sendMusicArg(CMD_SHUFFLE, (music.flags & FLAG_SHUFFLE) ? 0 : 1); break; }
        loadAlbumPage(alSel / TRACK_ROWS);
        const AlbumRow& a = alRows[alSel % TRACK_ROWS];
        if (a.count) { showTracks(a.first, a.count, a.name); gotoScreen(SCR_TRACKS); }
      }
      break;

    case SCR_TRACKS: {
      bool wholeCard = albumTotal <= 1;
      if (!musicReady()) { if (ev == EV_HOLD_L) gotoScreen(SCR_HOME); break; }
      if (step) moveSel(tlSel, step, wholeCard ? -1 : tlFirst, tlFirst + tlCount - 1);
      else if (ev == EV_HOLD_L) gotoScreen(wholeCard ? SCR_HOME : SCR_ALBUMS);
      else if (ev == EV_HOLD_R) {
        if (tlSel < 0) { sendMusicArg(CMD_SHUFFLE, (music.flags & FLAG_SHUFFLE) ? 0 : 1); }
        else { sendMusicArg(CMD_PLAY_INDEX, tlSel); gotoScreen(SCR_PLAYING); }
      }
      break;
    }

    case SCR_SETTINGS:
      if (step) moveSel(setSel, step, 0, SET_ROWS - 1);
      else if (ev == EV_HOLD_L) gotoScreen(SCR_HOME);
      else if (ev == EV_HOLD_R) changeSetting(setSel);
      break;
  }
}

// Does a change on the audio board show on the current screen?
bool musicChangeMatters(uint8_t ch) {
  static uint32_t lastTimeDraw = 0;
  switch (screen) {
    case SCR_READER:   return !musicStarting() && (ch & (CH_STATE | CH_TRACK));   // the footer stays empty until it is ready
    case SCR_HOME:     return ch & (CH_STATE | CH_TRACK);
    case SCR_TRACKS:
    case SCR_ALBUMS:   return ch & (CH_STATE | CH_TRACK);
    case SCR_SETTINGS: return ch & CH_STATE;
    case SCR_PLAYING:
      if (ch & (CH_STATE | CH_TRACK)) { lastTimeDraw = millis(); return true; }
      // the progress bar: e-ink can't tick every second, so move it every 15 s
      if ((ch & CH_TIME) && millis() - lastTimeDraw > 15000) { lastTimeDraw = millis(); return true; }
      return false;
    default: return false;
  }
}

// =====================================================================
// Sleep
// =====================================================================
// Drawn in 8-level grey so it can carry the Duet logo. It is one full refresh
// and the panel keeps it with the power off; waking starts again in black and white.
void drawSleepScreen() {
  const uint16_t INK = 0;                               // grey mode: 0 = black ... 7 = white
  display.selectDisplayMode(INKPLATE_3BIT);
  display.clearDisplay();
  display.setTextColor(INK);
  int W = display.width(), H = display.height(), cx = W / 2;
  int lx = cx - DUET_LOGO_W / 2;
  if (book.isOpen()) {
    display.image.draw(duet_logo, lx, 76, DUET_LOGO_W, DUET_LOGO_H);
    drawTextCenter(UI_BOLD, cx, 336, "N O W   R E A D I N G");
    int lines = drawWrapped(TITLE, bookTitle, 80, 396, W - 160, 2, true);
    int y = 396 + (lines - 1) * TITLE->yAdvance + 44;
    if (bookAuthor[0]) {
      char a[64];
      fitText(MENU, bookAuthor, W - 180, a, sizeof(a));
      drawTextCenter(MENU, cx, y, a);
      y += 40;
    }
    boxOutline(cx - 120, y, 240, 12, 2, INK);
    display.fillRect(cx - 120, y, 240 * bookPercent() / 100, 12, INK);
    char t[24];
    snprintf(t, sizeof(t), "%d%% read", bookPercent());
    drawTextCenter(UI, cx, y + 38, t);
  } else {
    display.image.draw(duet_logo, lx, 200, DUET_LOGO_W, DUET_LOGO_H);
  }
  drawTextCenter(TITLE_MED, cx, H - 150, "Asleep");
  drawTextCenter(UI, cx, H - 118, "Press WAKE on the bottom edge");
  updateBattery(true);
  if (battPct >= 0) {
    char p[12];
    snprintf(p, sizeof(p), "%d%%", battPct);
    int w = 32 + textWidth(UI, p);
    iconBattery(cx - w / 2, H - 96, battPct, INK);
    drawText(UI, cx - w / 2 + 32, H - 83, p);
  }
  display.display();
}

void goToSleep() {
  saveState();
  drawSleepScreen();                  // stays on the panel with the power off
  if (book.isOpen()) book.close();
  display.sdCardSleep();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);  // Inkplate's WAKE button
  esp_deep_sleep_start();
}

// =====================================================================
// Splash: the Duet logo in 8-level grey, shown on power-on only (not when
// waking from sleep). Needs one full refresh in 3-bit mode, then we switch
// back to fast 1-bit mode for everything else.
// =====================================================================
const uint32_t SPLASH_MIN_MS = 1500;   // keep it up at least this long

void showSplash() {
  display.selectDisplayMode(INKPLATE_3BIT);
  display.clearDisplay();
  int x = (display.width() - DUET_LOGO_W) / 2;
  int y = (display.height() - DUET_LOGO_H) / 2 - 30;
  display.image.draw(duet_logo, x, y, DUET_LOGO_W, DUET_LOGO_H);
  display.setFont(UI);
  display.setTextColor(2);                         // dark grey (0 = black, 7 = white)
  char v[16];
  snprintf(v, sizeof(v), "v%s", DUET_VERSION);
  display.setCursor((display.width() - textWidth(UI, v)) / 2, display.height() - 40);
  display.print(v);
  display.display();
  display.selectDisplayMode(INKPLATE_1BIT);
  display.setTextColor(BLACK);
  screenFresh = true;                              // the next screen must fully replace the grey
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  display.begin();
  display.setRotation(ROTATION);
  display.setTextWrap(false);

  const bool coldBoot = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0;
  const uint32_t bootStart = millis();
  if (coldBoot) showSplash();

  display.expander2.pinMode(BTN_BACK, INPUT_PULLUP);
  display.expander2.pinMode(BTN_FWD, INPUT_PULLUP);
  if (CATCH_TAPS_DURING_REFRESH) {                 // latch a press until the port is next read
    display.expander2.setIntPin(BTN_BACK, FALLING);
    display.expander2.setIntPin(BTN_FWD, FALLING);
  }

  if (!display.sdCardInit()) {
    display.clearDisplay();
    display.setTextColor(BLACK);
    drawTextCenter(MENU_BOLD, display.width() / 2, 340, "No book card found");
    drawTextCenter(UI, display.width() / 2, 380, "Put a FAT32 microSD card in the top slot,");
    drawTextCenter(UI, display.width() / 2, 404, "then press WAKE on the bottom edge.");
    display.display();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
    esp_deep_sleep_start();
  }
  loadSettings();

  pollMusic();
  if (coldBoot) while (millis() - bootStart < SPLASH_MIN_MS) delay(20);
  bool resumed = loadState();
  screen = resumed ? SCR_READER : SCR_HOME;
  if (!resumed) gotoScreen(SCR_LIBRARY);
  needFull = true;
  dirty = true;
  lastActivity = millis();
}

void loop() {
  DuetEvent ev = pollInput();
  if (ev == EV_NONE && pendingTap != EV_NONE) ev = pendingTap;   // a tap made during the last redraw
  pendingTap = EV_NONE;
  if (ev != EV_NONE) {
    lastActivity = millis();
    handleEvent(ev);
  }

  // The audio board: once a second, faster right after a command, slower while it is off
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  uint32_t every = (int32_t)(musicFastUntil - now) > 0 ? 250 : musicOnline ? 1000 : 3000;
  if (now - lastPoll >= every) {
    lastPoll = now;
    uint8_t ch = pollMusic();
    if (ch && musicChangeMatters(ch)) dirty = true;
    if (screen == SCR_PLAYING && btWaiting()) {        // pairing: show the headphones' name once it is known
      char name[DUET_TEXT_LEN];
      if (musicText(CMD_GET_BT_NAME, 0, name, sizeof(name)) && strcmp(name, btName) != 0) dirty = true;
    }
  }

  if (dirty) redraw();

  if (stateDirty && millis() - lastActivity > 1500) saveState();   // not on every page turn: keeps turns quick
  if (millis() - lastActivity > SLEEP_MINUTES[settings.sleepIdx] * 60000UL) goToSleep();
  delay(10);
}
