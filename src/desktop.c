#include "appimagetool.h"
#include <dirent.h>

void desktop_entry_free(DesktopEntry *de)
{
    free(de->path);
    free(de->name);
    free(de->exec);
    free(de->icon_name);
    memset(de, 0, sizeof(*de));
}

static char *parse_key(const char *content, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = content;
    while (*p) {
        const char *line_start = p;
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

        if (line_len > key_len && memcmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val_start = p + key_len + 1;
            size_t val_len = line_len - key_len - 1;
            while (val_len > 0 && (val_start[val_len - 1] == '\r' ||
                   val_start[val_len - 1] == '\n'))
                val_len--;
            char *value = strndup(val_start, val_len);
            if (value) {
                /* trim surrounding quotes */
                size_t vlen = strlen(value);
                if (vlen >= 2 && value[0] == '"' && value[vlen - 1] == '"') {
                    memmove(value, value + 1, vlen - 2);
                    value[vlen - 2] = '\0';
                }
            }
            return value;
        }

        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

int desktop_entry_parse(DesktopEntry *de, const char *appdir)
{
    memset(de, 0, sizeof(*de));

    DIR *dir = opendir(appdir);
    if (!dir) {
        LOG_ERROR("AppDir not found: %s", appdir);
        return -1;
    }

    char found_path[4096] = {0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".desktop") == 0) {
            if (found_path[0]) {
                closedir(dir);
                LOG_ERROR("Multiple .desktop files found, expected exactly one");
                return -1;
            }
            snprintf(found_path, sizeof(found_path), "%s/%s", appdir, entry->d_name);
        }
    }
    closedir(dir);

    if (!found_path[0]) {
        LOG_ERROR("No .desktop file found in AppDir");
        return -1;
    }

    uint8_t *content = NULL;
    size_t content_len = 0;
    if (file_read(found_path, &content, &content_len) != 0) return -1;

    de->path = strdup(found_path);
    de->name = parse_key((const char *)content, "Name");
    if (!de->name) de->name = strdup("");
    de->exec = parse_key((const char *)content, "Exec");
    if (!de->exec) de->exec = strdup("");
    de->icon_name = parse_key((const char *)content, "Icon");

    free(content);
    return 0;
}

int desktop_add_metadata(DesktopEntry *de, const char *app_name,
                         const char *version, const char *arch)
{
    uint8_t *content = NULL;
    size_t content_len = 0;
    if (file_read(de->path, &content, &content_len) != 0) return -1;

    Str out = str_new();
    const char *p = (const char *)content;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

        if (strncmp(p, "X-AppImage-Name=", 16) == 0 ||
            strncmp(p, "X-AppImage-Version=", 19) == 0 ||
            strncmp(p, "X-AppImage-Arch=", 16) == 0) {
            /* skip existing X-AppImage-* lines */
            if (nl) p = nl + 1; else break;
            continue;
        }

        str_append_buf(&out, p, line_len);
        str_append_c(&out, '\n');
        if (!nl) break;
        p = nl + 1;
    }

    str_append(&out, "X-AppImage-Name=%s\n", app_name);
    str_append(&out, "X-AppImage-Version=%s\n", version);
    str_append(&out, "X-AppImage-Arch=%s\n", arch);

    int ret = file_write(de->path, (uint8_t *)out.buf, out.len);
    str_free(&out);
    free(content);
    return ret;
}

const char *desktop_main_binary(DesktopEntry *de)
{
    if (!de->exec || !de->exec[0]) return NULL;
    const char *p = de->exec;
    /* skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    /* skip quotes */
    if (*p == '"') p++;
    /* find last '/' in the first word */
    const char *name = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '"') {
        if (*p == '/') name = p + 1;
        p++;
    }
    return name;
}

int desktop_check_apprun(const char *appdir)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/AppRun", appdir);
    if (access(path, F_OK) != 0) {
        LOG_ERROR("No AppRun found in AppDir");
        return -1;
    }
    return 0;
}

int desktop_check_dir_icon(const char *appdir)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/.DirIcon", appdir);
    if (access(path, F_OK) != 0) {
        LOG_ERROR("No .DirIcon found in AppDir");
        return -1;
    }
    return 0;
}

int desktop_compute_output_name(Str *out, const char *app_name,
                                const char *version, const char *arch)
{
    Str sanitized = str_new();
    sanitize_filename(&sanitized, app_name);

    if (version && version[0]) {
        Str v = str_new();
        sanitize_filename(&v, version);
        str_append(out, "%s-%s-anylinux-%s.AppImage", sanitized.buf, v.buf, arch);
        str_free(&v);
    } else {
        LOG_WARN("VERSION is not set, omitting from filename");
        str_append(out, "%s-anylinux-%s.AppImage", sanitized.buf, arch);
    }

    str_free(&sanitized);
    return 0;
}
