/**
 * @file theme.c
 * @brief The game theme (see theme.h).
 */
#include "theme.h"
#include "save_data.h"

static int g_theme = BB_THEME_DEFAULT;

void bb_theme_set(int theme) { g_theme = (theme > BB_THEME_DEFAULT && theme < BB_THEME_COUNT) ? theme : BB_THEME_DEFAULT; }
int  bb_theme_get(void) { return g_theme; }
bool bb_theme_halloween(void) { return g_theme == BB_THEME_HALLOWEEN; }
