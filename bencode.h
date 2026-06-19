// BENCODE.C
// bj.h - v0.1 - 2026
// public domain - no warranty implied, use at your own risk

#ifndef BENCODE_H
#define BENCODE_H

#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

typedef struct {
  char *data, *cur, *end;
  int depth;
  char *error;
} Bencode_Reader;

typedef struct {
  int type;
  char *start, *end; /* for strings: start..end (bytes), for ints: start..end (digits between i and e) */
  int depth;
} Bencode_Value;

/*

Here is the list of the possible errors that an ill-formatted bencode may have:

- Null root value.
- Non-singular root item.
- Invalid type encountered (character not 'i', 'l', 'd', or '0'-'9').
- Missing 'e' terminator for 'i', 'l', or 'd' types.
- Byte string errors:
  - Negative length.
  - Length not followed by ':'.
  - Unexpected EOF before completing string.
  - Length specified in units of codepoints (characters) rather than bytes.
- Dictionary errors:
  - Key is not a string.
  - Duplicate keys.
  - Keys not sorted.
  - Keys incorrectly sorted by codepoint in a particular character encoding, rather than lexicographically sorted by ordinal.
  - Missing value for a key.

*/

enum { BJ_ERROR = -1, BJ_END = 0, BJ_INT = 1, BJ_STR = 2, BJ_LIST = 3, BJ_DICT = 4 };

Bencode_Reader bj_reader(char *data, size_t len);
Bencode_Value bj_read(Bencode_Reader *r);
bool bj_iter_list(Bencode_Reader *r, Bencode_Value list, Bencode_Value *val);
bool bj_iter_dict(Bencode_Reader *r, Bencode_Value dict, Bencode_Value *key, Bencode_Value *val);
void bj_location(Bencode_Reader *r, int *col);

#ifdef BENCODE_IMPLEMENTATION

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

Bencode_Reader bj_reader(char *data, size_t len) {
  return (Bencode_Reader) {
    .data = data,
    .cur = data,
    .end = data + len,
    .depth = 0,
    .error = NULL
  };
}

/* read a positive decimal integer used for string length */
static bool bj__read_number(Bencode_Reader* r, long* out) {
  if (r->cur == r->end || !isdigit((unsigned char)*r->cur)) {
    return false;
  }
  
  // Can't have leading zeros
  if (*r->cur == '0') {
    return false;
  }
  
  long v = 0;
  while (r->cur != r->end && isdigit((unsigned char)*r->cur)) {
    int d = *r->cur - '0';
    if (v > (LONG_MAX - d) / 10) {
      r->error = "integer overflow";
      return false;
    }

    v = v * 10 + d;
    r->cur++;
  }

  *out = v;
  return true;
}

static inline Bencode_Value bj_error(Bencode_Reader *r, char * desc) {
  r->error = desc;
  return (Bencode_Value){.type = BJ_ERROR, .start = r->cur, .end = r->cur};
}

Bencode_Value bj_read(Bencode_Reader* r) {
  Bencode_Value res;
top:
  if (r->error) {
    return (Bencode_Value){.type = BJ_ERROR, .start = r->cur, .end = r->cur};
  }
  if (r->cur == r->end) {
    return bj_error(r, "unexpected eof");
  }

  res.start = r->cur;
  switch (*r->cur) {
    // Integer. i<base10 integer>e
    case 'i': {
      res.type = BJ_INT;
      
      r->cur++;
      if (r->cur == r->end) {
        return bj_error(r, "unexpected eof while reading integer");
      }
      
      // Singe we have the next char after i, it is a start.
      // Either a minus sign or a first digit.
      res.start = r->cur;

      // Actually, there could be the third case when after i we would have e.
      // a.k.a. integer without payload. not good. Let's check for it.
      if (*r->cur == 'e') {
        return bj_error(r, "Empty integer payload. No Digits?");
      }

      // We know there is something between i and e.
      // It can be either a digit or a minus.
      // Let's find out what it is and act accordingly
      if (*r->cur == '-') {
        // We are dealing with the negative numbers!
        // Firstly, lets check what's after the -
        r->cur++;
        if (r->cur == r->end) {
          return bj_error(r, "unexpected eof while reading negative integer");
        }
        // Bencode does not support -0 or any leading zeros in numbers.
        if (*r->cur == '0') {
          return bj_error(r, "Integer can't be -0 or have leading zeros");
        }
        // And also we could have a situation of i-e. We don't want it.
        if (*r->cur == 'e') {
          return bj_error(r, "Empty negative integer payload. No Digits?");
        }
        // Now let's advance the r->cur untill we meet e.
        bool e_found = false;
        while (!e_found) {
          // met EOF before e
          if (r->cur == r->end) {
            return bj_error(r, "unexpected eof while reading negative integer digits");
          }
          // met non-digit character (non e)
          if (*r->cur != 'e' && !isdigit(*r->cur)) {
            return bj_error(r, "met non-digit character while reading negative integer digits");
          }
          // Successfully met e, store the last digit position into res.end
          if (*r->cur == 'e') {
            e_found = true;
            res.end = r->cur;
            res.end;
          }
          r->cur++;
        }
        return res;
      } else {
        // No negatives here.
        // The goal is to meet the e and have a valid digits between i an e.
        // Bencode does not support -0 or any leading zeros in numbers.

        // Let's check the edge case: 0 is ok, leading zero 0X is no good
        if (*r->cur == '0') {
          r->cur++;
          if (r->cur == r->end) {
            return bj_error(r, "unexpected eof while reading integer 0. Expected e.");
          }
          if (isdigit(*r->cur)) {
            return bj_error(r, "Integer can't have leading zeros");
          }
          // If the next char after first 0 is not the r->end and not the digit
          // it's either e or an invalid char.
          if (*r->cur != 'e') {
            return bj_error(r, "unexpected character while reading digits of integer");
          }
          res.end = r->cur;
          res.end;
          return res;
        }
        // We handled the -0 and -0X edge case above and returned from it,
        // so here we deal only with a sequence of characters which start not from zero.
        // the r->cur atm is the first char after -
        bool e_found = false;
        while (!e_found) {
          // met EOF before e
          if (r->cur == r->end) {
            return bj_error(r, "unexpected eof while reading positive integer. No e met");
          }
          if (*r->cur != 'e' && !isdigit(*r->cur)) {
            return bj_error(r, "met non-digit character while reading integer digits");
          }
          // Successfully met e, store the last digit position into res.end
          if (*r->cur == 'e') {
            e_found = true;
            res.end = r->cur;
            res.end;
          }
          r->cur++;
        }
        return res;
      }
    }

    // List. l<elements>e
    // Dict. d<pairs>e
    case 'l':
    case 'd': {
      res.type = (*r->cur == 'l') ? BJ_LIST
                                  : BJ_DICT;
      res.depth = ++r->depth;
      r->cur++;
      return res;
    }

    // end of list/dict
    case 'e': {
      res.type = BJ_END;
      if (--r->depth < 0) {
        return bj_error(r, "stray 'e'");
      }
      r->cur++;
      return res;
    }

    // Byte string. <length>:<contents>
    default: {
      /* string: <len>:<data> */
      if (!isdigit((unsigned char)*r->cur)) {
        return bj_error(r, "unknown token");
      }
      long len = 0;
      if (!bj__read_number(r, &len)) {
        return bj_error(r, "invalid string length");
      }
      if (r->cur == r->end || *r->cur != ':') {
        return bj_error(r, "missing ':' after length");
      }
      // skip ':'
      r->cur++;
      if (len < 0 || r->end - r->cur < len) {
        return bj_error(r, "unexpected eof in string");
      }
      res.type = BJ_STR;
      res.start = r->cur;
      r->cur += len;
      res.end = r->cur;
      return res;
    }
  }

  // unreachable
  return bj_error(r, "internal error");
}

static void bj__discard_until(Bencode_Reader* r, int depth) {
  Bencode_Value v;
  v.type = BJ_END;
  while (r->depth != depth && v.type != BJ_ERROR) {
    v = bj_read(r);
  }
}

bool bj_iter_list(Bencode_Reader* r, Bencode_Value list, Bencode_Value* val) {
  bj__discard_until(r, list.depth);
  *val = bj_read(r);
  if (val->type == BJ_ERROR || val->type == BJ_END) {
    return false;
  }
  return true;
}

bool bj_iter_dict(Bencode_Reader* r, Bencode_Value dict, Bencode_Value* key, Bencode_Value* val) {
  bj__discard_until(r, dict.depth);
  *key = bj_read(r);
  if (key->type == BJ_ERROR || key->type == BJ_END) {
    return false;
  }
  if (key->type != BJ_STR) {
    r->error = "dictionary keys must be strings";
    return false;
  }
  *val = bj_read(r);
  if (val->type == BJ_ERROR) {
    return false;
  }
  if (val->type == BJ_END) {
    r->error = "unexpected dict end";
    return false;
  }
  return true;
}

void bj_location(Bencode_Reader* r, int* col) {
  int cl = 1;
  for (char* p = r->data; p != r->cur; p++) {
    cl++;
  }
  *col = cl;
}

#endif /* BJ_IMPL */
#endif
