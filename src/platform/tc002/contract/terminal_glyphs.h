#ifndef AWTRIX_TC002_TERMINAL_GLYPHS_H
#define AWTRIX_TC002_TERMINAL_GLYPHS_H

/* The 3x5 terminal font of the TC002: the runtime's boot intro and info screen
 * (TerminalFont.cpp) and the loader's rescue message draw with it. Five rows per character, bit 2
 * being the left column. */
#define TC002_GLYPH_WIDTH 3
#define TC002_GLYPH_HEIGHT 5

struct tc002_glyph {
  char c;
  unsigned char rows[TC002_GLYPH_HEIGHT];
};

static const struct tc002_glyph tc002_terminal_glyphs[] = {
    {'0', {07, 05, 05, 05, 07}}, {'1', {02, 06, 02, 02, 07}}, {'2', {06, 01, 02, 04, 07}},
    {'3', {06, 01, 02, 01, 06}}, {'4', {05, 05, 07, 01, 01}}, {'5', {07, 04, 06, 01, 06}},
    {'6', {03, 04, 07, 05, 07}}, {'7', {07, 01, 02, 02, 02}}, {'8', {07, 05, 07, 05, 07}},
    {'9', {07, 05, 07, 01, 06}}, {'A', {02, 05, 07, 05, 05}}, {'B', {06, 05, 06, 05, 06}},
    {'C', {03, 04, 04, 04, 03}}, {'D', {06, 05, 05, 05, 06}}, {'E', {07, 04, 06, 04, 07}},
    {'F', {07, 04, 06, 04, 04}}, {'G', {03, 04, 05, 05, 03}}, {'H', {05, 05, 07, 05, 05}},
    {'I', {07, 02, 02, 02, 07}}, {'K', {05, 05, 06, 05, 05}}, {'L', {04, 04, 04, 04, 07}},
    {'M', {05, 07, 07, 05, 05}}, {'N', {06, 05, 05, 05, 05}}, {'O', {02, 05, 05, 05, 02}},
    {'P', {06, 05, 06, 04, 04}}, {'R', {06, 05, 06, 05, 05}}, {'S', {03, 04, 02, 01, 06}},
    {'T', {07, 02, 02, 02, 02}}, {'U', {05, 05, 05, 05, 07}}, {'V', {05, 05, 05, 05, 02}},
    {'W', {05, 05, 05, 07, 05}}, {'X', {05, 05, 02, 05, 05}}, {'Y', {05, 05, 02, 02, 02}},
    {'Z', {07, 01, 02, 04, 07}}, {'%', {05, 01, 02, 04, 05}}, {'.', {00, 00, 00, 00, 02}},
    {'>', {04, 02, 01, 02, 04}}, {':', {00, 02, 00, 02, 00}}, {'-', {00, 00, 07, 00, 00}},
    {'/', {01, 01, 02, 04, 04}}, {'|', {02, 02, 02, 02, 02}}, {'\\', {04, 04, 02, 01, 01}},
};

/* The rows of ch in its 3x5 cell, lower case read as upper case; NULL for a character the font
 * lacks. */
static inline const unsigned char* tc002_glyph_rows(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
  for (unsigned i = 0; i < sizeof tc002_terminal_glyphs / sizeof tc002_terminal_glyphs[0]; ++i)
    if (tc002_terminal_glyphs[i].c == ch) return tc002_terminal_glyphs[i].rows;
  return 0;
}

#endif
