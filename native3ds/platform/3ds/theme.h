/**
 * @file theme.h
 * @brief The game theme (Calendar.GAME_THEME): DEFAULT or HALLOWEEN, chosen with the main menu's Change Theme button and saved
 *        (bb_settings_t.theme). Screens that have Halloween variants in the original ask bb_theme_halloween().
 */
#ifndef BB_THEME_H
#define BB_THEME_H

#include <stdbool.h>

void bb_theme_set(int theme);       /* bb_theme_t */
int  bb_theme_get(void);
bool bb_theme_halloween(void);

#endif
