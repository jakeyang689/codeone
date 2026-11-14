/* Platform filesystem: macOS and Linux (POSIX) */

#if defined(__APPLE__)
  #define _DARWIN_C_SOURCE 1
#elif defined(__linux__)
  #define _POSIX_C_SOURCE 200809L
#else
  #error "Unsupported platform"
#endif

#include "fs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int c1_fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

int c1_fs_is_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int c1_fs_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

long long c1_fs_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

long long c1_fs_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_mtime;
}

int c1_fs_mkdir(const char *path) {
    return mkdir(path, 0777) == 0 ? 0 : -1;
}

int c1_fs_remove(const char *path) {
    return remove(path) == 0 ? 0 : -1;
}

int c1_fs_rename(const char *from, const char *to) {
    return rename(from, to) == 0 ? 0 : -1;
}

void *c1_fs_opendir(const char *path) {
    return (void *)opendir(path);
}

int c1_fs_readdir(void *dir, char *name, int cap) {
    DIR *d = (DIR *)dir;
    struct dirent *entry;
    if (!d) return -1;
    while ((entry = readdir(d)) != NULL) {
        const char *n = entry->d_name;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        size_t len = strlen(n);
        if (len + 1 > (size_t)cap) return -1;
        memcpy(name, n, len + 1);
        return 1;
    }
    return 0;
}

int c1_fs_closedir(void *dir) {
    if (!dir) return -1;
    return closedir((DIR *)dir) == 0 ? 0 : -1;
}
