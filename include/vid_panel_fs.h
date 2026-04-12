#pragma once

#define FS_MAX_ENTRIES 512
#define FS_NAME_LEN    256
#define FS_PATH_LEN    512

typedef enum {
    FS_DIR,
    FS_FILE
} FsType;

typedef struct {
    char   name[FS_NAME_LEN];
    FsType type;
} FsEntry;

typedef struct {
    FsEntry entries[FS_MAX_ENTRIES];
    int     count;
} FsListing;

int  fs_list(const char *path, FsListing *out);
/** 1 if path is an existing directory on SDMC (e.g. "sdmc:/movies"). */
int  fs_directory_exists(const char *path);
void fs_path_enter(char *path, const char *dir);
int  fs_path_up(char *path);

int  fs_path_at_or_below_root(const char *path, const char *root);
int  fs_path_up_bounded(char *path, const char *root);
void fs_path_clamp_to_root(char *path, const char *root);
