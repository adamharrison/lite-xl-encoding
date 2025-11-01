#include <errno.h>
#include <stdbool.h>
#include <uchardet.h>

#ifdef _WIN32
  #include <windows.h>
#endif

#ifdef USE_LUA
  #include <lua.h>
  #include <lauxlib.h>
  #include <lualib.h>
#else
  #include <lite_xl_plugin_api.h>
#endif

typedef struct {
  const char* charset;
  unsigned char bom[4];
  int len;
} bom_t;

/*
 * List of encodings that can have byte order marks.
 * Note: UTF-32 should be tested before UTF-16, the order matters.
*/
static bom_t bom_list[] = {
  { "UTF-8",    {0xef, 0xbb, 0xbf},       3 },
  { "UTF-32LE", {0xff, 0xfe, 0x00, 0x00}, 4 },
  { "UTF-32BE", {0x00, 0x00, 0xfe, 0xff}, 4 },
  { "UTF-16LE", {0xff, 0xfe},             2 },
  { "UTF-16BE", {0xfe, 0xff},             2 },
  { "GB18030",  {0x84, 0x31, 0x95, 0x33}, 4 },
  { "UTF-7",    {0x2b, 0x2f, 0x76, 0x38}, 4 },
  { "UTF-7",    {0x2b, 0x2f, 0x76, 0x39}, 4 },
  { "UTF-7",    {0x2b, 0x2f, 0x76, 0x2b}, 4 },
  { "UTF-7",    {0x2b, 0x2f, 0x76, 0x2f}, 4 },
  { NULL }
};

/*
 * NOTE:
 * Newer uchardet currently has some issues properly detecting some instances of
 * UTF-8 as seen on https://gitlab.freedesktop.org/uchardet/uchardet/-/issues.
 * REF: https://github.com/notepad-plus-plus/notepad-plus-plus/issues/5310
 *
 * For this reason, we included a third party MIT function to check if a string
 * is valid utf8 and prefer this result over the one from uchardet.
*/
/*************************UTF-8 Validation Code********************************/
/* Found on: https://stackoverflow.com/a/22135005                             */
/* Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>             */
/* See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.             */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

uint32_t utf8_validate(uint32_t *state, const char *str, size_t len) {
   size_t i;
   uint32_t type;

    for (i = 0; i < len; i++) {
        // We don't care about the codepoint, so this is
        // a simplified version of the decode function.
        type = utf8d[(uint8_t)str[i]];
        *state = utf8d[256 + (*state) * 16 + type];

        if (*state == UTF8_REJECT)
            break;
    }

    return *state;
}
/*************************End of UTF-8 Validation Code*************************/


/* Get the applicable byte order marks for the given charset */
static const char* encoding_bom_from_charset(const char* charset, size_t* len) {
  for (size_t i=0; bom_list[i].charset != NULL; i++){
    if (strcmp(bom_list[i].charset, charset) == 0) {
      if (len) *len = bom_list[i].len;
      return bom_list[i].bom;
    }
  }
  if (len) 
    *len = 0;
  return "";
}


/* Detect the encoding of the given string if a valid bom sequence is found */
static const char* encoding_charset_from_bom(
  const char* string, size_t len, size_t* bom_len
) {
  const unsigned char* bytes = (unsigned char*) string;

  for (size_t i=0; bom_list[i].charset != NULL; i++) {
    if (len >= bom_list[i].len) {
      bool all_match = true;
      for (size_t b = 0; b<bom_list[i].len; b++) {
        if (bytes[b] != bom_list[i].bom[b]) {
          all_match = false;
          break;
        }
      }
      if (all_match) {
        if (bom_len) *bom_len = bom_list[i].len;
        return bom_list[i].charset;
      }
    }
  }

  if (bom_len)
      *bom_len = 0;

  return NULL;
}


/*
 * encoding.detect(string)
 *
 * Detects a string's encoding.
 *
 * Arguments:
 *  string, the string to check
 *
 * Returns:
 *  The charset string or nil
 *  The error message
 */
int f_detect_string(lua_State *L) {
	size_t string_len = 0;
	const char* string = luaL_checklstring(L, 1);
  if (string_len == 0) 
		return "UTF-8";
  static char charset[30] = {0};
  size_t bom_len = 0;
  const char* bom_charset = encoding_charset_from_bom(string, string_len, &bom_len);
  uchardet_t ud = uchardet_new();
  uchardet_handle_data(ud, buffer, string_len);
  uchardet_data_end(ud);
  const char* charset = uchardet_get_charset(ud);
  uchardet_delete(ud);
  if (charset) {
    lua_pushstring(L, charset);
  } else {
    lua_pushnil(L);
		lua_pushstring(L, "could not detect the file encoding");
		return 2;
  }
  return 1;
}


/*
 * encoding.convert(tocharset, fromcharset, text, options)
 *
 * Convert the given text from one charset into another if possible.
 *
 * Arguments:
 *  tocharset, a string representing a valid iconv charset
 *  fromcharset, a string representing a valid iconv charset
 *  text, the string to convert
 *  options, a table of conversion options
 *
 * Returns:
 *  The converted ouput string or nil
 *  The error message
 */
int f_convert(lua_State *L) {
  const char* to = luaL_checkstring(L, 1);
  const char* from = luaL_checkstring(L, 2);
  size_t text_len = 0;
  const char* text = luaL_checklstring(L, 3, &text_len);
  /* conversion options */
  bool strict = false;
  bool handle_to_bom = false;
  bool handle_from_bom = false;
  const unsigned char* bom;
  size_t bom_len = 0;

  if (lua_gettop(L) > 3 && lua_istable(L, 4)) {
    lua_getfield(L, 4, "handle_to_bom");
    if (lua_isboolean(L, -1)) {
      handle_to_bom = lua_toboolean(L, -1);
    }
    lua_getfield(L, 4, "handle_from_bom");
    if (lua_isboolean(L, -1)) {
      handle_from_bom = lua_toboolean(L, -1);
    }
    lua_getfield(L, 4, "strict");
    if (lua_isboolean(L, -1)) {
      strict = lua_toboolean(L, -1);
    }
  }
  iconv_t conv = iconv_open(to, from);
  if (conv == (iconv_t)-1) {
    lua_pushnil(L);
    lua_pushstring(L, errstr(errno));
    return 2;
  }
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  char buffer[4096];
  const char* inbuf = text;
  size_t inbytesleft = text_len;
  while (inbytesleft > 0) {
    size_t err = 0;
    char* outbuf = buffer;
    size_t outbytesleft = sizeof(buffer);
    err = iconv(conv, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (err == -1) {
      iconv_close(conv);
      lua_pushnil(L);
      lua_pushstring(L, "illegal multibyte sequence");
      return 2;
    }
    luaL_addlstring(L, buffer, outbuf - buffer);
  }
  luaL_pushresult(&b);
  return 1;
}


/*
 * encoding.bom(charset)
 *
 * Retrieve the byte order marks sequence for the given charset if applicable.
 *
 * Arguments:
 *  charset, a string representing a valid iconv charset
 *
 * Returns:
 *  The bom sequence string or empty string if not applicable.
 */
int f_get_charset_bom(lua_State *L) {
  size_t bom_len = 0;
  const char* bom = encoding_bom_from_charset(luaL_checkstring(L, 1), &bom_len);
  lua_pushlstring(L, bom, bom_len);
  return 1;
}


static const luaL_Reg lib[] = {
  { "detect",          f_detect          },
  { "convert",         f_convert         },
  { "bom",             f_bom             },
  { NULL, NULL }
};


int luaopen_encoding(lua_State *L) {
  luaL_newlib(L, lib);
  return 1;
}

/* Called by lite-xl f_load_native_plugin on `require "encoding"` */
int luaopen_lite_xl_encoding(lua_State *L, void* (*api_require)(char *)) {
#ifndef USE_LUA
  lite_xl_plugin_init(api_require);
#endif
  return luaopen_encoding(L);
}
