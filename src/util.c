#include "appimagetool.h"
#include <curl/curl.h>
#include <fcntl.h>
#include <sys/wait.h>

/* ------------------------------------------------------------------ */
/*  Dynamic string                                                      */
/* ------------------------------------------------------------------ */

Str str_new(void)
{
    Str s = {0};
    return s;
}

Str str_new_from(const char *src)
{
    Str s = {0};
    if (src) str_append(&s, "%s", src);
    return s;
}

void str_free(Str *s)
{
    free(s->buf);
    s->buf = NULL;
    s->len = s->cap = 0;
}

int str_append_v(Str *s, const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (needed < 0) return -1;

    size_t new_len = s->len + (size_t)needed;
    if (new_len + 1 > s->cap) {
        size_t new_cap = s->cap ? s->cap * 2 : 128;
        while (new_cap < new_len + 1) new_cap *= 2;
        char *new_buf = realloc(s->buf, new_cap);
        if (!new_buf) return -1;
        s->buf = new_buf;
        s->cap = new_cap;
    }

    vsnprintf(s->buf + s->len, (size_t)needed + 1, fmt, ap);
    s->len = new_len;
    return 0;
}

int str_append(Str *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = str_append_v(s, fmt, ap);
    va_end(ap);
    return ret;
}

int str_append_buf(Str *s, const char *data, size_t len)
{
    if (s->len + len + 1 > s->cap) {
        size_t new_cap = s->cap ? s->cap * 2 : 128;
        while (new_cap < s->len + len + 1) new_cap *= 2;
        char *new_buf = realloc(s->buf, new_cap);
        if (!new_buf) return -1;
        s->buf = new_buf;
        s->cap = new_cap;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    s->buf[s->len] = '\0';
    return 0;
}

int str_append_c(Str *s, char c)
{
    return str_append_buf(s, &c, 1);
}

char *str_detach(Str *s)
{
    char *ret = s->buf;
    s->buf = NULL;
    s->len = s->cap = 0;
    return ret;
}

/* ------------------------------------------------------------------ */
/*  HTTP download                                                       */
/* ------------------------------------------------------------------ */

struct WriteBuf {
    uint8_t *data;
    size_t   len;
    size_t   cap;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *user)
{
    struct WriteBuf *wb = user;
    size_t total = size * nmemb;
    if (wb->len + total > wb->cap) {
        size_t new_cap = wb->cap ? wb->cap * 2 : 65536;
        while (new_cap < wb->len + total) new_cap *= 2;
        uint8_t *new_data = realloc(wb->data, new_cap);
        if (!new_data) return 0;
        wb->data = new_data;
        wb->cap = new_cap;
    }
    memcpy(wb->data + wb->len, ptr, total);
    wb->len += total;
    return total;
}

static int try_download(const char *url, const char *dest)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    struct WriteBuf wb = {0};
    char staging[4096];
    snprintf(staging, sizeof(staging), "%s.partial.%d", dest, getpid());

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wb);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "appimagetool/" APPIMAGETOOL_VERSION);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(wb.data);
        return -1;
    }

    FILE *f = fopen(staging, "wbe");
    if (!f) { free(wb.data); return -1; }
    if (fwrite(wb.data, 1, wb.len, f) != wb.len) {
        fclose(f);
        unlink(staging);
        free(wb.data);
        return -1;
    }
    fclose(f);

    if (rename(staging, dest) != 0) {
        unlink(staging);
        free(wb.data);
        return -1;
    }

    free(wb.data);
    return 0;
}

int download_file(const char *url, const char *dest)
{
    for (int attempt = 0; attempt < 5; attempt++) {
        if (attempt > 0) {
            LOG_WARN("Download failed, retrying in 5s...");
            sleep(5);
        }
        if (try_download(url, dest) == 0)
            return 0;
    }
    LOG_ERROR("Failed to download %s after 5 attempts", url);
    return -1;
}

/* ------------------------------------------------------------------ */
/*  ELF detection                                                       */
/* ------------------------------------------------------------------ */

int is_elf(const char *path)
{
    FILE *f = fopen(path, "rbe");
    if (!f) return 0;
    uint8_t magic[4];
    int ok = (fread(magic, 1, 4, f) == 4 && memcmp(magic, "\x7f" "ELF", 4) == 0);
    fclose(f);
    return ok;
}

/* ------------------------------------------------------------------ */
/*  File utilities                                                      */
/* ------------------------------------------------------------------ */

int set_executable(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    mode_t mode = st.st_mode | 0111;
    return chmod(path, mode);
}

int ensure_cached_binary(const char *cached, const char *url)
{
    if (access(cached, F_OK) == 0 && is_elf(cached))
        return 0;

    LOG_INFO("Downloading %s from %s", cached, url);
    if (download_file(url, cached) != 0)
        return -1;

    set_executable(cached);

    if (!is_elf(cached)) {
        unlink(cached);
        LOG_ERROR("Downloaded file is not a valid ELF binary: %s", cached);
        return -1;
    }
    return 0;
}

int mkdir_p(const char *path)
{
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

int file_read(const char *path, uint8_t **data, size_t *len)
{
    FILE *f = fopen(path, "rbe");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen < 0) { fclose(f); return -1; }
    rewind(f);
    *data = malloc((size_t)flen + 1);
    if (!*data) { fclose(f); return -1; }
    *len = fread(*data, 1, (size_t)flen, f);
    fclose(f);
    (*data)[*len] = '\0';
    return 0;
}

int file_write(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wbe");
    if (!f) return -1;
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

char *env_get(const char *name)
{
    const char *val = getenv(name);
    if (!val) return NULL;
    return strdup(val);
}

bool env_truthy(const char *name)
{
    const char *val = getenv(name);
    if (!val) return false;
    return strcmp(val, "1") == 0 ||
           strcasecmp(val, "true") == 0 ||
           strcasecmp(val, "yes") == 0 ||
           strcasecmp(val, "on") == 0;
}

char *staging_path(const char *dest)
{
    Str s = str_new();
    str_append(&s, "%s.partial.%d", dest, getpid());
    return str_detach(&s);
}

char *process_unique_path(const char *dir, const char *basename)
{
    Str s = str_new();
    str_append(&s, "%s/%s.%d", dir, basename, getpid());
    return str_detach(&s);
}

int sanitize_filename(Str *out, const char *s)
{
    str_free(out);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c == 0x7f ||
            c == '"' || c == ':' || c == '>' || c == '<' ||
            c == '*' || c == '|' || c == '?' || c == '/' ||
            c == '\\') {
            str_append_c(out, '_');
        } else {
            str_append_c(out, (char)c);
        }
    }
    /* trim trailing underscores */
    while (out->len > 0 && out->buf[out->len - 1] == '_')
        out->buf[--out->len] = '\0';
    return 0;
}
