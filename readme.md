# Bencode reader & validator C library

- [API](#api)
- [Usage](#usage)
  - [Validate](#validate)
  - [Print](#print)
  - [Parse raw bencode into a C data structure](#parse-raw-bencode-into-a-c-data-structure)

What is [Bencode](https://en.wikipedia.org/wiki/Bencode)?

[This library]() provides a bencode validator, bencode human readable formatted printing and primitives to read the bencode stream.

- Zero-allocations with minimal state
- Error messages with column location

## API

This library provides the following functions:
- Validating raw bencode
- Printing raw bencode
- Functions that help parse bencode

```c
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
```

## Usage

### Validate
```c
#include <stdio.h>
#include <string.h>

#define BENCODE_IMPLEMENTATION
#include "bencode.h"

int main(void) {
  const char* data = "d1:a1:a2:aal1:b1:bd3:abe5:lmaoseee";
  Bencode_Reader r = bencode_reader((char*)data, strlen(data));
  Bencode_Value v = bencode_read(&r);

  if (!bencode_validate_value(&r, v, 0)) {
    int col;
    bencode_location(&r, &col);
    printf("Error: %s at column %d\n", r.error, col);
    return 1;
  }
  printf("Valid Bencode\n");
  /*
    Valid Bencode
  */

  data = "d1:a1:a1:al1:b1:bd3:abe5:lmaoseee";
  r = bencode_reader((char*)data, strlen(data));
  v = bencode_read(&r);

  if (!bencode_validate_value(&r, v, 0)) {
    int col;
    bencode_location(&r, &col);
    printf("Error: %s at column %d\n", r.error, col);
    /*
      Error: Keys in the map can't be duplicates at column 12
    */
    return 1;
  }
  printf("Valid Bencode\n");
  return 0;
}
```


### Print
```c
#include <stdio.h>
#include <string.h>

#define BENCODE_IMPLEMENTATION
#include "bencode.h"

int main(void) {
  const char *data = "d1:a1:a2:aal1:b1:bd3:abe5:lmaoseee";
  Bencode_Reader r = bencode_reader((char*)data, strlen(data));
  Bencode_Value v = bencode_read(&r);
  bencode_print_value(&r, v, 0);
  /*
    dict {
      key: a
        string (len=1): a
      key: aa
        list [
          string (len=1): b
          string (len=1): b
          dict {
            key: abe
              string (len=5): lmaos
          }
        ]
    }
  */
  return 0;
}
```

### Parse raw bencode into a C data structure

```c
#include <stdio.h>
#include <string.h>

#define BENCODE_IMPLEMENTATION
#include "bencode.h"

typedef struct {
  int a;
  char* aa[2];
} FixedSchema;

bool parse_fixed_schema(Bencode_Reader *r, FixedSchema* out);

int main(void) {
  char* data = "d1:ai32e2:aal1:b1:bee";
  /*
    dict {
      key: a
        int: 32
      key: aa
        list [
          string (len=1): b
          string (len=1): b
        ]
    }
  */
  Bencode_Reader r = bencode_reader((char*)data, strlen(data));
  
  FixedSchema schema = {0};
  
  if (parse_fixed_schema(&r, &schema)) {
    printf("Parsed successfully:\n");
    printf("  a  = %d\n", schema.a);
    printf("  aa[0] = \"%s\"\n", schema.aa[0]);
    printf("  aa[1] = \"%s\"\n", schema.aa[1]);
    /*
      Parsed successfully:
        a  = 32
        aa[0] = "b"
        aa[1] = "b"
    */
  } else {
    int col;
    bencode_location(&r, &col);
    fprintf(stderr, "Parse error at column %d: %s \n", col, r.error);
  }
  return 0;
}

static char *dup_bencode_str(Bencode_Value v) {
  size_t len = v.end - v.start;
  char *copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, v.start, len);
  copy[len] = '\0';
  return copy;
}

bool parse_fixed_schema(Bencode_Reader *r, FixedSchema* out) {
  Bencode_Value root = bencode_read(r);

  if (root.type != BC_DICT) {
    r->error = "root must be a dictionary";
    return false;
  }

  bool got_a = false, got_aa = false;
  Bencode_Value key, val;

  while (bencode_iter_dict(r, root, &key, &val)) {
    /* compare key with "a" and "aa" */
    size_t key_len = key.end - key.start;
    if (key_len == 1 && memcmp(key.start, "a", 1) == 0) {
      if (val.type != BC_INT) {
        r->error = "value for 'a' must be an int";
        return false;
      }
      out->a = atoi(val.start);
      got_a = true;
    } else if (key_len == 2 && memcmp(key.start, "aa", 2) == 0) {
      if (val.type != BC_LIST) {
        r->error = "value for 'aa' must be a list";
        return false;
      }
      /* iterate the list to fill aa[0], aa[1] */
      int idx = 0;
      Bencode_Value item;
      while (bencode_iter_list(r, val, &item)) {
        if (item.type != BC_STR) {
          r->error = "list item must be a string";
          return false;
        }
        if (idx >= 2) {
          r->error = "list for 'aa' has more than 2 elements";
          return false;
        }
        out->aa[idx++] = dup_bencode_str(item);
      }
      if (item.type == BC_ERROR) {
        return false;
      }
      if (idx != 2) {
        r->error = "list for 'aa' must have exactly 2 elements";
        return false;
      }
      got_aa = true;
    }
  }
  if (r->error != NULL) return false;

  if (key.type == BC_ERROR) return false;

  if (!got_a || !got_aa) {
    r->error = "missing required key(s) 'a' and/or 'aa'";
    return false;
  }

  if (r->cur != r->end) {
    r->error = "trailing garbage after root value";
    return false;
  }

  return true;
}
```

## Credits:
- https://github.com/rxi/sj.h

