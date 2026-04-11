/* SD path: /3ds/topos/topos_setting.txt (sections: fake_model, sem_settings, vid_settings). */

#ifndef TOPOS_SETTING_H
#define TOPOS_SETTING_H

#include <stdbool.h>
#include <stdint.h>

#define DEF_TOPOS_SETTING_DIR "/3ds/topos/"
#define DEF_TOPOS_SETTING_FILE "topos_setting.txt"

typedef struct {
	uint8_t fake_model;/* 0=O3DS 1=N3DS 255=auto（按可用 CPU 核数判 N/O 档） */
	char* sem_text;
	uint32_t sem_len;
	char* vid_text;
	uint32_t vid_len;
} Topos_md_bundle;

/** Load & parse topos_setting.txt (only settings path). Missing/empty file → write defaults then empty bundle. */
uint32_t Topos_md_read_bundle(Topos_md_bundle* out);
void Topos_md_bundle_free(Topos_md_bundle* out);
/** After Sem_init parsed bundle: hand off vid blob so Vid_load_settings need not open the file again. */
void Topos_md_boot_stash_vid(Topos_md_bundle* b);
/** Returns true once; caller must free *vid. If false, caller should Topos_md_read_bundle for vid. */
bool Topos_md_boot_take_vid(char** vid, uint32_t* len);
uint32_t Topos_md_write_bundle(const char* sem, uint32_t sem_len, const char* vid, uint32_t vid_len, uint8_t fake_model);

#endif
