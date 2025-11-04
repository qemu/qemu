/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/audio.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace/control.h"
#include "glib.h"

#include "audio/audio_int.h"

#include <math.h>
#ifdef CONFIG_SDL
/*
 * SDL insists on wrapping the main() function with its own implementation on
 * some platforms; it does so via a macro that renames our main function, so
 * <SDL.h> must be #included here even with no SDL code called from this file.
 */
#include <SDL.h>
#endif

#define SAMPLE_RATE     44100
#define CHANNELS        2
#define DURATION_SECS   2
#define FREQUENCY       440.0
#define BUFFER_FRAMES   1024
#define TIMEOUT_SECS    (DURATION_SECS + 1)

/* Command-line options */
static gchar *opt_audiodev;
static gchar *opt_trace;

static GOptionEntry test_options[] = {
    { "audiodev", 'a', 0, G_OPTION_ARG_STRING, &opt_audiodev,
      "Audio device spec (e.g., none or pa,out.buffer-length=50000)", "DEV" },
    { "trace", 'T', 0, G_OPTION_ARG_STRING, &opt_trace,
      "Trace options (e.g., 'pw_*')", "TRACE" },
    { NULL }
};

#define TEST_AUDIODEV_ID "test"

typedef struct TestSineState {
    AudioBackend *be;
    SWVoiceOut *voice;
    int64_t total_frames;
    int64_t frames_written;
} TestSineState;

/* Default audio settings for tests */
static const struct audsettings default_test_settings = {
    .freq = SAMPLE_RATE,
    .nchannels = CHANNELS,
    .fmt = AUDIO_FORMAT_S16,
    .endianness = 0,
};

static void dummy_audio_callback(void *opaque, int avail)
{
}

static AudioBackend *get_test_audio_backend(void)
{
    AudioBackend *be;
    Error *err = NULL;

    if (opt_audiodev) {
        be = audio_be_by_name(TEST_AUDIODEV_ID, &err);
    } else {
        be = audio_get_default_audio_be(&err);
    }

    if (err) {
        g_error("%s", error_get_pretty(err));
        error_free(err);
        exit(1);
    }
    g_assert_nonnull(be);
    return be;
}

/*
 * Helper functions for opening test voices with default settings.
 * These reduce boilerplate in test functions.
 */
static SWVoiceOut *open_test_voice_out(AudioBackend *be, const char *name,
                                       void *opaque, audio_callback_fn cb)
{
    struct audsettings as = default_test_settings;
    SWVoiceOut *voice;

    voice = AUD_open_out(be, NULL, name, opaque, cb, &as);
    g_assert_nonnull(voice);
    return voice;
}

static SWVoiceIn *open_test_voice_in(AudioBackend *be, const char *name,
                                     void *opaque, audio_callback_fn cb)
{
    struct audsettings as = default_test_settings;

    return AUD_open_in(be, NULL, name, opaque, cb, &as);
}

/*
 * Generate 440Hz sine wave samples into buffer.
 */
static void generate_sine_samples(int16_t *buffer, int frames,
                                  int64_t start_frame)
{
    for (int i = 0; i < frames; i++) {
        double t = (double)(start_frame + i) / SAMPLE_RATE;
        double sample = sin(2.0 * M_PI * FREQUENCY * t);
        int16_t s = (int16_t)(sample * 32767.0);

        buffer[i * 2] = s;       /* left channel */
        buffer[i * 2 + 1] = s;   /* right channel */
    }
}

static void test_sine_callback(void *opaque, int avail)
{
    TestSineState *s = opaque;
    int16_t buffer[BUFFER_FRAMES * CHANNELS];
    int frames_remaining;
    int frames_to_write;
    size_t bytes_written;

    frames_remaining = s->total_frames - s->frames_written;
    if (frames_remaining <= 0) {
        return;
    }

    frames_to_write = avail / (sizeof(int16_t) * CHANNELS);
    frames_to_write = MIN(frames_to_write, BUFFER_FRAMES);
    frames_to_write = MIN(frames_to_write, frames_remaining);

    generate_sine_samples(buffer, frames_to_write, s->frames_written);

    bytes_written = AUD_write(s->be, s->voice, buffer,
                              frames_to_write * sizeof(int16_t) * CHANNELS);
    s->frames_written += bytes_written / (sizeof(int16_t) * CHANNELS);
}


static void test_audio_out_sine_wave(void)
{
    TestSineState state = {0};
    int64_t start_time;
    int64_t elapsed_ms;

    state.be = get_test_audio_backend();
    state.total_frames = SAMPLE_RATE * DURATION_SECS;
    state.frames_written = 0;

    g_test_message("Opening audio output...");
    state.voice = open_test_voice_out(state.be, "test-sine",
                                      &state, test_sine_callback);

    g_test_message("Playing 440Hz sine wave for %d seconds...", DURATION_SECS);
    AUD_set_active_out(state.be, state.voice, true);

    /*
     * Run the audio subsystem until all frames are written or timeout.
     */
    start_time = g_get_monotonic_time();
    while (state.frames_written < state.total_frames) {
        audio_run(AUDIO_MIXENG_BACKEND(state.be), "test");
        main_loop_wait(true);

        elapsed_ms = (g_get_monotonic_time() - start_time) / 1000;
        if (elapsed_ms > TIMEOUT_SECS * 1000) {
            g_test_message("Timeout waiting for audio to complete");
            break;
        }

        g_usleep(G_USEC_PER_SEC / 100);  /* 10ms */
    }

    g_test_message("Wrote %" PRId64 " frames (%.2f seconds)",
                   state.frames_written,
                   (double)state.frames_written / SAMPLE_RATE);

    g_assert_cmpint(state.frames_written, ==, state.total_frames);

    AUD_set_active_out(state.be, state.voice, false);
    AUD_close_out(state.be, state.voice);
}

static void test_audio_prio_list(void)
{
    g_autofree gchar *backends = NULL;
    GString *str = g_string_new(NULL);
    bool has_none = false;

    for (int i = 0; audio_prio_list[i]; i++) {
        if (i > 0) {
            g_string_append_c(str, ' ');
        }
        g_string_append(str, audio_prio_list[i]);

        if (g_strcmp0(audio_prio_list[i], "none") == 0) {
            has_none = true;
        }
    }

    backends = g_string_free(str, FALSE);
    g_test_message("Available backends: %s", backends);

    /* The 'none' backend should always be available */
    g_assert_true(has_none);
}

static void test_audio_out_active_state(void)
{
    AudioBackend *be;
    SWVoiceOut *voice;

    be = get_test_audio_backend();
    voice = open_test_voice_out(be, "test-active", NULL, dummy_audio_callback);

    g_assert_false(AUD_is_active_out(be, voice));

    AUD_set_active_out(be, voice, true);
    g_assert_true(AUD_is_active_out(be, voice));

    AUD_set_active_out(be, voice, false);
    g_assert_false(AUD_is_active_out(be, voice));

    AUD_close_out(be, voice);
}

static void test_audio_out_buffer_size(void)
{
    AudioBackend *be;
    SWVoiceOut *voice;
    int buffer_size;

    be = get_test_audio_backend();
    voice = open_test_voice_out(be, "test-buffer", NULL, dummy_audio_callback);

    buffer_size = AUD_get_buffer_size_out(be, voice);
    g_test_message("Buffer size: %d bytes", buffer_size);
    g_assert_cmpint(buffer_size, >, 0);

    AUD_close_out(be, voice);

    g_assert_cmpint(AUD_get_buffer_size_out(NULL, NULL), ==, 0);
}

static void test_audio_out_volume(void)
{
    AudioBackend *be;
    SWVoiceOut *voice;
    Volume vol;

    be = get_test_audio_backend();
    voice = open_test_voice_out(be, "test-volume", NULL, dummy_audio_callback);

    vol = (Volume){ .mute = false, .channels = 2, .vol = {255, 255} };
    AUD_set_volume_out(be, voice, &vol);

    vol = (Volume){ .mute = true, .channels = 2, .vol = {255, 255} };
    AUD_set_volume_out(be, voice, &vol);

    vol = (Volume){ .mute = false, .channels = 2, .vol = {128, 128} };
    AUD_set_volume_out(be, voice, &vol);

    AUD_close_out(be, voice);
}

static void test_audio_in_active_state(void)
{
    AudioBackend *be;
    SWVoiceIn *voice;

    be = get_test_audio_backend();
    voice = open_test_voice_in(be, "test-in-active", NULL, dummy_audio_callback);
    if (!voice) {
        g_test_skip("The backend may not support input");
        return;
    }

    g_assert_false(AUD_is_active_in(be, voice));

    AUD_set_active_in(be, voice, true);
    g_assert_true(AUD_is_active_in(be, voice));

    AUD_set_active_in(be, voice, false);
    g_assert_false(AUD_is_active_in(be, voice));

    AUD_close_in(be, voice);
}

static void test_audio_in_volume(void)
{
    AudioBackend *be;
    SWVoiceIn *voice;
    Volume vol;

    be = get_test_audio_backend();
    voice = open_test_voice_in(be, "test-in-volume", NULL, dummy_audio_callback);
    if (!voice) {
        g_test_skip("The backend may not support input");
        return;
    }

    vol = (Volume){ .mute = false, .channels = 2, .vol = {255, 255} };
    AUD_set_volume_in(be, voice, &vol);

    vol = (Volume){ .mute = true, .channels = 2, .vol = {255, 255} };
    AUD_set_volume_in(be, voice, &vol);

    AUD_close_in(be, voice);
}


/* Capture test state */
#define CAPTURE_BUFFER_FRAMES (SAMPLE_RATE / 10)  /* 100ms of audio */
#define CAPTURE_BUFFER_SIZE   (CAPTURE_BUFFER_FRAMES * CHANNELS * sizeof(int16_t))

typedef struct TestCaptureState {
    bool notify_called;
    bool capture_called;
    bool destroy_called;
    audcnotification_e last_notify;
    int16_t *captured_samples;
    size_t captured_bytes;
    size_t capture_buffer_size;
} TestCaptureState;

static void test_capture_notify(void *opaque, audcnotification_e cmd)
{
    TestCaptureState *s = opaque;
    s->notify_called = true;
    s->last_notify = cmd;
}

static void test_capture_capture(void *opaque, const void *buf, int size)
{
    TestCaptureState *s = opaque;
    size_t bytes_to_copy;

    s->capture_called = true;

    if (!s->captured_samples || s->captured_bytes >= s->capture_buffer_size) {
        return;
    }

    bytes_to_copy = MIN(size, s->capture_buffer_size - s->captured_bytes);
    memcpy((uint8_t *)s->captured_samples + s->captured_bytes, buf, bytes_to_copy);
    s->captured_bytes += bytes_to_copy;
}

static void test_capture_destroy(void *opaque)
{
    TestCaptureState *s = opaque;
    s->destroy_called = true;
}

/*
 * Compare captured audio with expected sine wave.
 * Returns the number of matching samples (within tolerance).
 */
static int compare_sine_samples(const int16_t *captured, int frames,
                                int64_t start_frame, int tolerance)
{
    int matching = 0;

    for (int i = 0; i < frames; i++) {
        double t = (double)(start_frame + i) / SAMPLE_RATE;
        double sample = sin(2.0 * M_PI * FREQUENCY * t);
        int16_t expected = (int16_t)(sample * 32767.0);

        /* Check left channel */
        if (abs(captured[i * 2] - expected) <= tolerance) {
            matching++;
        }
        /* Check right channel */
        if (abs(captured[i * 2 + 1] - expected) <= tolerance) {
            matching++;
        }
    }

    return matching;
}

static void test_audio_capture(void)
{
    AudioBackend *be;
    CaptureVoiceOut *cap;
    SWVoiceOut *voice;
    TestCaptureState state = {0};
    TestSineState sine_state = {0};
    struct audsettings as = default_test_settings;
    struct audio_capture_ops ops = {
        .notify = test_capture_notify,
        .capture = test_capture_capture,
        .destroy = test_capture_destroy,
    };
    int64_t start_time;
    int64_t elapsed_ms;
    int captured_frames;
    int matching_samples;
    int total_samples;
    double match_ratio;

    be = get_test_audio_backend();

    state.captured_samples = g_malloc0(CAPTURE_BUFFER_SIZE);
    state.captured_bytes = 0;
    state.capture_buffer_size = CAPTURE_BUFFER_SIZE;

    cap = AUD_add_capture(be, &as, &ops, &state);
    g_assert_nonnull(cap);

    sine_state.be = be;
    sine_state.total_frames = CAPTURE_BUFFER_FRAMES;
    sine_state.frames_written = 0;

    voice = open_test_voice_out(be, "test-capture-sine",
                                &sine_state, test_sine_callback);
    sine_state.voice = voice;

    AUD_set_active_out(be, voice, true);

    start_time = g_get_monotonic_time();
    while (sine_state.frames_written < sine_state.total_frames ||
           state.captured_bytes < CAPTURE_BUFFER_SIZE) {
        audio_run(AUDIO_MIXENG_BACKEND(be), "test-capture");
        main_loop_wait(true);

        elapsed_ms = (g_get_monotonic_time() - start_time) / 1000;
        if (elapsed_ms > 1000) {  /* 1 second timeout */
            break;
        }

        g_usleep(G_USEC_PER_SEC / 1000);  /* 1ms */
    }

    g_test_message("Wrote %" PRId64 " frames, captured %zu bytes",
                   sine_state.frames_written, state.captured_bytes);

    g_assert_true(state.capture_called);
    g_assert_cmpuint(state.captured_bytes, >, 0);

    /* Compare captured data with expected sine wave */
    captured_frames = state.captured_bytes / (CHANNELS * sizeof(int16_t));
    if (captured_frames > 0) {
        /*
         * Allow some tolerance due to mixing/conversion.
         * The tolerance accounts for potential rounding differences.
         */
        matching_samples = compare_sine_samples(state.captured_samples,
                                                captured_frames, 0, 100);
        total_samples = captured_frames * CHANNELS;
        match_ratio = (double)matching_samples / total_samples;

        g_test_message("Captured %d frames, %d/%d samples match (%.1f%%)",
                       captured_frames, matching_samples, total_samples,
                       match_ratio * 100.0);

        /*
         * Expect at least 90% of samples to match within tolerance.
         * Some variation is expected due to mixing engine processing.
         */
        g_assert_cmpfloat(match_ratio, >=, 0.9);
    }

    AUD_set_active_out(be, voice, false);
    AUD_close_out(be, voice);

    AUD_del_capture(be, cap, &state);
    g_assert_true(state.destroy_called);

    g_free(state.captured_samples);
}

static void test_audio_null_handling(void)
{
    AudioBackend *be = get_test_audio_backend();
    uint8_t buffer[64];

    /* AUD_is_active_out/in(NULL) should return false */
    g_assert_false(AUD_is_active_out(be, NULL));
    g_assert_false(AUD_is_active_in(be, NULL));

    /* AUD_get_buffer_size_out(NULL) should return 0 */
    g_assert_cmpint(AUD_get_buffer_size_out(be, NULL), ==, 0);

    /* AUD_write/read(NULL, ...) should return size (no-op) */
    g_assert_cmpuint(AUD_write(be, NULL, buffer, sizeof(buffer)), ==,
                     sizeof(buffer));
    g_assert_cmpuint(AUD_read(be, NULL, buffer, sizeof(buffer)), ==,
                     sizeof(buffer));

    /* These should not crash */
    AUD_set_active_out(be, NULL, true);
    AUD_set_active_out(be, NULL, false);
    AUD_set_active_in(be, NULL, true);
    AUD_set_active_in(be, NULL, false);
}

static void test_audio_multiple_voices(void)
{
    AudioBackend *be;
    SWVoiceOut *out1, *out2;
    SWVoiceIn *in1;

    be = get_test_audio_backend();
    out1 = open_test_voice_out(be, "test-multi-out1", NULL, dummy_audio_callback);
    out2 = open_test_voice_out(be, "test-multi-out2", NULL, dummy_audio_callback);
    in1 = open_test_voice_in(be, "test-multi-in1", NULL, dummy_audio_callback);

    AUD_set_active_out(be, out1, true);
    AUD_set_active_out(be, out2, true);
    AUD_set_active_in(be, in1, true);

    g_assert_true(AUD_is_active_out(be, out1));
    g_assert_true(AUD_is_active_out(be, out2));
    if (in1) {
        g_assert_true(AUD_is_active_in(be, in1));
    }

    AUD_set_active_out(be, out1, false);
    AUD_set_active_out(be, out2, false);
    AUD_set_active_in(be, in1, false);

    AUD_close_in(be, in1);
    AUD_close_out(be, out2);
    AUD_close_out(be, out1);
}

int main(int argc, char **argv)
{
    GOptionContext *context;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    int ret;

    context = g_option_context_new("- QEMU audio test");
    g_option_context_add_main_entries(context, test_options, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Option parsing failed: %s\n", error->message);
        return 1;
    }
    g_option_context_free(context);

    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_TRACE);
    qemu_add_opts(&qemu_trace_opts);
    if (opt_trace) {
        trace_opt_parse(opt_trace);
        qemu_set_log(LOG_TRACE, &error_fatal);
    }
    trace_init_backends();
    trace_init_file();

    dir = g_test_build_filename(G_TEST_BUILT, "..", "..", NULL);
    g_setenv("QEMU_MODULE_DIR", dir, true);
    qemu_init_exec_dir(argv[0]);
    module_call_init(MODULE_INIT_QOM);
    module_init_info(qemu_modinfo);

    qemu_init_main_loop(&error_abort);
    if (opt_audiodev) {
        g_autofree gchar *spec = is_help_option(opt_audiodev) ?
            opt_audiodev : g_strdup_printf("%s,id=%s", opt_audiodev, TEST_AUDIODEV_ID);
        audio_parse_option(spec);
    }
    audio_create_default_audiodevs();
    audio_init_audiodevs();

    g_test_add_func("/audio/prio-list", test_audio_prio_list);

    g_test_add_func("/audio/out/active-state", test_audio_out_active_state);
    g_test_add_func("/audio/out/sine-wave", test_audio_out_sine_wave);
    g_test_add_func("/audio/out/buffer-size", test_audio_out_buffer_size);
    g_test_add_func("/audio/out/volume", test_audio_out_volume);
    g_test_add_func("/audio/out/capture", test_audio_capture);

    g_test_add_func("/audio/in/active-state", test_audio_in_active_state);
    g_test_add_func("/audio/in/volume", test_audio_in_volume);

    g_test_add_func("/audio/null-handling", test_audio_null_handling);
    g_test_add_func("/audio/multiple-voices", test_audio_multiple_voices);

    ret = g_test_run();

    audio_cleanup();

    return ret;
}
