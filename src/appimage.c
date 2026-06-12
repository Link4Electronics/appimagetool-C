#include "appimagetool.h"

static int sort_env_file(const char *appdir)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/.env", appdir);

    if (access(path, F_OK) != 0) return 0;

    uint8_t *content = NULL;
    size_t content_len = 0;
    if (file_read(path, &content, &content_len) != 0) return -1;

    /* Split into regular and unset lines */
    Str regular = str_new();
    Str unsets = str_new();

    const char *p = (const char *)content;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

        if (strncmp(p, "unset", 5) == 0 &&
            (p[5] == ' ' || p[5] == '\t' || p[5] == '\0')) {
            str_append_buf(&unsets, p, line_len);
            str_append_c(&unsets, '\n');
        } else {
            str_append_buf(&regular, p, line_len);
            str_append_c(&regular, '\n');
        }

        if (!nl) break;
        p = nl + 1;
    }

    Str sorted = str_new();
    str_append(&sorted, "%s%s", regular.buf, unsets.buf);

    free(content);
    str_free(&regular);
    str_free(&unsets);

    int ret = file_write(path, (const uint8_t *)sorted.buf, sorted.len);
    str_free(&sorted);
    return ret;
}

static int write_appinfo(const char *output_dir, const char *name,
                         const char *version, const char *arch)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/appinfo", output_dir);

    Str content = str_new();
    str_append(&content, "X-AppImage-Name=%s\n", name);
    str_append(&content, "X-AppImage-Version=%s\n", version);
    str_append(&content, "X-AppImage-Arch=%s\n", arch);

    int ret = file_write(path, (const uint8_t *)content.buf, content.len);
    str_free(&content);
    return ret;
}

int appimage_build(const Config *cfg)
{
    /* Validate AppDir */
    if (desktop_check_apprun(cfg->appdir) != 0) return -1;
    if (desktop_check_dir_icon(cfg->appdir) != 0) return -1;

    /* Sort .env */
    if (sort_env_file(cfg->appdir) != 0) {
        LOG_WARN("Failed to sort .env file");
    }

    /* Parse desktop entry */
    DesktopEntry de;
    if (desktop_entry_parse(&de, cfg->appdir) != 0) return -1;

    /* Determine app name */
    const char *app_name_raw = env_get("APPNAME");
    if (!app_name_raw) app_name_raw = de.name;
    Str app_name = str_new();
    sanitize_filename(&app_name, app_name_raw);
    if (app_name_raw != de.name) free((char *)app_name_raw);
    free((char *)de.name); de.name = strdup(app_name.buf); /* HACK: reuse */

    const char *version = cfg->version ? cfg->version : "UNKNOWN";

    /* Handle devel release */
    const char *update_info = cfg->update_info;
    Str patched_update = str_new();

    if (cfg->devel_release) {
        uint8_t *content = NULL;
        size_t content_len = 0;
        if (file_read(de.path, &content, &content_len) == 0) {
            Str new_content = str_new();
            int found_nightly = 0;
            const char *p = (const char *)content;
            while (*p) {
                const char *nl = strchr(p, '\n');
                size_t line_len = nl ? (size_t)(nl - p) : strlen(p);

                if (strncmp(p, "Name=", 5) == 0) {
                    char line[4096];
                    snprintf(line, sizeof(line), "%.*s",
                             (int)MIN(line_len, sizeof(line) - 1), p);
                    if (!strstr(line, "Nightly")) {
                        str_append(&new_content, "%.*s Nightly\n", (int)line_len, p);
                    } else {
                        found_nightly = 1;
                        str_append_buf(&new_content, p, line_len);
                        str_append_c(&new_content, '\n');
                    }
                } else {
                    str_append_buf(&new_content, p, line_len);
                    str_append_c(&new_content, '\n');
                }

                if (!nl) break;
                p = nl + 1;
            }
            file_write(de.path, (const uint8_t *)new_content.buf, new_content.len);
            str_free(&new_content);
            free(content);
        }

        if (cfg->update_info) {
            const char *info = cfg->update_info;
            const char *latest = strstr(info, "|latest|");
            if (latest) {
                str_append_buf(&patched_update, info, (size_t)(latest - info));
                str_append(&patched_update, "|nightly|");
                str_append(&patched_update, "%s", latest + 7);
                update_info = patched_update.buf;
            }
        }
    }

    /* Add X-AppImage-* metadata */
    if (desktop_add_metadata(&de, app_name.buf, version, cfg->appimage_arch) != 0) {
        desktop_entry_free(&de);
        str_free(&app_name);
        str_free(&patched_update);
        return -1;
    }

    /* Compute output filename */
    Str output_name = str_new();
    if (cfg->output_name) {
        str_append(&output_name, "%s", cfg->output_name);
    } else {
        desktop_compute_output_name(&output_name, app_name.buf, version, cfg->arch);
    }

    mkdir_p(cfg->output_dir);

    Str output_path = str_new();
    str_append(&output_path, "%s/%s", cfg->output_dir, output_name.buf);

    /* Resolve runtime */
    char *runtime_path = NULL;
    if (uruntime_resolve(cfg, &runtime_path) != 0) {
        desktop_entry_free(&de);
        str_free(&app_name);
        str_free(&patched_update);
        str_free(&output_name);
        str_free(&output_path);
        return -1;
    }
    LOG_INFO("Using runtime: %s", runtime_path);

    /* Configure runtime */
    if (uruntime_configure(runtime_path, update_info,
                           (const char **)cfg->env_vars, cfg->env_vars_count,
                           cfg->keep_mount) != 0) {
        free(runtime_path);
        desktop_entry_free(&de);
        str_free(&app_name);
        str_free(&patched_update);
        str_free(&output_name);
        str_free(&output_path);
        return -1;
    }

    /* Resolve mkdwarfs */
    char *mkdwarfs_path = NULL;
    if (dwarfs_resolve(cfg, &mkdwarfs_path) != 0) {
        free(runtime_path);
        desktop_entry_free(&de);
        str_free(&app_name);
        str_free(&patched_update);
        str_free(&output_name);
        str_free(&output_path);
        return -1;
    }
    LOG_INFO("Using mkdwarfs: %s", mkdwarfs_path);

    /* Optional profiling pass */
    char *profile_to_use = NULL;

    if (cfg->optimize_launch) {
        if (dwarfs_check_fuse() != 0) {
            goto cleanup;
        }

        char *tmp_appimage = process_unique_path(cfg->tmpdir, ".analyze");
        if (dwarfs_build_profile_image(mkdwarfs_path, cfg->appdir,
                                       runtime_path, tmp_appimage) != 0) {
            free(tmp_appimage);
            goto cleanup;
        }

        set_executable(tmp_appimage);

        const char *profile_path = cfg->dwarfs_profile;
        if (!profile_path) {
            Str def = str_new();
            str_append(&def, "%s/.dwarfsprofile", cfg->appdir);
            profile_to_use = str_detach(&def);
        } else {
            profile_to_use = strdup(profile_path);
        }

        if (dwarfs_run_profiling(tmp_appimage, profile_to_use,
                                 cfg->tmpdir, cfg->profile_timeout) != 0) {
            free(tmp_appimage);
            goto cleanup;
        }

        free(tmp_appimage);
    } else if (cfg->dwarfs_profile) {
        profile_to_use = strdup(cfg->dwarfs_profile);
    }

    /* Build final AppImage */
    if (dwarfs_build_appimage(mkdwarfs_path, cfg->appdir, runtime_path,
                              output_path.buf, cfg->dwarfs_comp,
                              profile_to_use) != 0) {
        goto cleanup;
    }

    set_executable(output_path.buf);

    /* Generate zsync */
    if (update_info) {
        zsync_generate(output_path.buf, output_name.buf,
                       cfg->output_dir, update_info);
    }

    /* Write appinfo */
    write_appinfo(cfg->output_dir, app_name.buf, version, cfg->appimage_arch);

    LOG_INFO("All done! AppImage at: %s", output_path.buf);

cleanup:
    free(runtime_path);
    free(mkdwarfs_path);
    free(profile_to_use);
    desktop_entry_free(&de);
    str_free(&app_name);
    str_free(&patched_update);
    str_free(&output_name);
    str_free(&output_path);
    return 0;
}
