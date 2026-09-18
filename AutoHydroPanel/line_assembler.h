#pragma once

#include <cstddef>

// Splits a byte stream into NDJSON lines. Accepts LF and CRLF, drops blank
// lines, and discards over-long lines up to the next newline instead of
// truncating them into invalid JSON.
template <size_t kCapacity>
class LineAssembler {
 public:
  // Returns true when `line()` holds a complete, non-empty line.
  bool push(char c) {
    if (c == '\n') {
      const bool overflowed = overflow_;
      const size_t len = len_;
      len_ = 0;
      overflow_ = false;
      if (overflowed || len == 0) return false;
      buf_[len] = '\0';
      line_len_ = len;
      return true;
    }
    if (c == '\r') return false;  // tolerate CRLF
    if (overflow_) return false;
    if (len_ + 1 >= kCapacity) {
      overflow_ = true;
      dropped_++;
      len_ = 0;
      return false;
    }
    buf_[len_++] = c;
    return false;
  }

  const char* line() const { return buf_; }
  size_t line_len() const { return line_len_; }
  unsigned dropped() const { return dropped_; }

 private:
  char buf_[kCapacity] = {};
  size_t len_ = 0;
  size_t line_len_ = 0;
  bool overflow_ = false;
  unsigned dropped_ = 0;
};
