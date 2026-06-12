#include "appimagetool.h"

int main(int argc, char **argv)
{
    Config cfg;
    if (config_from_env(&cfg) != 0) {
        LOG_ERROR("Failed to initialize configuration from environment");
        return APPIMAGETOOL_EXIT_ERROR;
    }

    int ret = config_apply_cli(&cfg, argc, argv);
    if (ret != 0) {
        config_free(&cfg);
        return ret;
    }

    ret = appimage_build(&cfg);
    config_free(&cfg);

    if (ret != 0) {
        LOG_ERROR("Build failed");
        return APPIMAGETOOL_EXIT_ERROR;
    }

    return APPIMAGETOOL_EXIT_OK;
}
