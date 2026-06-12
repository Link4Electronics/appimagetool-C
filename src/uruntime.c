#include "appimagetool.h"

static const char *DEFAULT_URL_TEMPLATE =
    "https://github.com/Link4Electronics/uruntime-C/releases/download/v0.6.0/"
    "uruntime-appimage-dwarfs-lite-{arch}";

static const uint8_t MOUNT_MARKER[] = "URUNTIME_MOUNT=";

static int find_and_replace(uint8_t *data, size_t data_len,
                            const uint8_t *needle, size_t needle_len,
                            uint8_t replace_byte)
{
    int count = 0;
    for (size_t i = 0; i + needle_len <= data_len; i++) {
        if (memcmp(data + i, needle, needle_len) == 0) {
            size_t digit_idx = i + needle_len;
            if (digit_idx < data_len && data[digit_idx] >= '0' && data[digit_idx] <= '9') {
                data[digit_idx] = replace_byte;
                count++;
            }
        }
    }
    return count;
}

int uruntime_resolve(const Config *cfg, char **out_path)
{
    if (cfg->runtime) {
        if (access(cfg->runtime, F_OK) == 0) {
            *out_path = strdup(cfg->runtime);
            return 0;
        }
        LOG_ERROR("Runtime not found at %s", cfg->runtime);
        return -1;
    }

    Str cached = str_new();
    str_append(&cached, "%s/uruntime-%s", cfg->tmpdir, cfg->appimage_arch);

    Str url = str_new();
    if (cfg->runtime_url)
        str_append(&url, "%s", cfg->runtime_url);
    else
        str_append(&url, "%s", DEFAULT_URL_TEMPLATE);

    /* Replace {arch} in URL */
    {
        Str final = str_new();
        const char *p = url.buf;
        while (*p) {
            const char *brace = strstr(p, "{arch}");
            if (!brace) {
                str_append(&final, "%s", p);
                break;
            }
            str_append_buf(&final, p, (size_t)(brace - p));
            str_append(&final, "%s", cfg->appimage_arch);
            p = brace + 6;
        }
        str_free(&url);
        url = final;
    }

    if (ensure_cached_binary(cached.buf, url.buf) != 0) {
        str_free(&cached);
        str_free(&url);
        return -1;
    }

    /* Per-process working copy */
    Str work = str_new();
    str_append(&work, "%s/uruntime-%s.work.%d",
               cfg->tmpdir, cfg->appimage_arch, getpid());

    /* Copy cached to working copy */
    {
        uint8_t *buf = NULL;
        size_t len = 0;
        if (file_read(cached.buf, &buf, &len) != 0) {
            str_free(&cached);
            str_free(&url);
            str_free(&work);
            return -1;
        }
        if (file_write(work.buf, buf, len) != 0) {
            free(buf);
            str_free(&cached);
            str_free(&url);
            str_free(&work);
            return -1;
        }
        free(buf);
    }

    set_executable(work.buf);
    *out_path = str_detach(&work);

    str_free(&cached);
    str_free(&url);
    return 0;
}

int uruntime_configure(const char *runtime_path,
                       const char *update_info,
                       const char **env_vars, size_t env_vars_count,
                       bool keep_mount)
{
    uint8_t *data = NULL;
    size_t data_len = 0;
    if (file_read(runtime_path, &data, &data_len) != 0)
        return -1;

    int ret = 0;

    if (update_info) {
        LOG_INFO("Adding update information to runtime...");
        ret = elf_write_section(data, data_len, ".upd_info",
                                (const uint8_t *)update_info, strlen(update_info));
        if (ret != 0) goto cleanup;
    }

    if (env_vars_count > 0) {
        LOG_INFO("Adding environment variables to runtime...");
        Str env_data = str_new();
        for (size_t i = 0; i < env_vars_count; i++) {
            if (i > 0) str_append_c(&env_data, '\n');
            str_append(&env_data, "%s", env_vars[i]);
        }
        ret = elf_write_section(data, data_len, ".envs",
                                (const uint8_t *)env_data.buf, env_data.len);
        str_free(&env_data);
        if (ret != 0) goto cleanup;
    }

    if (keep_mount) {
        LOG_INFO("Setting runtime to keep mount point...");
        int count = find_and_replace(data, data_len,
                                     MOUNT_MARKER, sizeof(MOUNT_MARKER) - 1, '0');
        if (count == 0) {
            LOG_ERROR("URUNTIME_MOUNT marker not found in runtime");
            ret = -1;
            goto cleanup;
        }
        LOG_DEBUG("Patched %d URUNTIME_MOUNT markers", count);
    }

    if (file_write(runtime_path, data, data_len) != 0)
        ret = -1;

cleanup:
    free(data);
    return ret;
}
