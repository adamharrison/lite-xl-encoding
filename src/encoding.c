#include <errno.h>
#include <stdbool.h>
#include <uchardet.h>
#include <string.h>
#include <iconv.h>

#ifdef _WIN32
  #include <windows.h>
#endif

#ifdef ENCODING_STANDLONE
  #include <lua.h>
  #include <lauxlib.h>
  #include <lualib.h>
#else
  #define LITE_XL_PLUGIN_ENTRYPOINT
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
int utf8_validate(const char *str, size_t len) {
  int bytes_left = 0;
  for (size_t i = 0; i < len; i++) {
    int state = str[i];
    if (bytes_left) {
      if ((state & 0xC0) != 0x80)
        return 0;
      bytes_left--;
    } else {
      switch (state & 0xf0) {
        case 0xf0 :  bytes_left = 3;  break;
        case 0xe0 :  bytes_left = 2;  break;
        case 0xd0 :
        case 0xc0 :  bytes_left = 1;  break;
        default   :  break;
      }
    }
  }
  return 1;
}

/* Get the applicable byte order marks for the given charset */
static const char* encoding_bom_from_charset(const char* charset, size_t* len) {
  for (size_t i=0; bom_list[i].charset != NULL; i++){
    if (strcmp(bom_list[i].charset, charset) == 0) {
      if (len) *len = bom_list[i].len;
      return bom_list[i].bom;
    }
  }
  if (len) *len = 0;
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
  if (bom_len) *bom_len = 0;
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
 *  Whether a BOM was present, or the error message
 */
int f_detect(lua_State *L) {
	size_t string_len = 0;
	const char* string = luaL_checklstring(L, 1, &string_len);
  if (string_len > 0) {
    size_t bom_len = 0;
    const char* bom_charset = encoding_charset_from_bom(string, string_len, &bom_len);
    if (bom_charset) {
      lua_pushstring(L, bom_charset);
      lua_pushboolean(L, 1);
    } else if (utf8_validate(string, string_len)) {
      lua_pushstring(L, "UTF-8");
      lua_pushboolean(L, 0);
    } else {
      uchardet_t ud = uchardet_new();
      const char* detected_charset = NULL;
      if (uchardet_handle_data(ud, string, string_len) == 0) {
        uchardet_data_end(ud);
        detected_charset = uchardet_get_charset(ud);
      }
      if (detected_charset && *detected_charset) {
        lua_pushstring(L, detected_charset);
        lua_pushboolean(L, 0);
      } else {
        lua_pushnil(L);
        lua_pushstring(L, "could not detect the file encoding");
      }
      uchardet_delete(ud); 
    }
  } else {
    lua_pushstring(L, "UTF-8");
    lua_pushboolean(L, 0);
  }
  return 2;
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
  const unsigned char* bom;
  size_t bom_len = 0;

  if (lua_gettop(L) > 3 && lua_istable(L, 4)) {
    lua_getfield(L, 4, "strict");
    if (lua_isboolean(L, -1)) 
      strict = lua_toboolean(L, -1);
  }
  iconv_t conv = iconv_open(to, from);
  if (conv == (iconv_t)-1) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  char buffer[4096];
  const char* inbuf = (char*)text;
  size_t inbytesleft = text_len;
  while (inbytesleft > 0) {
    size_t err = 0;
    char* outbuf = buffer;
    size_t outbytesleft = sizeof(buffer);
    err = iconv(conv, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (err == -1) {
      if (strict) {
        iconv_close(conv);
        lua_pushnil(L);
        lua_pushstring(L, "illegal multibyte sequence");
        return 2;
      } else {
        ++inbuf;
        --inbytesleft;
      }
    }
    luaL_addlstring(&b, buffer, outbuf - buffer);
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
int f_bom(lua_State *L) {
  size_t bom_len = 0;
  const char* bom = encoding_bom_from_charset(luaL_checkstring(L, 1), &bom_len);
  lua_pushlstring(L, bom, bom_len);
  return 1;
}


static const luaL_Reg lib[] = {
  { "detect",  f_detect  },
  { "convert", f_convert },
  { "bom",     f_bom     },
  { NULL, NULL }
};


int luaopen_lite_xl_encoding(lua_State *L, void* (*api_require)(char *)) {
  lite_xl_plugin_init(api_require);
  luaL_newlib(L, lib);
  return 1;
}
