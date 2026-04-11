#if !defined(DEF_MENU_H)
#define DEF_MENU_H
#include <stdbool.h>
#include <stdint.h>

bool Menu_query_must_exit_flag(void);

void Menu_set_must_exit_flag(bool flag);

void Menu_init(void);

void Menu_exit(void);

void Menu_main(void);

#endif //!defined(DEF_MENU_H)
