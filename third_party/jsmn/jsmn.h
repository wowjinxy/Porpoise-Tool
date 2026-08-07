/*
 * jsmn.h
 * Minimalistic JSON parser in C.
 * Copyright (c) 2010 Serge A. Zaitsev
 * SPDX-License-Identifier: MIT
 */

#ifndef JSMN_H
#define JSMN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jsmntype_t {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT = 1,
    JSMN_ARRAY = 2,
    JSMN_STRING = 3,
    JSMN_PRIMITIVE = 4
} jsmntype_t;

enum jsmnerr {
    JSMN_ERROR_NOMEM = -1,
    JSMN_ERROR_INVAL = -2,
    JSMN_ERROR_PART = -3
};

typedef struct jsmntok_t {
    jsmntype_t type;
    int start;
    int end;
    int size;
    int parent;
} jsmntok_t;

typedef struct jsmn_parser {
    unsigned int pos;
    unsigned int toknext;
    int toksuper;
} jsmn_parser;

void jsmn_init(jsmn_parser *parser);

int jsmn_parse(jsmn_parser *parser,
               const char *json,
               size_t length,
               jsmntok_t *tokens,
               unsigned int token_count);

#ifdef __cplusplus
}
#endif

#endif /* JSMN_H */
