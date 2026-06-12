#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <strings.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#define MIN(a,b)      (((a) < (b)) ? (a) : (b))
#define MAX(a,b)      (((a) > (b)) ? (a) : (b))

#define APPIMAGETOOL_EXIT_OK      0
#define APPIMAGETOOL_EXIT_ERROR   1
#define APPIMAGETOOL_EXIT_USAGE   2

/* ------------------------------------------------------------------ */
/*  Logging                                                            */
/* ------------------------------------------------------------------ */

#define LOG_LEVEL_ERROR (-2)
#define LOG_LEVEL_WARN   (-1)
#define LOG_LEVEL_INFO    0
#define LOG_LEVEL_DEBUG   1

void log_init(int verbosity);
void log_debug(const char *msg);
void log_info(const char *msg);
void log_warn(const char *msg);
void log_error(const char *msg);

#define LOG_DEBUG(fmt, ...)  log_debug2(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   log_info2( fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   log_warn2( fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  log_error2(fmt, ##__VA_ARGS__)

void log_debug2(const char *file, int line, const char *fmt, ...);
void log_info2(const char *fmt, ...);
void log_warn2(const char *fmt, ...);
void log_error2(const char *fmt, ...);

/* ------------------------------------------------------------------ */
/*  Dynamic string                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} Str;

Str  str_new(void);
Str  str_new_from(const char *s);
void str_free(Str *s);
int  str_append(Str *s, const char *fmt, ...);
int  str_append_buf(Str *s, const char *data, size_t len);
int  str_append_c(Str *s, char c);
char *str_detach(Str *s);

/* ------------------------------------------------------------------ */
/*  Config / CLI                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *appdir;
    char  *output_dir;
    char  *output_name;
    char  *appimage_arch;
    char  *arch;
    char  *runtime;
    char  *runtime_url;
    char  *dwarfs_comp;
    char  *update_info;
    char  *dwarfs_profile;
    char  *mkdwarfs;
    char  *dwarfs_url;
    char  *tmpdir;
    char **env_vars;
    size_t env_vars_count;
    char  *version;
    bool   optimize_launch;
    bool   keep_mount;
    bool   devel_release;
    uint64_t profile_timeout;
} Config;

void config_free(Config *cfg);
int  config_from_env(Config *cfg);
int  config_apply_cli(Config *cfg, int argc, char **argv);

/* ------------------------------------------------------------------ */
/*  Desktop entry (.desktop file parsing)                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char *path;
    char *name;
    char *exec;
    char *icon_name;
} DesktopEntry;

void        desktop_entry_free(DesktopEntry *de);
int         desktop_entry_parse(DesktopEntry *de, const char *appdir);
int         desktop_add_metadata(DesktopEntry *de, const char *app_name,
                                 const char *version, const char *arch);
int         desktop_check_apprun(const char *appdir);
int         desktop_check_dir_icon(const char *appdir);
const char *desktop_main_binary(DesktopEntry *de);
int         desktop_compute_output_name(Str *out, const char *app_name,
                                        const char *version, const char *arch);

/* ------------------------------------------------------------------ */
/*  ELF section reader/writer (32/64-bit, LE/BE)                       */
/* ------------------------------------------------------------------ */

typedef struct {
    bool  is_64bit;
    bool  is_le;
    uint64_t sh_off;
    uint64_t sh_entsize;
    uint64_t sh_num;
    uint64_t sh_strndx;
} ElfInfo;

bool elf_parse(ElfInfo *info, const uint8_t *data, size_t data_len);
int  elf_find_section(ElfInfo *info, const uint8_t *data, size_t data_len,
                      const char *name, uint64_t *idx);
int  elf_read_section(const uint8_t *data, size_t data_len,
                      const char *name, const uint8_t **out, size_t *out_len);
int  elf_write_section(uint8_t *data, size_t data_len,
                       const char *name, const uint8_t *value, size_t value_len);
int  elf_write_section_file(const char *path, const char *name,
                            const uint8_t *value, size_t value_len);

/* ELF endian-aware helpers */
uint16_t elf_r16(const uint8_t *data, size_t off, bool le);
uint32_t elf_r32(const uint8_t *data, size_t off, bool le);
uint64_t elf_r64(const uint8_t *data, size_t off, bool le);

/* ------------------------------------------------------------------ */
/*  File utilities                                                      */
/* ------------------------------------------------------------------ */

int  download_file(const char *url, const char *dest);
int  ensure_cached_binary(const char *cached, const char *url);
int  set_executable(const char *path);
int  is_elf(const char *path);
int  sanitize_filename(Str *out, const char *s);
int  mkdir_p(const char *path);
int  file_read(const char *path, uint8_t **data, size_t *len);
int  file_write(const char *path, const uint8_t *data, size_t len);
char *env_get(const char *name);
bool  env_truthy(const char *name);
char *staging_path(const char *dest);
char *process_unique_path(const char *dir, const char *basename);

/* ------------------------------------------------------------------ */
/*  uruntime download / patch                                           */
/* ------------------------------------------------------------------ */

int uruntime_resolve(const Config *cfg, char **out_path);
int uruntime_configure(const char *runtime_path,
                       const char *update_info,
                       const char **env_vars, size_t env_vars_count,
                       bool keep_mount);

/* ------------------------------------------------------------------ */
/*  DWARFS image building                                               */
/* ------------------------------------------------------------------ */

int dwarfs_resolve(const Config *cfg, char **out_path);
int dwarfs_build_appimage(const char *mkdwarfs, const char *appdir,
                          const char *runtime, const char *output,
                          const char *compression, const char *profile);
int dwarfs_build_profile_image(const char *mkdwarfs, const char *appdir,
                               const char *runtime, const char *output);
int dwarfs_check_fuse(void);
int dwarfs_run_profiling(const char *appimage, const char *profile_output,
                         const char *tmpdir, uint64_t timeout_secs);

/* ------------------------------------------------------------------ */
/*  Zsync generation                                                    */
/* ------------------------------------------------------------------ */

int zsync_generate(const char *filepath, const char *filename,
                   const char *output_dir, const char *url);

/* ------------------------------------------------------------------ */
/*  SHA-1                                                              */
/* ------------------------------------------------------------------ */

#define SHA1_DIGEST_SIZE 20

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[64];
} SHA1_CTX;

void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
void sha1_final(SHA1_CTX *ctx, uint8_t digest[SHA1_DIGEST_SIZE]);

/* ------------------------------------------------------------------ */
/*  Top-level build pipeline                                            */
/* ------------------------------------------------------------------ */

int appimage_build(const Config *cfg);
