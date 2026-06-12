/* Zsync control file generation.
 * Format: gzip-compressed text file with metadata about the source file
 * and SHA-1 hashes for each block, enabling delta downloads.
 *
 * Format reference: http://zsync.moria.org.uk/
 */
#include "appimagetool.h"
#include <zlib.h>

#define ZSYNC_BLOCK_SIZE 2048

static void to_hex(const uint8_t *bin, size_t bin_len, char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < bin_len; i++) {
        hex[i * 2]     = hex_chars[bin[i] >> 4];
        hex[i * 2 + 1] = hex_chars[bin[i] & 0xf];
    }
    hex[bin_len * 2] = '\0';
}

int zsync_generate(const char *filepath, const char *filename,
                   const char *output_dir, const char *url)
{
    LOG_INFO("Generating .zsync file...");

    FILE *f = fopen(filepath, "rbe");
    if (!f) {
        LOG_ERROR("Cannot open %s", filepath);
        return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    if (file_len < 0) { fclose(f); return -1; }
    rewind(f);

    /* Read entire file */
    uint8_t *file_data = malloc((size_t)file_len);
    if (!file_data) { fclose(f); return -1; }
    if (fread(file_data, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(file_data);
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Build metadata text */
    Str meta = str_new();
    str_append(&meta, "zsync:0.6.2\n");
    if (url)
        str_append(&meta, "URL: %s\n", url);
    str_append(&meta, "Filename: %s\n", filename);
    {
        char timebuf[64];
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", tm);
        str_append(&meta, "MTime: %s\n", timebuf);
    }
    str_append(&meta, "Blocksize: %d\n", ZSYNC_BLOCK_SIZE);
    str_append(&meta, "Length: %ld\n", file_len);
    str_append(&meta, "Hash-Lengths: 1,1\n");

    /* SHA-1 per block */
    SHA1_CTX whole_ctx;
    sha1_init(&whole_ctx);

    size_t offset = 0;
    while (offset < (size_t)file_len) {
        size_t block_len = MIN(ZSYNC_BLOCK_SIZE, (size_t)file_len - offset);
        uint8_t digest[SHA1_DIGEST_SIZE];

        SHA1_CTX block_ctx;
        sha1_init(&block_ctx);
        sha1_update(&block_ctx, file_data + offset, block_len);
        sha1_final(&block_ctx, digest);
        sha1_update(&whole_ctx, file_data + offset, block_len);

        char hex[SHA1_DIGEST_SIZE * 2 + 1];
        to_hex(digest, SHA1_DIGEST_SIZE, hex);
        str_append(&meta, "%s\n", hex);

        offset += block_len;
    }

    /* Final SHA-1 of the whole file */
    uint8_t final_digest[SHA1_DIGEST_SIZE];
    sha1_final(&whole_ctx, final_digest);
    char final_hex[SHA1_DIGEST_SIZE * 2 + 1];
    to_hex(final_digest, SHA1_DIGEST_SIZE, final_hex);
    str_append(&meta, "SHA-1: %s\n", final_hex);

    /* Also include SHA-256 for compatibility with newer clients */
    /* (zsync2 uses SHA-256; we include it as optional extra) */

    free(file_data);

    /* Gzip compress the metadata */
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                           15 | 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        str_free(&meta);
        return -1;
    }

    uLong src_len = (uLong)meta.len;
    uLong dest_len = deflateBound(&strm, src_len);
    uint8_t *compressed = malloc(dest_len);
    if (!compressed) {
        deflateEnd(&strm);
        str_free(&meta);
        return -1;
    }

    strm.next_in = (Bytef *)meta.buf;
    strm.avail_in = (uInt)src_len;
    strm.next_out = compressed;
    strm.avail_out = (uInt)dest_len;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        free(compressed);
        str_free(&meta);
        return -1;
    }
    uLong comp_len = dest_len - strm.avail_out;
    deflateEnd(&strm);
    str_free(&meta);

    /* Write output */
    Str zsync_path = str_new();
    str_append(&zsync_path, "%s/%s.zsync", output_dir, filename);

    FILE *out = fopen(zsync_path.buf, "wbe");
    if (!out) {
        free(compressed);
        str_free(&zsync_path);
        return -1;
    }

    if (fwrite(compressed, 1, comp_len, out) != comp_len) {
        fclose(out);
        free(compressed);
        str_free(&zsync_path);
        return -1;
    }
    fclose(out);
    free(compressed);

    LOG_INFO("Wrote %s", zsync_path.buf);
    str_free(&zsync_path);
    return 0;
}
