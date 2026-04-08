#if !defined(DEF_VID_SETTINGS_HPP)
#define DEF_VID_SETTINGS_HPP

#include <stdint.h>

uint8_t  Vid_get_default_num_of_threads(void);
void     Vid_init_settings(void);
void     Vid_init_hidden_settings(void);
uint32_t Vid_load_settings(void);
uint32_t Vid_save_settings(void);
void     Vid_log_settings(void);

#endif //!defined(DEF_VID_SETTINGS_HPP)
