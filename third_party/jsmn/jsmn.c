/*
 * jsmn.c
 * Minimalistic JSON parser in C.
 * Copyright (c) 2010 Serge A. Zaitsev
 * SPDX-License-Identifier: MIT
 */

#include "jsmn.h"

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
                                   jsmntok_t *tokens,
                                   size_t token_count) {
    jsmntok_t *token;

    if (parser->toknext >= token_count) {
        return NULL;
    }
    token = &tokens[parser->toknext++];
    token->start = -1;
    token->end = -1;
    token->size = 0;
    token->parent = -1;
    token->type = JSMN_UNDEFINED;
    return token;
}

static void jsmn_fill_token(jsmntok_t *token,
                            jsmntype_t type,
                            int start,
                            int end) {
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser,
                                const char *json,
                                size_t length,
                                jsmntok_t *tokens,
                                size_t token_count) {
    unsigned int start = parser->pos;
    jsmntok_t *token;

    for (; parser->pos < length; parser->pos++) {
        char c = json[parser->pos];
        if (c == '\t' || c == '\r' || c == '\n' || c == ' ' ||
            c == ',' || c == ']' || c == '}') {
            break;
        }
        if ((unsigned char)c < 32U || c == ':' || c == '[' || c == '{' ||
            c == '\"' || c == '\\') {
            parser->pos = start;
            return JSMN_ERROR_INVAL;
        }
    }

    if (parser->pos == start) {
        return JSMN_ERROR_INVAL;
    }
    if (tokens == NULL) {
        parser->pos--;
        return 0;
    }

    token = jsmn_alloc_token(parser, tokens, token_count);
    if (token == NULL) {
        parser->pos = start;
        return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, (int)start, (int)parser->pos);
    token->parent = parser->toksuper;
    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser *parser,
                             const char *json,
                             size_t length,
                             jsmntok_t *tokens,
                             size_t token_count) {
    unsigned int start = parser->pos;

    parser->pos++;
    for (; parser->pos < length; parser->pos++) {
        char c = json[parser->pos];

        if (c == '\"') {
            jsmntok_t *token;

            if (tokens == NULL) {
                return 0;
            }
            token = jsmn_alloc_token(parser, tokens, token_count);
            if (token == NULL) {
                parser->pos = start;
                return JSMN_ERROR_NOMEM;
            }
            jsmn_fill_token(token, JSMN_STRING, (int)start + 1,
                            (int)parser->pos);
            token->parent = parser->toksuper;
            return 0;
        }

        if ((unsigned char)c < 32U) {
            parser->pos = start;
            return JSMN_ERROR_INVAL;
        }

        if (c == '\\') {
            parser->pos++;
            if (parser->pos >= length) {
                parser->pos = start;
                return JSMN_ERROR_PART;
            }
            c = json[parser->pos];
            if (c == '\"' || c == '/' || c == '\\' || c == 'b' ||
                c == 'f' || c == 'r' || c == 'n' || c == 't') {
                continue;
            }
            if (c == 'u') {
                unsigned int i;
                for (i = 0; i < 4U; i++) {
                    parser->pos++;
                    if (parser->pos >= length) {
                        parser->pos = start;
                        return JSMN_ERROR_PART;
                    }
                    c = json[parser->pos];
                    if (!((c >= '0' && c <= '9') ||
                          (c >= 'A' && c <= 'F') ||
                          (c >= 'a' && c <= 'f'))) {
                        parser->pos = start;
                        return JSMN_ERROR_INVAL;
                    }
                }
                continue;
            }
            parser->pos = start;
            return JSMN_ERROR_INVAL;
        }
    }

    parser->pos = start;
    return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser *parser) {
    if (parser == NULL) {
        return;
    }
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser,
               const char *json,
               size_t length,
               jsmntok_t *tokens,
               unsigned int token_count) {
    int count;
    int result;

    if (parser == NULL || json == NULL) {
        return JSMN_ERROR_INVAL;
    }
    count = (int)parser->toknext;

    for (; parser->pos < length; parser->pos++) {
        char c = json[parser->pos];
        jsmntok_t *token;
        int i;

        switch (c) {
        case '{':
        case '[':
            count++;
            if (tokens == NULL) {
                break;
            }
            token = jsmn_alloc_token(parser, tokens, token_count);
            if (token == NULL) {
                return JSMN_ERROR_NOMEM;
            }
            if (parser->toksuper != -1) {
                tokens[parser->toksuper].size++;
                token->parent = parser->toksuper;
            }
            token->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
            token->start = (int)parser->pos;
            parser->toksuper = (int)parser->toknext - 1;
            break;

        case '}':
        case ']':
            if (tokens == NULL) {
                break;
            }
            for (i = (int)parser->toknext - 1; i >= 0; i--) {
                token = &tokens[i];
                if (token->start != -1 && token->end == -1) {
                    jsmntype_t expected = (c == '}') ? JSMN_OBJECT : JSMN_ARRAY;
                    if (token->type != expected) {
                        return JSMN_ERROR_INVAL;
                    }
                    token->end = (int)parser->pos + 1;
                    parser->toksuper = token->parent;
                    break;
                }
            }
            if (i < 0) {
                return JSMN_ERROR_INVAL;
            }
            break;

        case '\"':
            result = jsmn_parse_string(parser, json, length, tokens,
                                       token_count);
            if (result < 0) {
                return result;
            }
            count++;
            if (tokens != NULL && parser->toksuper != -1) {
                tokens[parser->toksuper].size++;
            }
            break;

        case '\t':
        case '\r':
        case '\n':
        case ' ':
            break;

        case ':':
            if (tokens != NULL) {
                parser->toksuper = (int)parser->toknext - 1;
            }
            break;

        case ',':
            if (tokens != NULL && parser->toksuper != -1 &&
                tokens[parser->toksuper].type != JSMN_ARRAY &&
                tokens[parser->toksuper].type != JSMN_OBJECT) {
                parser->toksuper = tokens[parser->toksuper].parent;
            }
            break;

        default:
            result = jsmn_parse_primitive(parser, json, length, tokens,
                                          token_count);
            if (result < 0) {
                return result;
            }
            count++;
            if (tokens != NULL && parser->toksuper != -1) {
                tokens[parser->toksuper].size++;
            }
            break;
        }
    }

    if (tokens != NULL) {
        unsigned int i;
        for (i = parser->toknext; i > 0U; i--) {
            if (tokens[i - 1U].start != -1 && tokens[i - 1U].end == -1) {
                return JSMN_ERROR_PART;
            }
        }
    }

    return count;
}
