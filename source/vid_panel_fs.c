#include "vid_panel_fs.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int cmp_entries(const void *a, const void *b) {
    const FsEntry *ea = (const FsEntry *)a;
    const FsEntry *eb = (const FsEntry *)b;
    if (ea->type != eb->type)
        return (ea->type == FS_DIR) ? -1 : 1;
    return strcmp(ea->name, eb->name);
}

static const char *inner_path(const char *path) {
    if (!path || path[0] == '\0') return "/";
    if (strncmp(path, "sdmc:", 5) == 0) {
        const char *p = path + 5;
        return (p[0] == '\0') ? "/" : p;
    }
    return path;
}

int fs_list(const char *path, FsListing *out) {
    if (!path || !out) return -1;
    out->count = 0;

    FS_Archive archive;
    Result rc = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(rc)) return -1;

    Handle dir_handle;
    rc = FSUSER_OpenDirectory(&dir_handle, archive,
                              fsMakePath(PATH_ASCII, inner_path(path)));
    if (R_FAILED(rc)) {
        FSUSER_CloseArchive(archive);
        return -1;
    }

    FS_DirectoryEntry entry;
    u32 read_count;

    while (out->count < FS_MAX_ENTRIES) {
        read_count = 0;
        rc = FSDIR_Read(dir_handle, &read_count, 1, &entry);
        if (R_FAILED(rc) || read_count == 0) break;

        FsEntry *e = &out->entries[out->count];
        ssize_t n = utf16_to_utf8((uint8_t *)e->name, entry.name, FS_NAME_LEN - 1);
        if (n < 0) n = 0;
        if (n >= FS_NAME_LEN) n = FS_NAME_LEN - 1;
        e->name[n] = '\0';
        if (e->name[0] == '\0') continue;
        e->type = (entry.attributes & FS_ATTRIBUTE_DIRECTORY) ? FS_DIR : FS_FILE;
        out->count++;
    }

    FSDIR_Close(dir_handle);
    FSUSER_CloseArchive(archive);

    qsort(out->entries, out->count, sizeof(FsEntry), cmp_entries);
    return 0;
}

int fs_directory_exists(const char *path) {
    FS_Archive archive;
    Handle dir_handle;
    Result rc;

    if (!path || path[0] == '\0')
        return 0;
    rc = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(rc))
        return 0;
    rc = FSUSER_OpenDirectory(&dir_handle, archive,
                              fsMakePath(PATH_ASCII, inner_path(path)));
    if (R_FAILED(rc)) {
        FSUSER_CloseArchive(archive);
        return 0;
    }
    FSDIR_Close(dir_handle);
    FSUSER_CloseArchive(archive);
    return 1;
}

void fs_path_enter(char *path, const char *dir) {
    int len = (int)strlen(path);
    if (len > 0 && path[len - 1] != '/') {
        strncat(path, "/", FS_PATH_LEN - len - 1);
    }
    strncat(path, dir, FS_PATH_LEN - (int)strlen(path) - 1);
}

int fs_path_up(char *path) {
    char *last  = strrchr(path, '/');
    char *first = strchr(path, '/');
    if (!last) return 0;

    if (last == first) {
        if (*(last + 1) == '\0') return 0;
        *(last + 1) = '\0';
        return 1;
    }

    *last = '\0';
    return 1;
}

int fs_path_at_or_below_root(const char *path, const char *root) {
    if (!path || !root) return 0;
    size_t lr = strlen(root);
    size_t lp = strlen(path);
    if (lr == 0) return 0;

    size_t lr_base = (root[lr - 1] == '/') ? lr - 1 : lr;

    if (lp < lr_base) return 0;
    if (strncmp(path, root, lr_base) != 0) return 0;

    return (path[lr_base] == '\0' || path[lr_base] == '/') ? 1 : 0;
}

int fs_path_up_bounded(char *path, const char *root) {
    char tmp[FS_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    if (!fs_path_up(tmp)) return 0;
    if (!fs_path_at_or_below_root(tmp, root)) return 0;
    strcpy(path, tmp);
    return 1;
}

void fs_path_clamp_to_root(char *path, const char *root) {
    if (fs_path_at_or_below_root(path, root)) return;
    snprintf(path, FS_PATH_LEN, "%s", root);
}
