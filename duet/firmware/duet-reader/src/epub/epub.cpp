// epub.cpp — see epub.h. Plain C++ (std::string / std::vector), no Arduino
// dependencies, so the same file is unit-tested on a PC.
#include "epub.h"
#include "../inflate/duet_tinfl.h"
#include <string>
#include <vector>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

namespace {

// ------------------------------------------------------------------ zip ----
struct ZipEntry {
  std::string name;
  uint16_t method;        // 0 stored, 8 deflate
  uint16_t flags;
  uint32_t csize, usize, local;
};

uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool readAt(EpubIn& in, uint32_t pos, void* buf, uint32_t len) {
  return in.seek(pos) && in.read(buf, len) == (int)len;
}

bool readCentralDirectory(EpubIn& in, std::vector<ZipEntry>& out) {
  uint32_t fsize = in.size();
  if (fsize < 22) return false;
  // The end-of-central-directory record is in the last 22 + 65535 bytes (the zip
  // comment can be up to 64 KB). Almost every EPUB has no comment, so look in the
  // last 1 KB first and only read the full 64 KB if that fails.
  std::vector<uint8_t> buf;
  uint32_t tail = 0;
  int eocd = -1;
  for (uint32_t want : {1024u, 22u + 65535u}) {
    tail = fsize < want ? fsize : want;
    buf.assign(tail, 0);
    if (!readAt(in, fsize - tail, buf.data(), tail)) return false;
    for (int i = (int)tail - 22; i >= 0; i--)
      if (le32(&buf[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd >= 0 || tail == fsize) break;
  }
  if (eocd < 0) return false;
  uint16_t count = le16(&buf[eocd + 10]);
  uint32_t cdSize = le32(&buf[eocd + 12]);
  uint32_t cdOff = le32(&buf[eocd + 16]);
  if (cdOff >= fsize || cdSize > fsize - cdOff) return false;
  std::vector<uint8_t> cd(cdSize);
  if (!readAt(in, cdOff, cd.data(), cdSize)) return false;
  uint32_t p = 0;
  out.clear();
  for (uint16_t i = 0; i < count && p + 46 <= cdSize; i++) {
    if (le32(&cd[p]) != 0x02014b50) return false;
    ZipEntry e;
    e.flags = le16(&cd[p + 8]);
    e.method = le16(&cd[p + 10]);
    e.csize = le32(&cd[p + 20]);
    e.usize = le32(&cd[p + 24]);
    uint16_t n = le16(&cd[p + 28]), x = le16(&cd[p + 30]), c = le16(&cd[p + 32]);
    e.local = le32(&cd[p + 42]);
    if (p + 46 + n > cdSize) return false;
    e.name.assign((const char*)&cd[p + 46], n);
    out.push_back(e);
    p += 46 + n + x + c;
  }
  return !out.empty();
}

const ZipEntry* findEntry(const std::vector<ZipEntry>& zip, const std::string& name) {
  for (auto& e : zip) if (e.name == name) return &e;
  for (auto& e : zip) if (strcasecmp(e.name.c_str(), name.c_str()) == 0) return &e;
  return nullptr;
}

// Stream an entry's uncompressed bytes to sink(data, len, ctx). Returns false on error.
typedef bool (*Sink)(const uint8_t* data, size_t len, void* ctx);

bool extract(EpubIn& in, const ZipEntry& e, Sink sink, void* ctx) {
  if (e.flags & 1) return false;                         // zip-level encryption
  uint8_t lh[30];
  if (!readAt(in, e.local, lh, 30) || le32(lh) != 0x04034b50) return false;
  uint32_t pos = e.local + 30 + le16(lh + 26) + le16(lh + 28);
  if (!in.seek(pos)) return false;

  const uint32_t IN_SZ = 4096;
  uint8_t* inbuf = (uint8_t*)malloc(IN_SZ);
  if (!inbuf) return false;
  bool ok = true;

  if (e.method == 0) {                                   // stored
    uint32_t left = e.csize;
    while (left && ok) {
      uint32_t n = left < IN_SZ ? left : IN_SZ;
      if (in.read(inbuf, n) != (int)n) { ok = false; break; }
      ok = sink(inbuf, n, ctx);
      left -= n;
    }
    free(inbuf);
    return ok;
  }
  if (e.method != 8) { free(inbuf); return false; }

  tinfl_decompressor* d = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
  uint8_t* dict = (uint8_t*)malloc(TINFL_LZ_DICT_SIZE);
  if (!d || !dict) { free(d); free(dict); free(inbuf); return false; }
  tinfl_init(d);
  uint32_t left = e.csize, inAvail = 0, inPos = 0;
  size_t outPos = 0;
  for (;;) {
    if (inAvail == 0 && left) {
      uint32_t n = left < IN_SZ ? left : IN_SZ;
      if (in.read(inbuf, n) != (int)n) { ok = false; break; }
      left -= n; inAvail = n; inPos = 0;
    }
    size_t inBytes = inAvail, outBytes = TINFL_LZ_DICT_SIZE - outPos;
    tinfl_status st = tinfl_decompress(d, inbuf + inPos, &inBytes, dict, dict + outPos, &outBytes,
                                       left ? TINFL_FLAG_HAS_MORE_INPUT : 0);
    inAvail -= inBytes; inPos += inBytes;
    if (outBytes && !sink(dict + outPos, outBytes, ctx)) { ok = false; break; }
    outPos = (outPos + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
    if (st == TINFL_STATUS_DONE) break;
    if (st < 0) { ok = false; break; }
    if (st == TINFL_STATUS_NEEDS_MORE_INPUT && inAvail == 0 && left == 0) { ok = false; break; }
  }
  free(d); free(dict); free(inbuf);
  return ok;
}

bool sinkString(const uint8_t* data, size_t len, void* ctx) {
  std::string* s = (std::string*)ctx;
  if (s->size() + len > 2u * 1024 * 1024) return false;   // OPF/container are small; refuse silly sizes
  s->append((const char*)data, len);
  return true;
}

bool extractString(EpubIn& in, const std::vector<ZipEntry>& zip, const std::string& name, std::string& out) {
  const ZipEntry* e = findEntry(zip, name);
  out.clear();
  return e && extract(in, *e, sinkString, &out);
}

// ------------------------------------------------------------ text bits ----
void putUtf8(std::string& s, uint32_t cp) {
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
  else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

// Decode the body of an entity (without '&' and ';'). Returns false if unknown.
bool decodeEntity(const std::string& ent, std::string& out) {
  if (ent.size() > 1 && ent[0] == '#') {
    uint32_t cp = (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
    if (cp == 0 || cp > 0x10FFFF) return false;
    putUtf8(out, cp);
    return true;
  }
  static const struct { const char* n; uint32_t cp; } T[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"shy", 0xAD}, {"laquo", 0xAB}, {"raquo", 0xBB},
    {"eacute", 0xE9}, {"egrave", 0xE8}, {"agrave", 0xE0}, {"aacute", 0xE1}, {"ccedil", 0xE7},
    {"uuml", 0xFC}, {"ouml", 0xF6}, {"auml", 0xE4}, {"iuml", 0xEF}, {"euml", 0xEB}, {"ecirc", 0xEA},
    {"ocirc", 0xF4}, {"acirc", 0xE2}, {"icirc", 0xEE}, {"ucirc", 0xFB}, {"ntilde", 0xF1},
    {"aelig", 0xE6}, {"oelig", 0x153}, {"szlig", 0xDF}, {"copy", 0xA9}, {"deg", 0xB0},
    {"times", 0xD7}, {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE}, {"pound", 0xA3},
    {"middot", 0xB7}, {"sect", 0xA7}, {"para", 0xB6}, {"dagger", 0x2020}, {"prime", 0x2032},
    {"thinsp", 0x2009}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"zwnj", 0x200C}, {"zwj", 0x200D},
  };
  for (auto& t : T) if (ent == t.n) { putUtf8(out, t.cp); return true; }
  return false;
}

std::string decodeEntities(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '&') {
      size_t semi = s.find(';', i);
      if (semi != std::string::npos && semi - i <= 10 && decodeEntity(s.substr(i + 1, semi - i - 1), out)) { i = semi; continue; }
    }
    out += s[i];
  }
  return out;
}

// ------------------------------------------------------- tiny XML scan ----
struct Tag {
  std::string name;          // local name, lower case, no namespace prefix
  bool closing = false, selfClosing = false;
  std::string raw;           // everything between < and >
  std::string attr(const char* key) const {
    // find  key="value"  or  key='value'  (key may carry a namespace prefix in the source)
    size_t klen = strlen(key);
    for (size_t i = 0; i + klen < raw.size(); i++) {
      if (strncasecmp(raw.c_str() + i, key, klen) != 0) continue;
      char before = i ? raw[i - 1] : ' ';
      if (!(isspace((unsigned char)before) || before == ':')) continue;
      size_t j = i + klen;
      while (j < raw.size() && isspace((unsigned char)raw[j])) j++;
      if (j >= raw.size() || raw[j] != '=') continue;
      j++;
      while (j < raw.size() && isspace((unsigned char)raw[j])) j++;
      if (j >= raw.size() || (raw[j] != '"' && raw[j] != '\'')) continue;
      char q = raw[j];
      size_t end = raw.find(q, j + 1);
      if (end == std::string::npos) return "";
      return decodeEntities(raw.substr(j + 1, end - j - 1));
    }
    return "";
  }
};

void parseTag(const std::string& raw, Tag& t) {
  t.raw = raw;
  size_t i = 0;
  t.closing = !raw.empty() && raw[0] == '/';
  if (t.closing) i = 1;
  size_t start = i;
  while (i < raw.size() && !isspace((unsigned char)raw[i]) && raw[i] != '/' && raw[i] != '>') i++;
  std::string name = raw.substr(start, i - start);
  size_t colon = name.rfind(':');
  if (colon != std::string::npos) name = name.substr(colon + 1);
  for (auto& c : name) c = (char)tolower((unsigned char)c);
  t.name = name;
  size_t e = raw.find_last_not_of(" \t\r\n");
  t.selfClosing = e != std::string::npos && raw[e] == '/';
}

// Iterate the tags of a small, fully-loaded XML document. fn(tag, textAfterTag)
template <typename F>
void forEachTag(const std::string& xml, F fn) {
  size_t i = 0;
  while ((i = xml.find('<', i)) != std::string::npos) {
    if (xml.compare(i, 4, "<!--") == 0) { size_t e = xml.find("-->", i); if (e == std::string::npos) return; i = e + 3; continue; }
    size_t e = xml.find('>', i);
    if (e == std::string::npos) return;
    Tag t;
    parseTag(xml.substr(i + 1, e - i - 1), t);
    size_t next = xml.find('<', e);
    std::string text = xml.substr(e + 1, (next == std::string::npos ? xml.size() : next) - e - 1);
    fn(t, text);
    i = e + 1;
  }
}

// ---------------------------------------------------------------- paths ----
std::string percentDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2;
    } else o += s[i];
  }
  return o;
}

std::string resolvePath(const std::string& baseDir, std::string href) {
  size_t hash = href.find('#');
  if (hash != std::string::npos) href = href.substr(0, hash);
  href = percentDecode(href);
  std::string full = href.size() && href[0] == '/' ? href.substr(1) : baseDir + href;
  std::vector<std::string> parts;
  size_t s = 0;
  while (s <= full.size()) {
    size_t e = full.find('/', s);
    if (e == std::string::npos) e = full.size();
    std::string p = full.substr(s, e - s);
    if (p == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (!p.empty() && p != ".") parts.push_back(p);
    s = e + 1;
  }
  std::string out;
  for (size_t k = 0; k < parts.size(); k++) { if (k) out += '/'; out += parts[k]; }
  return out;
}

// --------------------------------------------------- XHTML → reading text ----
// A streaming state machine. Bytes arrive in arbitrary chunks from the inflater.
struct TextWriter {
  EpubOut* out;
  std::string buf;           // output buffer, flushed in 2 KB blocks
  uint32_t written = 0;
  bool failed = false;
  bool paraHasText = false;  // anything printed since the last paragraph break
  bool anyText = false;      // anything printed in the whole book
  bool pendingSpace = false;
  char pendingMark = 0;      // \x01 / \x02 to put before the next paragraph's first char

  void raw(const char* s, size_t n) {
    buf.append(s, n);
    if (buf.size() >= 2048) flush();
  }
  void flush() {
    if (!buf.empty() && !failed) {
      if (!out->write(buf.data(), buf.size())) failed = true;
      written += buf.size();
    }
    buf.clear();
  }
  void paraBreak() {
    if (paraHasText) raw("\n\n", 2);
    paraHasText = false; pendingSpace = false; pendingMark = 0;
  }
  void chapterBreak() {
    paraBreak();
    if (anyText) raw("\f", 1);
  }
  void text(const char* s, size_t n) {       // UTF-8, may contain whitespace
    for (size_t i = 0; i < n; i++) {
      unsigned char c = (unsigned char)s[i];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (paraHasText) pendingSpace = true; continue; }
      if (!paraHasText) {
        if (pendingMark) { char m = pendingMark; raw(&m, 1); pendingMark = 0; }
      } else if (pendingSpace) raw(" ", 1);
      pendingSpace = false;
      raw((const char*)&s[i], 1);
      paraHasText = anyText = true;
    }
  }
};

enum HState { H_TEXT, H_TAG, H_ENTITY, H_COMMENT, H_CDATA };

struct HtmlToText {
  TextWriter* w;
  HState st = H_TEXT;
  std::string tag, ent, tail;    // tail: last bytes, for -->  and ]]>
  char quote = 0;
  int skip = 0;                  // inside <head>, <style>, <script>, <svg> ...
  bool inBody = false;           // text before <body> (e.g. <title>) is ignored
  bool sawBody = false;

  static bool isBlock(const std::string& n) {
    static const char* B[] = {"p","div","li","ul","ol","blockquote","br","tr","table","section","article","header",
                              "footer","aside","figure","figcaption","dt","dd","dl","pre","nav","body","main","hgroup","td","th","caption","address","center"};
    for (auto b : B) if (n == b) return true;
    return false;
  }
  static bool isHeading(const std::string& n) { return n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6'; }
  static bool isSkip(const std::string& n) { return n == "head" || n == "style" || n == "script" || n == "svg" || n == "math" || n == "rt"; }

  void onTag(const std::string& raw) {
    if (raw.empty()) return;
    if (raw[0] == '?' || raw[0] == '!') return;           // <?xml ...?>, <!DOCTYPE ...>
    Tag t; parseTag(raw, t);
    const std::string& n = t.name;
    if (n == "body") { if (!t.closing) { inBody = sawBody = true; } else inBody = false; }
    if (isSkip(n)) {
      if (t.selfClosing) return;
      if (t.closing) { if (skip) skip--; } else skip++;
      return;
    }
    if (skip) return;
    if (isHeading(n)) {
      w->paraBreak();
      if (!t.closing) w->pendingMark = '\x01';
      return;
    }
    if (n == "hr") {
      w->paraBreak(); w->pendingMark = '\x02';
      w->text("* * *", 5); w->paraBreak();
      return;
    }
    if (isBlock(n)) w->paraBreak();
  }

  bool printing() const { return skip == 0 && (inBody || !sawBody); }

  void feed(const uint8_t* d, size_t len) {
    for (size_t i = 0; i < len; i++) {
      char c = (char)d[i];
      switch (st) {
        case H_TEXT:
          if (c == '<') { st = H_TAG; tag.clear(); quote = 0; }
          else if (c == '&') { st = H_ENTITY; ent.clear(); }
          else if (printing()) w->text(&c, 1);
          break;
        case H_TAG:
          if (quote) { if (c == quote) quote = 0; tag += c; break; }
          if ((c == '"' || c == '\'') && tag.find('=') != std::string::npos) { quote = c; tag += c; break; }
          if (c == '>') { st = H_TEXT; onTag(tag); break; }
          tag += c;
          if (tag == "!--") { st = H_COMMENT; tail.clear(); }
          else if (tag == "![CDATA[") { st = H_CDATA; tail.clear(); }
          else if (tag.size() > 4096) { st = H_TEXT; }    // runaway: give up on this tag
          break;
        case H_ENTITY:
          if (c == ';') {
            std::string o;
            if (decodeEntity(ent, o)) { if (printing()) w->text(o.data(), o.size()); }
            else if (printing()) { w->text("&", 1); w->text(ent.data(), ent.size()); w->text(";", 1); }
            st = H_TEXT;
          } else if (ent.size() > 10 || c == '<' || isspace((unsigned char)c)) {
            if (printing()) { w->text("&", 1); w->text(ent.data(), ent.size()); }
            st = H_TEXT; i--;                                  // re-read this char as text
          } else ent += c;
          break;
        case H_COMMENT:
          tail += c; if (tail.size() > 3) tail.erase(0, 1);
          if (tail == "-->") st = H_TEXT;
          break;
        case H_CDATA:
          tail += c; if (tail.size() > 3) tail.erase(0, 1);
          if (tail == "]]>") st = H_TEXT;
          else if (printing() && c != ']' ) w->text(&c, 1);
          break;
      }
    }
  }
};

bool sinkHtml(const uint8_t* data, size_t len, void* ctx) {
  HtmlToText* h = (HtmlToText*)ctx;
  h->feed(data, len);
  return !h->w->failed;
}

void copyField(char* dst, size_t cap, const std::string& src) {
  // collapse whitespace, trim
  std::string s; bool sp = false;
  for (char c : src) {
    if (isspace((unsigned char)c)) { sp = !s.empty(); continue; }
    if (sp) { s += ' '; sp = false; }
    s += c;
  }
  strncpy(dst, s.c_str(), cap - 1);
  dst[cap - 1] = 0;
}

}  // namespace

// ================================================================ public ====
const char* epubErrorText(EpubResult r) {
  switch (r) {
    case EPUB_OK: return "OK";
    case EPUB_NOT_ZIP: return "Not a valid EPUB (zip) file";
    case EPUB_NO_CONTAINER: return "EPUB is missing META-INF/container.xml";
    case EPUB_NO_OPF: return "EPUB package file not found";
    case EPUB_NO_SPINE: return "EPUB has no chapters";
    case EPUB_BAD_DATA: return "EPUB data is damaged";
    case EPUB_NO_MEMORY: return "Not enough memory";
    case EPUB_WRITE_FAILED: return "Could not write to the card";
    case EPUB_DRM: return "This book is DRM-protected";
  }
  return "Unknown error";
}

static EpubResult convert(EpubIn& in, EpubOut* outp, EpubInfo& info, EpubProgress progress, void* ctx) {
  memset(&info, 0, sizeof(info));
  std::vector<ZipEntry> zip;
  if (!readCentralDirectory(in, zip)) return EPUB_NOT_ZIP;

  // 1. container.xml -> path of the OPF package file
  std::string xml;
  if (!extractString(in, zip, "META-INF/container.xml", xml)) return EPUB_NO_CONTAINER;
  std::string opfPath;
  forEachTag(xml, [&](const Tag& t, const std::string&) {
    if (opfPath.empty() && !t.closing && t.name == "rootfile") opfPath = t.attr("full-path");
  });
  if (opfPath.empty() || !extractString(in, zip, opfPath, xml)) return EPUB_NO_OPF;
  std::string baseDir = opfPath.substr(0, opfPath.rfind('/') + 1);

  // 2. OPF: metadata, manifest, spine
  struct Item { std::string id, href, type; };
  std::vector<Item> manifest;
  std::vector<std::string> spine;
  std::string title, author;
  forEachTag(xml, [&](const Tag& t, const std::string& text) {
    if (t.closing) return;
    if (t.name == "title" && title.empty()) { title = decodeEntities(text); }
    else if (t.name == "creator" && author.empty()) { author = decodeEntities(text); }
    else if (t.name == "item") manifest.push_back({t.attr("id"), t.attr("href"), t.attr("media-type")});
    else if (t.name == "itemref") { if (t.attr("linear") != "no") spine.push_back(t.attr("idref")); }
  });
  copyField(info.title, sizeof(info.title), title);
  copyField(info.author, sizeof(info.author), author);
  if (!outp) return EPUB_OK;                           // title and author only
  EpubOut& out = *outp;

  std::vector<std::string> files;
  for (auto& idref : spine)
    for (auto& it : manifest)
      if (it.id == idref) {
        if (it.type.find("html") != std::string::npos || it.type.empty() || it.type.find("xml") != std::string::npos)
          files.push_back(resolvePath(baseDir, it.href));
        break;
      }
  if (files.empty()) return EPUB_NO_SPINE;

  // 3. DRM check: an encryption.xml that covers chapter files (font obfuscation is fine)
  std::string enc;
  if (extractString(in, zip, "META-INF/encryption.xml", enc))
    for (auto& f : files) {
      std::string leaf = f.substr(f.rfind('/') + 1);
      if (enc.find(leaf) != std::string::npos) return EPUB_DRM;
    }
  if (findEntry(zip, "META-INF/rights.xml")) return EPUB_DRM;

  // 4. Chapters, in reading order
  TextWriter w;
  w.out = &out;
  int done = 0, total = (int)files.size();
  for (auto& f : files) {
    const ZipEntry* e = findEntry(zip, f);
    done++;
    if (!e) { if (progress) progress(done, total, ctx); continue; }   // missing chapter: skip it
    w.chapterBreak();
    HtmlToText h;
    h.w = &w;
    if (!extract(in, *e, sinkHtml, &h)) {
      if (w.failed) return EPUB_WRITE_FAILED;
      return EPUB_BAD_DATA;
    }
    info.chapters++;
    if (progress) progress(done, total, ctx);
  }
  w.paraBreak();
  w.flush();
  if (w.failed) return EPUB_WRITE_FAILED;
  info.textBytes = w.written;
  return w.anyText ? EPUB_OK : EPUB_NO_SPINE;
}

EpubResult epubConvert(EpubIn& in, EpubOut& out, EpubInfo& info, EpubProgress progress, void* ctx) {
  return convert(in, &out, info, progress, ctx);
}

EpubResult epubReadInfo(EpubIn& in, EpubInfo& info) {
  return convert(in, nullptr, info, nullptr, nullptr);
}
