#include "appimagetool.h"

uint16_t elf_r16(const uint8_t *data, size_t off, bool le)
{
    uint16_t val;
    memcpy(&val, data + off, 2);
    return le ? val : __builtin_bswap16(val);
}

uint32_t elf_r32(const uint8_t *data, size_t off, bool le)
{
    uint32_t val;
    memcpy(&val, data + off, 4);
    return le ? val : __builtin_bswap32(val);
}

uint64_t elf_r64(const uint8_t *data, size_t off, bool le)
{
    uint64_t val;
    memcpy(&val, data + off, 8);
    return le ? val : __builtin_bswap64(val);
}

bool elf_parse(ElfInfo *info, const uint8_t *data, size_t data_len)
{
    if (data_len < 16) return false;
    if (memcmp(data, "\x7f" "ELF", 4) != 0) return false;

    bool is_64bit = (data[4] == 2);
    bool is_le    = (data[5] == 1);

    uint64_t sh_off, sh_entsize, sh_num, sh_strndx;

    if (is_64bit) {
        if (data_len < 64) return false;
        sh_off     = elf_r64(data, 40, is_le);
        sh_entsize = elf_r16(data, 58, is_le);
        sh_num     = elf_r16(data, 60, is_le);
        sh_strndx  = elf_r16(data, 62, is_le);
    } else {
        if (data_len < 52) return false;
        sh_off     = elf_r32(data, 32, is_le);
        sh_entsize = elf_r16(data, 46, is_le);
        sh_num     = elf_r16(data, 48, is_le);
        sh_strndx  = elf_r16(data, 50, is_le);
    }

    if (sh_off == 0 || sh_entsize == 0 || sh_num == 0 || sh_strndx >= sh_num)
        return false;

    info->is_64bit   = is_64bit;
    info->is_le      = is_le;
    info->sh_off     = sh_off;
    info->sh_entsize = sh_entsize;
    info->sh_num     = sh_num;
    info->sh_strndx  = sh_strndx;

    return true;
}

static size_t shdr_offset(ElfInfo *info, uint64_t idx, size_t data_len)
{
    uint64_t off = info->sh_off + idx * info->sh_entsize;
    if (off > (uint64_t)data_len) return SIZE_MAX;
    return (size_t)off;
}

int elf_find_section(ElfInfo *info, const uint8_t *data, size_t data_len,
                     const char *name, uint64_t *idx)
{
    size_t strtab_off = shdr_offset(info, info->sh_strndx, data_len);
    if (strtab_off == SIZE_MAX) return -1;

    size_t strtab_off2, strtab_size;
    if (info->is_64bit) {
        if (strtab_off + 40 > data_len) return -1;
        strtab_off2  = (size_t)elf_r64(data, strtab_off + 24, info->is_le);
        strtab_size  = (size_t)elf_r64(data, strtab_off + 32, info->is_le);
    } else {
        if (strtab_off + 24 > data_len) return -1;
        strtab_off2  = elf_r32(data, strtab_off + 16, info->is_le);
        strtab_size  = elf_r32(data, strtab_off + 20, info->is_le);
    }

    if (strtab_off2 > data_len || strtab_size > data_len - strtab_off2)
        return -1;

    const uint8_t *strtab = data + strtab_off2;
    size_t name_len = strlen(name);

    for (uint64_t i = 0; i < info->sh_num; i++) {
        size_t hdr_off = shdr_offset(info, i, data_len);
        if (hdr_off == SIZE_MAX) return -1;
        if (hdr_off + 4 > data_len) return -1;

        uint32_t name_idx = elf_r32(data, hdr_off, info->is_le);
        if (name_idx >= strtab_size) continue;
        if (name_idx + name_len >= strtab_size) continue;

        if (memcmp(strtab + name_idx, name, name_len) == 0 &&
            strtab[name_idx + name_len] == '\0') {
            *idx = i;
            return 0;
        }
    }
    return -1;
}

static int read_offset_size(ElfInfo *info, const uint8_t *data, size_t data_len,
                            uint64_t idx, size_t *offset, size_t *size)
{
    size_t hdr_off = shdr_offset(info, idx, data_len);
    if (hdr_off == SIZE_MAX) return -1;

    if (info->is_64bit) {
        if (hdr_off + 40 > data_len) return -1;
        *offset = (size_t)elf_r64(data, hdr_off + 24, info->is_le);
        *size   = (size_t)elf_r64(data, hdr_off + 32, info->is_le);
    } else {
        if (hdr_off + 24 > data_len) return -1;
        *offset = elf_r32(data, hdr_off + 16, info->is_le);
        *size   = elf_r32(data, hdr_off + 20, info->is_le);
    }
    return 0;
}

int elf_read_section(const uint8_t *data, size_t data_len,
                     const char *name, const uint8_t **out, size_t *out_len)
{
    ElfInfo info;
    if (!elf_parse(&info, data, data_len)) return -1;

    uint64_t idx;
    if (elf_find_section(&info, data, data_len, name, &idx) != 0)
        return -1;

    size_t offset, size;
    if (read_offset_size(&info, data, data_len, idx, &offset, &size) != 0)
        return -1;

    if (offset > data_len || size > data_len - offset)
        return -1;

    *out = data + offset;
    *out_len = size;
    return 0;
}

int elf_write_section(uint8_t *data, size_t data_len,
                      const char *name, const uint8_t *value, size_t value_len)
{
    ElfInfo info;
    if (!elf_parse(&info, data, data_len)) {
        LOG_ERROR("Runtime is not a valid ELF binary");
        return -1;
    }

    uint64_t idx;
    if (elf_find_section(&info, data, data_len, name, &idx) != 0) {
        LOG_ERROR("ELF section '%s' not found in runtime", name);
        return -1;
    }

    size_t offset, size;
    if (read_offset_size(&info, data, data_len, idx, &offset, &size) != 0)
        return -1;

    if (offset > data_len || size > data_len - offset) {
        LOG_ERROR("Malformed ELF section headers");
        return -1;
    }

    if (value_len > size) {
        LOG_ERROR("Data too large for ELF section '%s': %zu > %zu bytes",
                  name, value_len, size);
        return -1;
    }

    memset(data + offset, 0, size);
    memcpy(data + offset, value, value_len);
    return 0;
}

int elf_write_section_file(const char *path, const char *name,
                           const uint8_t *value, size_t value_len)
{
    uint8_t *data = NULL;
    size_t data_len = 0;
    if (file_read(path, &data, &data_len) != 0) {
        LOG_ERROR("Failed to read %s", path);
        return -1;
    }

    int ret = elf_write_section(data, data_len, name, value, value_len);
    if (ret != 0) {
        free(data);
        return ret;
    }

    ret = file_write(path, data, data_len);
    free(data);
    return ret;
}
