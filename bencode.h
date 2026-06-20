// BENCODE.C
// bencode.h - v0.1 - 2026
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

enum { BC_ERROR = -1, BC_END = 0, BC_INT = 1, BC_STR = 2, BC_LIST = 3, BC_DICT = 4 };

// Create an instance of Bencode reader with the raw bencode data passed
Bencode_Reader bencode_reader(char *data, size_t len);
// Advance bencode reader by one value
Bencode_Value bencode_read(Bencode_Reader *r);
// Iterate bencode reader once over a list (get next list's element)
bool bencode_iter_list(Bencode_Reader *r, Bencode_Value list, Bencode_Value *val);
// Iterate bencode reader once over a dict (get next dict's key&value)
bool bencode_iter_dict(Bencode_Reader *r, Bencode_Value dict, Bencode_Value *key, Bencode_Value *val);
// Get current column of the Bencode reader
void bencode_location(Bencode_Reader *r, int *col);
// Validate bencode value (Iterates itself over all raw bencode data, returns true the raw data is valid)
bool bencode_validate_value(Bencode_Reader *r, Bencode_Value v, int indent);
// Prints the bencode value regardless of the data validity
void bencode_print_value(Bencode_Reader *r, Bencode_Value v, int indent);

#ifdef BENCODE_IMPLEMENTATION

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

Bencode_Reader bencode_reader(char *data, size_t len) {
  return (Bencode_Reader) {
    .data = data,
    .cur = data,
    .end = data + len,
    .depth = 0,
    .error = NULL
  };
}

/* read a positive decimal integer used for string length */
static bool bencode__read_number(Bencode_Reader* r, long* out) {
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

static inline Bencode_Value bencode_error(Bencode_Reader *r, char * desc) {
  r->error = desc;
  return (Bencode_Value){.type = BC_ERROR, .start = r->cur, .end = r->cur};
}

Bencode_Value bencode_read(Bencode_Reader* r) {
  Bencode_Value res;

  if (r->error) {
    return (Bencode_Value){.type = BC_ERROR, .start = r->cur, .end = r->cur};
  }
  if (r->cur == r->end) {
    return bencode_error(r, "unexpected eof");
  }

  res.start = r->cur;
  switch (*r->cur) {
    // Integer. i<base10 integer>e
    case 'i': {
      res.type = BC_INT;

      r->cur++;
      if (r->cur == r->end) {
        return bencode_error(r, "unexpected eof while reading integer");
      }

      // Singe we have the next char after i, it is a start.
      // Either a minus sign or a first digit.
      res.start = r->cur;

      // Actually, there could be the third case when after i we would have e.
      // a.k.a. integer without payload. not good. Let's check for it.
      if (*r->cur == 'e') {
        return bencode_error(r, "Empty integer payload. No Digits?");
      }

      // We know there is something between i and e.
      // It can be either a digit or a minus.
      // Let's find out what it is and act accordingly
      if (*r->cur == '-') {
        // We are dealing with the negative numbers!
        // Firstly, lets check what's after the -
        r->cur++;
        if (r->cur == r->end) {
          return bencode_error(r, "unexpected eof while reading negative integer");
        }
        // Bencode does not support -0 or any leading zeros in numbers.
        if (*r->cur == '0') {
          return bencode_error(r, "Integer can't be -0 or have leading zeros");
        }
        // And also we could have a situation of i-e. We don't want it.
        if (*r->cur == 'e') {
          return bencode_error(r, "Empty negative integer payload. No Digits?");
        }
        // Now let's advance the r->cur untill we meet e.
        bool e_found = false;
        while (!e_found) {
          // met EOF before e
          if (r->cur == r->end) {
            return bencode_error(r, "unexpected eof while reading negative integer digits");
          }
          // met non-digit character (non e)
          if (*r->cur != 'e' && !isdigit(*r->cur)) {
            return bencode_error(r, "met non-digit character while reading negative integer digits");
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
            return bencode_error(r, "unexpected eof while reading integer 0. Expected e.");
          }
          if (isdigit(*r->cur)) {
            return bencode_error(r, "Integer can't have leading zeros");
          }
          // If the next char after first 0 is not the r->end and not the digit
          // it's either e or an invalid char.
          if (*r->cur != 'e') {
            return bencode_error(r, "unexpected character while reading digits of integer");
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
            return bencode_error(r, "unexpected eof while reading positive integer. No e met");
          }
          if (*r->cur != 'e' && !isdigit(*r->cur)) {
            return bencode_error(r, "met non-digit character while reading integer digits");
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
      res.type = (*r->cur == 'l') ? BC_LIST
                                  : BC_DICT;
      res.depth = ++r->depth;
      r->cur++;
      return res;
    }

    // end of list/dict
    case 'e': {
      res.type = BC_END;
      if (--r->depth < 0) {
        return bencode_error(r, "stray 'e'");
      }
      r->cur++;
      return res;
    }

    // Byte string. <length>:<contents>
    default: {
      /* string: <len>:<data> */
      if (!isdigit((unsigned char)*r->cur)) {
        return bencode_error(r, "unknown token");
      }

      long len = 0;
      // Check an edge case of length being 0. and leading zeros
      if (*r->cur == '0') {
        r->cur++;
        if (r->cur == r->end) {
          return bencode_error(r, "unexpected eof while reading string length '0'. Expected ':'");
        }
        if (*r->cur != ':') {
          static char err_buf[71];
          if (isdigit(*r->cur)) {
            snprintf(err_buf, sizeof(err_buf), "No leading zeros in string length allowed '0'");
            return bencode_error(r, err_buf);
          }
          snprintf(err_buf, sizeof(err_buf), "unexpected symbol while reading string length '0'. Expected ':', got %c", *r->cur);
          return bencode_error(r, err_buf);
        }
      } else {
        // Get length of the string in other cases
        if (!bencode__read_number(r, &len)) {
          return bencode_error(r, "invalid string length");
        }
      }
      if (r->cur == r->end || *r->cur != ':') {
        if (r->cur == r->end) {
          return bencode_error(r, "missing ':' after string length. got 'eof'");
        }
        static char err_buf[41];
        snprintf(err_buf, sizeof(err_buf), "missing ':' after string length. got '%c'", *r->cur);
        return bencode_error(r, err_buf);
      }
      // skip ':'
      r->cur++;
      if (len < 0 || r->end - r->cur < len) {
        return bencode_error(r, "unexpected eof in string");
      }
      res.type = BC_STR;
      res.start = r->cur;
      r->cur += len;
      res.end = r->cur;
      return res;
    }
  }

  // unreachable
  return bencode_error(r, "internal error");
}

static void bencode__discard_until(Bencode_Reader* r, int depth) {
  Bencode_Value v;
  v.type = BC_END;
  while (r->depth != depth && v.type != BC_ERROR) {
    v = bencode_read(r);
  }
}

bool bencode_iter_list(Bencode_Reader* r, Bencode_Value list, Bencode_Value* val) {
  bencode__discard_until(r, list.depth);
  *val = bencode_read(r);
  if (val->type == BC_ERROR || val->type == BC_END) {
    return false;
  }
  return true;
}

bool bencode_iter_dict(Bencode_Reader* r, Bencode_Value dict, Bencode_Value* key, Bencode_Value* val) {
  bencode__discard_until(r, dict.depth);
  *key = bencode_read(r);
  if (key->type == BC_END) {
    return false;
  }
  if (key->type == BC_ERROR) {
    return false;
  }
  if (key->type != BC_STR) {
    r->error = "dictionary keys must be strings";
    return false;
  }
  *val = bencode_read(r);
  if (val->type == BC_ERROR) {
    return false;
  }
  if (val->type == BC_END) {
    r->error = "unexpected dict end. Expected a value for the key";
    return false;
  }
  return true;
}

void bencode_location(Bencode_Reader* r, int* col) {
  int cl = 1;
  for (char* p = r->data; p != r->cur; p++) {
    cl++;
  }
  *col = cl;
}

static void print_indent(int d) {
  for (int i = 0; i < d; i++) putchar(' ');
}

static void print_str(Bencode_Value s) {
  fwrite(s.start, 1, (size_t)(s.end - s.start), stdout);
}

bool bencode_validate_value(Bencode_Reader* r, Bencode_Value v, int indent);

static bool validate_list(Bencode_Reader* r, Bencode_Value list, int indent) {
  Bencode_Value item;
  while (bencode_iter_list(r, list, &item)) {
    if (!bencode_validate_value(r, item, indent + 2)) {
      return false;
    }
  }
  if (item.type == BC_ERROR) {
    return false;
  }
  return true;
}

// < 0 if key1 less than key2
//   0 if key1 is identical to key2
// > 0 if key1 greater than key2
static int bencode_keys_compare(const Bencode_Value *key1,
                                const Bencode_Value *key2) {
  size_t len1 = key1->end - key1->start;
  size_t len2 = key2->end - key2->start;
  size_t min_len = (len1 < len2) ? len1
                                 : len2;
  int result = memcmp(key1->start, key2->start, min_len);
  if (result == 0) {
    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
  }
  return result;
}

static bool validate_dict(Bencode_Reader* r, Bencode_Value dict, int indent) {
  Bencode_Value prev_key;
  bool first_key = true;

  Bencode_Value key, val;

  while (bencode_iter_dict(r, dict, &key, &val)) {
    if (!first_key) {
      int cmp_res = bencode_keys_compare(&prev_key, &key);
      if (cmp_res == 0) {
        r->error = "Keys in the map can't be duplicates";
        return false;
      }
      if (cmp_res > 0) {
        r->error = "Keys in the dict must be sorted alphabetically";
        return false;
      }
      prev_key = key;
    } else {
      first_key = false;
      prev_key = key;
    }

    if (!bencode_validate_value(r, val, indent + 4)) {
      return false;
    }
  }
  if (key.type == BC_ERROR) {
    return false;
  }
  if (r->error != NULL) {
    return false;
  }
  if (val.type == BC_ERROR || val.type == BC_END) {
    return false;
  }
  return true;
}

bool bencode_validate_value(Bencode_Reader* r, Bencode_Value v, int indent) {
  if (v.type == BC_ERROR) {
    return false;
  }
  switch (v.type) {
    case BC_STR:
      break;
    case BC_INT:
      break;
    case BC_LIST:
      if (!validate_list(r, v, indent)) {
        return false;
      }
      break;
    case BC_DICT:
      if (!validate_dict(r, v, indent)) {
        return false;
      }
      break;
    case BC_END:
      break;
    case BC_ERROR:
    default:
      return false;
      break;
  }
  if (indent == 0) {
    if (r->cur != r->end) {
      r->error = "Some grabage after root value. no good.";
      return false;
    }
  }
  return true;
}

void bencode_print_value(Bencode_Reader* r, Bencode_Value v, int indent);

static void print_list(Bencode_Reader* r, Bencode_Value list, int indent) {
  print_indent(indent);
  printf("list [\n");
  Bencode_Value item;
  while (bencode_iter_list(r, list, &item)) {
    bencode_print_value(r, item, indent + 2);
  }
  print_indent(indent);
  printf("]\n");
}

static void print_dict(Bencode_Reader* r, Bencode_Value dict, int indent) {
  print_indent(indent);
  printf("dict {\n");
  Bencode_Value key, val;
  while (bencode_iter_dict(r, dict, &key, &val)) {
    print_indent(indent + 2);
    printf("key: ");
    print_str(key);
    printf("\n");
    bencode_print_value(r, val, indent + 4);
  }
  print_indent(indent);
  printf("}\n");
}

void bencode_print_value(Bencode_Reader* r, Bencode_Value v, int indent) {
  switch (v.type) {
    case BC_STR:
      print_indent(indent);
      printf("string (len=%ld): ", (long)(v.end - v.start));
      print_str(v);
      printf("\n");
      break;
    case BC_INT:
      print_indent(indent);
      printf("int: ");
      fwrite(v.start, 1, (size_t)(v.end - v.start), stdout);
      printf("\n");
      break;
    case BC_LIST:
      print_list(r, v, indent);
      break;
    case BC_DICT:
      print_dict(r, v, indent);
      break;
    case BC_END:
      print_indent(indent);
      printf("end\n");
      break;
    case BC_ERROR:
    default:
      print_indent(indent);
      printf("error at position\n");
      break;
  }
}

#endif /* BENCODE_IMPLEMENTATION */
#endif // BENCODE_H
