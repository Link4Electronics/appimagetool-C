#include "appimagetool.h"
#include <getopt.h>

static const char *DEFAULT_DWARFS_COMP = "zstd:level=22 -S26 -B6";
static const char *DEFAULT_RUNTIME_URL =
    "https://github.com/Link4Electronics/uruntime-C/releases/download/v0.6.0/"
    "uruntime-appimage-dwarfs-lite-{arch}";
static const char *DEFAULT_DWARFS_URL =
    "https://github.com/mhx/dwarfs/releases/download/v0.15.3/"
    "dwarfs-universal-0.15.3-Linux-{arch}";

static const char *host_arch(void)
{
#if defined(__x86_64__) || defined(__amd64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#elif defined(__i386__) || defined(__i686__)
    return "i386";
#elif defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
    return "ppc64le";
#elif defined(__powerpc64__)
    return "ppc64";
#elif defined(__powerpc__)
    return "ppc";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64";
#elif defined(__loongarch64)
    return "loongarch64";
#elif defined(__s390x__)
    return "s390x";
#else
    return "unknown";
#endif
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rbe");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
    return buf;
}

static char *strip_epoch(const char *version)
{
    const char *colon = strchr(version, ':');
    if (colon)
        return strdup(colon + 1);
    return strdup(version);
}

static char *resolve_version(void)
{
    char *v = env_get("VERSION");
    if (v) {
        char *stripped = strip_epoch(v);
        free(v);
        return stripped;
    }
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (home) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/version", home);
        char *content = read_file(path);
        if (content) {
            char *stripped = strip_epoch(content);
            free(content);
            return stripped;
        }
    }
    return NULL;
}

void config_free(Config *cfg)
{
    free(cfg->appdir);
    free(cfg->output_dir);
    free(cfg->output_name);
    free(cfg->appimage_arch);
    free(cfg->arch);
    free(cfg->runtime);
    free(cfg->runtime_url);
    free(cfg->dwarfs_comp);
    free(cfg->update_info);
    free(cfg->dwarfs_profile);
    free(cfg->mkdwarfs);
    free(cfg->dwarfs_url);
    free(cfg->tmpdir);
    free(cfg->version);
    for (size_t i = 0; i < cfg->env_vars_count; i++)
        free(cfg->env_vars[i]);
    free(cfg->env_vars);
    memset(cfg, 0, sizeof(*cfg));
}

int config_from_env(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->appdir = env_get("APPDIR");
    if (!cfg->appdir) cfg->appdir = strdup("./AppDir");

    cfg->output_dir = env_get("OUTPATH");
    if (!cfg->output_dir) cfg->output_dir = strdup(".");

    cfg->output_name = env_get("OUTNAME");

    cfg->appimage_arch = env_get("APPIMAGE_ARCH");
    if (!cfg->appimage_arch)
        cfg->appimage_arch = strdup(host_arch());

    cfg->arch = env_get("ARCH");
    if (!cfg->arch)
        cfg->arch = strdup(cfg->appimage_arch);

    cfg->runtime = env_get("RUNTIME");
    cfg->runtime_url = env_get("URUNTIME_LINK");

    cfg->dwarfs_comp = env_get("DWARFS_COMP");
    if (!cfg->dwarfs_comp)
        cfg->dwarfs_comp = strdup(DEFAULT_DWARFS_COMP);

    cfg->update_info = env_get("UPINFO");
    if (!cfg->update_info) {
        char *repo = env_get("GITHUB_REPOSITORY");
        if (repo) {
            char *slash = strchr(repo, '/');
            if (slash) {
                *slash = '\0';
                char buf[4096];
                snprintf(buf, sizeof(buf),
                         "gh-releases-zsync|%s|%s|latest|*-%s.AppImage.zsync",
                         repo, slash + 1, cfg->arch);
                cfg->update_info = strdup(buf);
            }
            free(repo);
        }
    }

    cfg->dwarfs_profile = env_get("DWARFSPROF");

    cfg->mkdwarfs = env_get("DWARFS_CMD");
    cfg->dwarfs_url = env_get("DWARFS_LINK");

    cfg->tmpdir = env_get("TMPDIR");
    if (!cfg->tmpdir) cfg->tmpdir = strdup("/tmp");

    cfg->optimize_launch = env_truthy("OPTIMIZE_LAUNCH");
    cfg->keep_mount = env_truthy("URUNTIME_PRELOAD");
    cfg->devel_release = env_truthy("DEVEL_RELEASE");

    char *timeout_str = env_get("OPTIMIZE_LAUNCH_TIMEOUT");
    cfg->profile_timeout = 10;
    if (timeout_str) {
        char *end;
        unsigned long val = strtoul(timeout_str, &end, 10);
        if (*end == '\0') cfg->profile_timeout = val;
        free(timeout_str);
    }

    char *vars = env_get("ADD_PERMA_ENV_VARS");
    if (vars) {
        size_t count = 1;
        for (char *p = vars; *p; p++)
            if (*p == '\n') count++;
        cfg->env_vars = calloc(count, sizeof(char *));
        cfg->env_vars_count = 0;
        char *line = strtok(vars, "\n");
        while (line) {
            cfg->env_vars[cfg->env_vars_count++] = strdup(line);
            line = strtok(NULL, "\n");
        }
    }

    cfg->version = resolve_version();

    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] [APPDIR]\n\n"
        "Create AppImages from an AppDir using DWARFS compression and uruntime.\n\n"
        "Arguments:\n"
        "  [APPDIR]                    Path to the AppDir directory [env: APPDIR]\n"
        "                              [default: ./AppDir]\n\n"
        "Options:\n"
        "  -o, --output <DIR>          Output directory [env: OUTPATH] [default: .]\n"
        "  -n, --name <NAME>           Output filename (auto-detected from .desktop)\n"
        "                              [env: OUTNAME]\n"
        "      --appimage-arch <ARCH>  Runtime arch (download URL + X-AppImage-Arch)\n"
        "                              [env: APPIMAGE_ARCH]\n"
        "      --arch <ARCH>           Display arch in output filename [env: ARCH]\n"
        "      --runtime <PATH>        Path to uruntime binary [env: RUNTIME]\n"
        "      --runtime-url <URL>     URL to download uruntime [env: URUNTIME_LINK]\n"
        "  -u, --update-info <INFO>    Update information string [env: UPINFO]\n"
        "      --dwarfs-comp <COMP>    DWARFS compression options [env: DWARFS_COMP]\n"
        "      --optimize-launch       Enable DWARFS profile optimization\n"
        "      --profile-timeout <S>   Profiling timeout in seconds [default: 10]\n"
        "                              [env: OPTIMIZE_LAUNCH_TIMEOUT]\n"
        "      --keep-mount            Keep FUSE mount alive after exit\n"
        "      --devel-release         Tag as nightly/devel release\n"
        "      --dwarfs-profile <FILE> Path to DWARFS profile [env: DWARFSPROF]\n"
        "      --mkdwarfs <PATH>       Path to mkdwarfs binary [env: DWARFS_CMD]\n"
        "      --dwarfs-url <URL>      URL to download mkdwarfs [env: DWARFS_LINK]\n"
        "      --tmpdir <DIR>          Temporary directory [env: TMPDIR] [default: /tmp]\n"
        "  -v, --verbose               Increase verbosity (-v, -vv)\n"
        "  -q, --quiet                 Suppress informational output (-q, -qq)\n"
        "  -h, --help                  Print help\n"
        "  -V, --version               Print version\n",
        prog);
}

int config_apply_cli(Config *cfg, int argc, char **argv)
{
    enum {
        OPT_APPIMAGE_ARCH = 256,
        OPT_ARCH,
        OPT_RUNTIME,
        OPT_RUNTIME_URL,
        OPT_DWARFS_COMP,
        OPT_OPTIMIZE_LAUNCH,
        OPT_PROFILE_TIMEOUT,
        OPT_KEEP_MOUNT,
        OPT_DEVEL_RELEASE,
        OPT_DWARFS_PROFILE,
        OPT_MKDWARFS,
        OPT_DWARFS_URL,
        OPT_TMPDIR,
    };

    static const struct option long_opts[] = {
        {"output",          required_argument, NULL, 'o'},
        {"name",            required_argument, NULL, 'n'},
        {"update-info",     required_argument, NULL, 'u'},
        {"appimage-arch",   required_argument, NULL, OPT_APPIMAGE_ARCH},
        {"arch",            required_argument, NULL, OPT_ARCH},
        {"runtime",         required_argument, NULL, OPT_RUNTIME},
        {"runtime-url",     required_argument, NULL, OPT_RUNTIME_URL},
        {"dwarfs-comp",     required_argument, NULL, OPT_DWARFS_COMP},
        {"optimize-launch", no_argument,       NULL, OPT_OPTIMIZE_LAUNCH},
        {"profile-timeout", required_argument, NULL, OPT_PROFILE_TIMEOUT},
        {"keep-mount",      no_argument,       NULL, OPT_KEEP_MOUNT},
        {"devel-release",   no_argument,       NULL, OPT_DEVEL_RELEASE},
        {"dwarfs-profile",  required_argument, NULL, OPT_DWARFS_PROFILE},
        {"mkdwarfs",        required_argument, NULL, OPT_MKDWARFS},
        {"dwarfs-url",      required_argument, NULL, OPT_DWARFS_URL},
        {"tmpdir",          required_argument, NULL, OPT_TMPDIR},
        {"verbose",         no_argument,       NULL, 'v'},
        {"quiet",           no_argument,       NULL, 'q'},
        {"help",            no_argument,       NULL, 'h'},
        {"version",         no_argument,       NULL, 'V'},
        {0, 0, 0, 0}
    };

    int verbose = 0;
    int quiet = 0;

    optind = 1;
    opterr = 0;

    while (1) {
        int c = getopt_long(argc, argv, "o:n:u:vqVh", long_opts, NULL);
        if (c == -1) break;

        switch (c) {
        case 'o':
            free(cfg->output_dir);
            cfg->output_dir = strdup(optarg);
            break;
        case 'n':
            free(cfg->output_name);
            cfg->output_name = strdup(optarg);
            break;
        case 'u':
            free(cfg->update_info);
            cfg->update_info = strdup(optarg);
            break;
        case OPT_APPIMAGE_ARCH:
            free(cfg->appimage_arch);
            cfg->appimage_arch = strdup(optarg);
            break;
        case OPT_ARCH:
            free(cfg->arch);
            cfg->arch = strdup(optarg);
            break;
        case OPT_RUNTIME:
            free(cfg->runtime);
            cfg->runtime = strdup(optarg);
            break;
        case OPT_RUNTIME_URL:
            free(cfg->runtime_url);
            cfg->runtime_url = strdup(optarg);
            break;
        case OPT_DWARFS_COMP:
            free(cfg->dwarfs_comp);
            cfg->dwarfs_comp = strdup(optarg);
            break;
        case OPT_OPTIMIZE_LAUNCH:
            cfg->optimize_launch = true;
            break;
        case OPT_PROFILE_TIMEOUT: {
            char *end;
            unsigned long val = strtoul(optarg, &end, 10);
            if (*end == '\0') cfg->profile_timeout = val;
            break;
        }
        case OPT_KEEP_MOUNT:
            cfg->keep_mount = true;
            break;
        case OPT_DEVEL_RELEASE:
            cfg->devel_release = true;
            break;
        case OPT_DWARFS_PROFILE:
            free(cfg->dwarfs_profile);
            cfg->dwarfs_profile = strdup(optarg);
            break;
        case OPT_MKDWARFS:
            free(cfg->mkdwarfs);
            cfg->mkdwarfs = strdup(optarg);
            break;
        case OPT_DWARFS_URL:
            free(cfg->dwarfs_url);
            cfg->dwarfs_url = strdup(optarg);
            break;
        case OPT_TMPDIR:
            free(cfg->tmpdir);
            cfg->tmpdir = strdup(optarg);
            break;
        case 'v':
            verbose++;
            break;
        case 'q':
            quiet++;
            break;
        case 'V':
            printf("appimagetool version " APPIMAGETOOL_VERSION "\n");
            exit(APPIMAGETOOL_EXIT_OK);
        case 'h':
            print_usage(argv[0]);
            exit(APPIMAGETOOL_EXIT_OK);
        default:
            fprintf(stderr, "Unknown option: %s\n", argv[optind - 1]);
            print_usage(argv[0]);
            return APPIMAGETOOL_EXIT_USAGE;
        }
    }

    if (optind < argc) {
        free(cfg->appdir);
        cfg->appdir = strdup(argv[optind]);
    }

    int verbosity = verbose - quiet;
    log_init(verbosity);

    return 0;
}
