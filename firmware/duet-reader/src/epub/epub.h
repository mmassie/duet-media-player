// epub.h — turns an EPUB into the Duet's plain reading text.
//
// An EPUB is a zip of XHTML chapters. epubConvert() reads the zip's central
// directory, finds the book's OPF (via META-INF/container.xml), walks the spine
// in reading order and streams each chapter through a small HTML-to-text state
// machine, writing UTF-8 text to `out`. Nothing is held in memory but a 32 KB
// inflate window and a few small buffers, so books of any size convert.
//
// Output format (what the reader's page layout understands):
//   paragraphs are separated by a blank line ("\n\n")
//   '\f'   starts a new chapter on a new page
//   '\x01' at the start of a paragraph: heading (drawn bold, centred)
//   '\x02' at the start of a paragraph: centred line (scene break "* * *")
#pragma once
#include <stdint.h>
#include <stddef.h>

struct EpubIn {                    // random-access input (the .epub file)
  virtual ~EpubIn() {}
  virtual uint32_t size() = 0;
  virtual bool seek(uint32_t pos) = 0;
  virtual int read(void* buf, uint32_t len) = 0;     // bytes read, or -1
};

struct EpubOut {                   // sequential output (the cached text file)
  virtual ~EpubOut() {}
  virtual bool write(const void* buf, uint32_t len) = 0;
};

struct EpubInfo {
  char title[96];
  char author[64];
  uint16_t chapters;               // spine items converted
  uint32_t textBytes;              // bytes written to `out`
};

enum EpubResult {
  EPUB_OK = 0,
  EPUB_NOT_ZIP,
  EPUB_NO_CONTAINER,
  EPUB_NO_OPF,
  EPUB_NO_SPINE,
  EPUB_BAD_DATA,
  EPUB_NO_MEMORY,
  EPUB_WRITE_FAILED,
  EPUB_DRM,
};

// progress(done, total, ctx) is called after each chapter.
typedef void (*EpubProgress)(int done, int total, void* ctx);

EpubResult epubConvert(EpubIn& in, EpubOut& out, EpubInfo& info, EpubProgress progress = nullptr, void* ctx = nullptr);
// Title and author only: reads two small files from the zip, converts nothing.
EpubResult epubReadInfo(EpubIn& in, EpubInfo& info);
const char* epubErrorText(EpubResult r);
