/* Platform filesystem abstraction — unified API for SSA extern calls */

#ifndef C1_FS_H
#define C1_FS_H

#ifdef __cplusplus
extern "C" {
#endif

/* All functions take NUL-terminated paths.
 * Boolean queries return 1 or 0.
 * size/mtime return -1 on error; mtime is Unix seconds.
 * mkdir/remove/rename/closes return 0 on success, -1 on error.
 * readdir: 1 = entry written to name, 0 = end, -1 = error or name too small.
 */

int c1_fs_exists(const char *path);
int c1_fs_is_file(const char *path);
int c1_fs_is_dir(const char *path);
long long c1_fs_size(const char *path);
long long c1_fs_mtime(const char *path);

int c1_fs_mkdir(const char *path);
int c1_fs_remove(const char *path);
int c1_fs_rename(const char *from, const char *to);

/* Directory iteration; the handle is opaque. */
void *c1_fs_opendir(const char *path);
int c1_fs_readdir(void *dir, char *name, int cap);
int c1_fs_closedir(void *dir);

#ifdef __cplusplus
}
#endif

#endif /* C1_FS_H */
