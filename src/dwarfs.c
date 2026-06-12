#include "appimagetool.h"
#include <sys/wait.h>
#include <dirent.h>

static const char *DEFAULT_DWARFS_URL_TEMPLATE =
    "https://github.com/mhx/dwarfs/releases/download/v0.15.3/"
    "dwarfs-universal-0.15.3-Linux-{arch}";

int dwarfs_resolve(const Config *cfg, char **out_path)
{
    if (cfg->mkdwarfs) {
        if (access(cfg->mkdwarfs, F_OK) == 0) {
            *out_path = strdup(cfg->mkdwarfs);
            return 0;
        }
        LOG_ERROR("mkdwarfs not found at %s", cfg->mkdwarfs);
        return -1;
    }

    /* Check $PATH */
    {
        const char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");
            while (dir) {
                char candidate[4096];
                snprintf(candidate, sizeof(candidate), "%s/mkdwarfs", dir);
                struct stat st;
                if (stat(candidate, &st) == 0 && (st.st_mode & 0111)) {
                    *out_path = strdup(candidate);
                    free(path_copy);
                    return 0;
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }

    Str cached = str_new();
    str_append(&cached, "%s/mkdwarfs", cfg->tmpdir);

    Str url = str_new();
    if (cfg->dwarfs_url)
        str_append(&url, "%s", cfg->dwarfs_url);
    else
        str_append(&url, "%s", DEFAULT_DWARFS_URL_TEMPLATE);

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

    *out_path = str_detach(&cached);
    str_free(&url);
    return 0;
}

static int run_cmd(const char **argv)
{
    pid_t pid = fork();
    if (pid == -1) return -1;
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int dwarfs_build_appimage(const char *mkdwarfs, const char *appdir,
                          const char *runtime, const char *output,
                          const char *compression, const char *profile)
{
    LOG_INFO("Building DWARFS AppImage...");

    /* Count args: mkdwarfs + fixed args + compression + profile + output */
    const char *argv[64];
    int argc = 0;

    argv[argc++] = mkdwarfs;
    argv[argc++] = "--force";
    argv[argc++] = "--order=path";
    argv[argc++] = "--set-owner";
    argv[argc++] = "0";
    argv[argc++] = "--set-group";
    argv[argc++] = "0";
    argv[argc++] = "--no-history";
    argv[argc++] = "--no-create-timestamp";
    argv[argc++] = "--header";
    argv[argc++] = runtime;
    argv[argc++] = "--input";
    argv[argc++] = appdir;

    if (profile && access(profile, F_OK) == 0) {
        LOG_INFO("Using DWARFS profile %s...", profile);
        argv[argc++] = "--categorize=hotness";
        Str hl = str_new();
        str_append(&hl, "--hotness-list=%s", profile);
        argv[argc++] = str_detach(&hl);
    }

    /* Parse compression string into args */
    char comp_copy[4096];
    snprintf(comp_copy, sizeof(comp_copy), "%s", compression);
    char *tok = strtok(comp_copy, " ");
    bool first = true;
    while (tok && argc < 60) {
        if (first) {
            argv[argc++] = "-C";
            first = false;
        }
        argv[argc++] = tok;
        tok = strtok(NULL, " ");
    }

    argv[argc++] = "--output";
    argv[argc++] = output;
    argv[argc] = NULL;

    int status = run_cmd(argv);

    /* Free allocated hotness-list string if used */
    if (profile && access(profile, F_OK) == 0)
        free((char *)argv[argc - 4]); /* roughly */

    if (status != 0) {
        LOG_ERROR("mkdwarfs exited with status %d", status);
        return -1;
    }
    return 0;
}

int dwarfs_build_profile_image(const char *mkdwarfs, const char *appdir,
                               const char *runtime, const char *output)
{
    LOG_INFO("Building temporary image for DWARFS profiling...");

    const char *argv[] = {
        mkdwarfs, "--force", "--order=path",
        "--set-owner", "0", "--set-group", "0",
        "--no-history", "--no-create-timestamp",
        "--header", runtime,
        "--input", appdir,
        "-C", "zstd:level=5",
        "-S19",
        "--output", output,
        NULL
    };

    int status = run_cmd(argv);
    if (status != 0) {
        LOG_ERROR("mkdwarfs (profile build) exited with status %d", status);
        return -1;
    }
    return 0;
}

int dwarfs_check_fuse(void)
{
    if (access("/dev/fuse", F_OK) != 0) {
        LOG_ERROR("FUSE is not available: /dev/fuse is missing");
        return -1;
    }
    return 0;
}

static int snapshot_dir(const char *dir, char ***mounts, size_t *count)
{
    DIR *d = opendir(dir);
    if (!d) return 0;

    *count = 0;
    *mounts = NULL;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, ".mount_", 7) != 0) continue;

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        (*count)++;
        *mounts = realloc(*mounts, *count * sizeof(char *));
        (*mounts)[*count - 1] = strdup(full);
    }
    closedir(d);
    return 0;
}

static int is_in_list(const char *path, char **list, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (strcmp(path, list[i]) == 0) return 1;
    return 0;
}

static void unmount(const char *mountpoint)
{
    pid_t pid = fork();
    if (pid == 0) {
        execlp("fusermount", "fusermount", "-u", mountpoint, NULL);
        execlp("umount", "umount", mountpoint, NULL);
        execlp("umount", "umount", "-l", mountpoint, NULL);
        _exit(1);
    }
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

int dwarfs_run_profiling(const char *appimage, const char *profile_output,
                         const char *tmpdir, uint64_t timeout_secs)
{
    char *tmp_profile = process_unique_path(tmpdir, "dwarfsprof.tmp");

    /* Snapshot pre-existing mounts */
    char **pre_mounts = NULL;
    size_t pre_count = 0;
    snapshot_dir(tmpdir, &pre_mounts, &pre_count);

    LOG_INFO("Running DWARFS profiling for %lu seconds...", (unsigned long)timeout_secs);

    pid_t child = fork();
    if (child == -1) {
        LOG_ERROR("fork failed");
        free(tmp_profile);
        for (size_t i = 0; i < pre_count; i++) free(pre_mounts[i]);
        free(pre_mounts);
        return -1;
    }

    if (child == 0) {
        /* Child: launch with profiling */
        setenv("TMPDIR", tmpdir, 1);
        setenv("DWARFS_ANALYSIS_FILE", tmp_profile, 1);
        /* Create process group */
        setpgid(0, 0);
        execlp("xvfb-run", "xvfb-run", "-a", "--", appimage, NULL);
        _exit(127);
    }

    /* Parent: wait for timeout */
    struct timespec ts = { (time_t)timeout_secs, 0 };
    while (ts.tv_sec > 0 || ts.tv_nsec > 0) {
        int status;
        pid_t ret = waitpid(child, &status, WNOHANG);
        if (ret == child) break; /* process exited early */
        if (ret == -1) break;
        struct timespec rem;
        if (nanosleep(&ts, &rem) == 0) break;
        ts = rem;
    }

    /* Send SIGTERM to process group */
    kill(-child, SIGTERM);

    /* Wait a bit */
    ts = (struct timespec){ 3, 0 };
    while (ts.tv_sec > 0 || ts.tv_nsec > 0) {
        int status;
        pid_t ret = waitpid(child, &status, WNOHANG);
        if (ret == child) break;
        struct timespec rem;
        if (nanosleep(&ts, &rem) == 0) break;
        ts = rem;
    }

    /* Check if still alive */
    int status;
    pid_t ret = waitpid(child, &status, WNOHANG);
    if (ret == 0) {
        LOG_WARN("Process did not respond to SIGTERM, sending SIGKILL");
        kill(-child, SIGKILL);
        ts = (struct timespec){ 2, 0 };
        while (ts.tv_sec > 0 || ts.tv_nsec > 0) {
            ret = waitpid(child, &status, WNOHANG);
            if (ret == child) break;
            struct timespec rem;
            if (nanosleep(&ts, &rem) == 0) break;
            ts = rem;
        }
    }

    /* Unmount new mounts */
    char **post_mounts = NULL;
    size_t post_count = 0;
    snapshot_dir(tmpdir, &post_mounts, &post_count);

    for (size_t i = 0; i < post_count; i++) {
        if (!is_in_list(post_mounts[i], pre_mounts, pre_count))
            unmount(post_mounts[i]);
    }

    for (size_t i = 0; i < post_count; i++) free(post_mounts[i]);
    free(post_mounts);
    for (size_t i = 0; i < pre_count; i++) free(pre_mounts[i]);
    free(pre_mounts);

    /* Give profile writer a moment */
    nanosleep(&(struct timespec){ 2, 0 }, NULL);

    if (access(tmp_profile, F_OK) == 0) {
        /* Copy profile file */
        uint8_t *buf = NULL;
        size_t len = 0;
        if (file_read(tmp_profile, &buf, &len) == 0) {
            file_write(profile_output, buf, len);
            free(buf);
        }
        unlink(tmp_profile);
        LOG_INFO("DWARFS profile written to %s", profile_output);
    } else {
        LOG_WARN("DWARFS profile was not generated");
    }

    free(tmp_profile);
    unlink(appimage);

    return 0;
}
