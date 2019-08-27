#include "qemu/osdep.h"
#include "libqtest.h"

const char common_args[] = "-nodefaults -machine none";

static void test_modules_load(const void *data)
{
    QTestState *qts;
    const char **args = (const char **)data;

    qts = qtest_init(common_args);
    qtest_module_load(qts, args[0], args[1]);
    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    const char *modules[] = {
#ifdef CONFIG_CURL
        "block-", "curl",
#endif
#ifdef CONFIG_GLUSTERFS
        "block-", "gluster",
#endif
#ifdef CONFIG_LIBISCSI
        "block-", "iscsi",
#endif
#ifdef CONFIG_LIBNFS
        "block-", "nfs",
#endif
#ifdef CONFIG_LIBSSH
        "block-", "ssh",
#endif
#ifdef CONFIG_RBD
        "block-", "rbd",
#endif
#ifdef CONFIG_AUDIO_ALSA
        "audio-", "alsa",
#endif
#ifdef CONFIG_AUDIO_OSS
        "audio-", "oss",
#endif
#ifdef CONFIG_AUDIO_PA
        "audio-", "pa",
#endif
#ifdef CONFIG_AUDIO_SDL
        "audio-", "sdl",
#endif
#ifdef CONFIG_CURSES
        "ui-", "curses",
#endif
#if defined(CONFIG_GTK) && defined(CONFIG_VTE)
        "ui-", "gtk",
#endif
#ifdef CONFIG_SDL
        "ui-", "sdl",
#endif
#if defined(CONFIG_SPICE) && defined(CONFIG_GIO)
        "ui-", "spice-app",
#endif
    };
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < G_N_ELEMENTS(modules); i += 2) {
        char *testname = g_strdup_printf("/module/load/%s", modules[i + 1]);
        qtest_add_data_func(testname, modules + i, test_modules_load);
        g_free(testname);
    }

    return g_test_run();
}
