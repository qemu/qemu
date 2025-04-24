#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "qobject/qbool.h"
#include "libqtest-single.h"

static char *get_cpu0_qom_path(void)
{
    QDict *resp;
    QList *ret;
    QDict *cpu0;
    char *path;

    resp = qmp("{'execute': 'query-cpus-fast', 'arguments': {}}");
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_qlist(resp, "return");

    cpu0 = qobject_to(QDict, qlist_peek(ret));
    path = g_strdup(qdict_get_str(cpu0, "qom-path"));
    qobject_unref(resp);
    return path;
}

static QObject *qom_get(const char *path, const char *prop)
{
    QDict *resp = qmp("{ 'execute': 'qom-get',"
                      "  'arguments': { 'path': %s,"
                      "                 'property': %s } }",
                      path, prop);
    QObject *ret = qdict_get(resp, "return");
    qobject_ref(ret);
    qobject_unref(resp);
    return ret;
}

static bool qom_get_bool(const char *path, const char *prop)
{
    QBool *value = qobject_to(QBool, qom_get(path, prop));
    bool b = qbool_get_bool(value);

    qobject_unref(value);
    return b;
}

typedef struct CpuidTestArgs {
    const char *cmdline;
    const char *property;
    int64_t expected_value;
} CpuidTestArgs;

static void test_cpuid_prop(const void *data)
{
    const CpuidTestArgs *args = data;
    char *path;
    QNum *value;
    int64_t val;

    qtest_start(args->cmdline);
    path = get_cpu0_qom_path();
    value = qobject_to(QNum, qom_get(path, args->property));
    g_assert(qnum_get_try_int(value, &val));
    g_assert_cmpint(val, ==, args->expected_value);
    qtest_end();

    qobject_unref(value);
    g_free(path);
}

static void add_cpuid_test(const char *name, const char *cpu,
                           const char *cpufeat, const char *machine,
                           const char *property, int64_t expected_value)
{
    CpuidTestArgs *args = g_new0(CpuidTestArgs, 1);
    char *cmdline;
    char *save;

    if (!qtest_has_cpu_model(cpu)) {
        return;
    }
    cmdline = g_strdup_printf("-cpu %s", cpu);

    if (cpufeat) {
        save = cmdline;
        cmdline = g_strdup_printf("%s,%s", cmdline, cpufeat);
        g_free(save);
    }
    if (machine) {
        save = cmdline;
        cmdline = g_strdup_printf("-machine %s %s", machine, cmdline);
        g_free(save);
    }
    args->cmdline = cmdline;
    args->property = property;
    args->expected_value = expected_value;
    qtest_add_data_func(name, args, test_cpuid_prop);
}


/* Parameters to a add_feature_test() test case */
typedef struct FeatureTestArgs {
    /* cmdline to start QEMU */
    const char *cmdline;
    /*
     * cpuid-input-eax and cpuid-input-ecx values to look for,
     * in "feature-words" and "filtered-features" properties.
     */
    uint32_t in_eax, in_ecx;
    /* The register name to look for, in the X86CPUFeatureWordInfo array */
    const char *reg;
    /* The bit to check in X86CPUFeatureWordInfo.features */
    int bitnr;
    /* The expected value for the bit in (X86CPUFeatureWordInfo.features) */
    bool expected_value;
} FeatureTestArgs;

/* Get the value for a feature word in a X86CPUFeatureWordInfo list */
static uint32_t get_feature_word(QList *features, uint32_t eax, uint32_t ecx,
                                 const char *reg)
{
    const QListEntry *e;

    for (e = qlist_first(features); e; e = qlist_next(e)) {
        QDict *w = qobject_to(QDict, qlist_entry_obj(e));
        const char *rreg = qdict_get_str(w, "cpuid-register");
        uint32_t reax = qdict_get_int(w, "cpuid-input-eax");
        bool has_ecx = qdict_haskey(w, "cpuid-input-ecx");
        uint32_t recx = 0;
        int64_t val;

        if (has_ecx) {
            recx = qdict_get_int(w, "cpuid-input-ecx");
        }
        if (eax == reax && (!has_ecx || ecx == recx) && !strcmp(rreg, reg)) {
            g_assert(qnum_get_try_int(qobject_to(QNum,
                                                 qdict_get(w, "features")),
                                      &val));
            return val;
        }
    }
    return 0;
}

static void test_feature_flag(const void *data)
{
    const FeatureTestArgs *args = data;
    char *path;
    QList *present, *filtered;
    uint32_t value;

    qtest_start(args->cmdline);
    path = get_cpu0_qom_path();
    present = qobject_to(QList, qom_get(path, "feature-words"));
    filtered = qobject_to(QList, qom_get(path, "filtered-features"));
    value = get_feature_word(present, args->in_eax, args->in_ecx, args->reg);
    value |= get_feature_word(filtered, args->in_eax, args->in_ecx, args->reg);
    qtest_end();

    g_assert(!!(value & (1U << args->bitnr)) == args->expected_value);

    qobject_unref(present);
    qobject_unref(filtered);
    g_free(path);
}

/*
 * Add test case to ensure that a given feature flag is set in
 * either "feature-words" or "filtered-features", when running QEMU
 * using cmdline
 */
static void add_feature_test(const char *name, const char *cpu,
                             const char *cpufeat, uint32_t eax,
                             uint32_t ecx, const char *reg,
                             int bitnr, bool expected_value)
{
    FeatureTestArgs *args = g_new0(FeatureTestArgs, 1);
    char *cmdline;

    if (!qtest_has_cpu_model(cpu)) {
        return;
    }

    if (cpufeat) {
        cmdline = g_strdup_printf("-cpu %s,%s", cpu, cpufeat);
    } else {
        cmdline = g_strdup_printf("-cpu %s", cpu);
    }

    args->cmdline = cmdline;
    args->in_eax = eax;
    args->in_ecx = ecx;
    args->reg = reg;
    args->bitnr = bitnr;
    args->expected_value = expected_value;
    qtest_add_data_func(name, args, test_feature_flag);
}

static void test_plus_minus_subprocess(void)
{
    char *path;

    if (!qtest_has_cpu_model("pentium")) {
        return;
    }

    /* Rules:
     * 1)"-foo" overrides "+foo"
     * 2) "[+-]foo" overrides "foo=..."
     * 3) Old feature names with underscores (e.g. "sse4_2")
     *    should keep working
     *
     * Note: rules 1 and 2 are planned to be removed soon, and
     * should generate a warning.
     */
    qtest_start("-cpu pentium,-fpu,+fpu,-mce,mce=on,+cx8,cx8=off,+sse4_1,sse4_2=on");
    path = get_cpu0_qom_path();

    g_assert_false(qom_get_bool(path, "fpu"));
    g_assert_false(qom_get_bool(path, "mce"));
    g_assert_true(qom_get_bool(path, "cx8"));

    /* Test both the original and the alias feature names: */
    g_assert_true(qom_get_bool(path, "sse4-1"));
    g_assert_true(qom_get_bool(path, "sse4.1"));

    g_assert_true(qom_get_bool(path, "sse4-2"));
    g_assert_true(qom_get_bool(path, "sse4.2"));

    qtest_end();
    g_free(path);
}

static void test_plus_minus(void)
{
    if (!qtest_has_cpu_model("pentium")) {
        return;
    }

    g_test_trap_subprocess("/x86/cpuid/parsing-plus-minus/subprocess", 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("*Ambiguous CPU model string. "
                              "Don't mix both \"-mce\" and \"mce=on\"*");
    g_test_trap_assert_stderr("*Ambiguous CPU model string. "
                              "Don't mix both \"+cx8\" and \"cx8=off\"*");
    g_test_trap_assert_stdout("");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/x86/cpuid/parsing-plus-minus/subprocess",
                    test_plus_minus_subprocess);
    g_test_add_func("/x86/cpuid/parsing-plus-minus", test_plus_minus);

    /* Original level values for CPU models: */
    add_cpuid_test("x86/cpuid/phenom/level",
                   "phenom", NULL, NULL, "level", 5);
    add_cpuid_test("x86/cpuid/Conroe/level",
                   "Conroe", NULL, NULL, "level", 10);
    add_cpuid_test("x86/cpuid/SandyBridge/level",
                   "SandyBridge", NULL, NULL, "level", 0xd);
    add_cpuid_test("x86/cpuid/486/xlevel",
                   "486", NULL, NULL, "xlevel", 0);
    add_cpuid_test("x86/cpuid/core2duo/xlevel",
                   "core2duo", NULL, NULL, "xlevel", 0x80000008);
    add_cpuid_test("x86/cpuid/phenom/xlevel",
                   "phenom", NULL, NULL, "xlevel", 0x8000001A);
    add_cpuid_test("x86/cpuid/athlon/xlevel",
                   "athlon", NULL, NULL, "xlevel", 0x80000008);

    /* If level is not large enough, it should increase automatically: */
    /* CPUID[6].EAX: */
    add_cpuid_test("x86/cpuid/auto-level/486/arat",
                   "486", "arat=on", NULL, "level", 6);
    /* CPUID[EAX=7,ECX=0].EBX: */
    add_cpuid_test("x86/cpuid/auto-level/phenom/fsgsbase",
                   "phenom", "fsgsbase=on", NULL, "level", 7);
    /* CPUID[EAX=7,ECX=0].ECX: */
    add_cpuid_test("x86/cpuid/auto-level/phenom/avx512vbmi",
                   "phenom", "avx512vbmi=on", NULL, "level", 7);
    /* CPUID[EAX=0xd,ECX=1].EAX: */
    add_cpuid_test("x86/cpuid/auto-level/phenom/xsaveopt",
                   "phenom", "xsaveopt=on", NULL, "level", 0xd);
    /* CPUID[8000_0001].EDX: */
    add_cpuid_test("x86/cpuid/auto-xlevel/486/3dnow",
                   "486", "3dnow=on", NULL, "xlevel", 0x80000001);
    /* CPUID[8000_0001].ECX: */
    add_cpuid_test("x86/cpuid/auto-xlevel/486/sse4a",
                   "486", "sse4a=on", NULL, "xlevel", 0x80000001);
    /* CPUID[8000_0007].EDX: */
    add_cpuid_test("x86/cpuid/auto-xlevel/486/invtsc",
                   "486", "invtsc=on", NULL, "xlevel", 0x80000007);
    /* CPUID[8000_000A].EDX: */
    add_cpuid_test("x86/cpuid/auto-xlevel/486/npt",
                   "486", "svm=on,npt=on", NULL, "xlevel", 0x8000000A);
    /* CPUID[C000_0001].EDX: */
    add_cpuid_test("x86/cpuid/auto-xlevel2/phenom/xstore",
                   "phenom", "xstore=on", NULL, "xlevel2", 0xC0000001);
    /* SVM needs CPUID[0x8000000A] */
    add_cpuid_test("x86/cpuid/auto-xlevel/athlon/svm",
                   "athlon", "svm=on", NULL, "xlevel", 0x8000000A);


    /* If level is already large enough, it shouldn't change: */
    add_cpuid_test("x86/cpuid/auto-level/SandyBridge/multiple",
                   "SandyBridge", "arat=on,fsgsbase=on,avx512vbmi=on",
                   NULL, "level", 0xd);
    /* If level is explicitly set, it shouldn't change: */
    add_cpuid_test("x86/cpuid/auto-level/486/fixed/0xF",
                   "486",
                   "level=0xF,arat=on,fsgsbase=on,avx512vbmi=on,xsaveopt=on",
                   NULL, "level", 0xF);
    add_cpuid_test("x86/cpuid/auto-level/486/fixed/2",
                   "486",
                   "level=2,arat=on,fsgsbase=on,avx512vbmi=on,xsaveopt=on",
                   NULL, "level", 2);
    add_cpuid_test("x86/cpuid/auto-level/486/fixed/0",
                   "486",
                   "level=0,arat=on,fsgsbase=on,avx512vbmi=on,xsaveopt=on",
                   NULL, "level", 0);

    /* if xlevel is already large enough, it shouldn't change: */
    add_cpuid_test("x86/cpuid/auto-xlevel/phenom/3dnow",
                   "phenom", "3dnow=on,sse4a=on,invtsc=on,npt=on,svm=on",
                   NULL, "xlevel", 0x8000001A);
    /* If xlevel is explicitly set, it shouldn't change: */
    add_cpuid_test("x86/cpuid/auto-xlevel/486/fixed/80000002",
                   "486",
                   "xlevel=0x80000002,3dnow=on,sse4a=on,invtsc=on,npt=on,svm=on",
                   NULL, "xlevel", 0x80000002);
    add_cpuid_test("x86/cpuid/auto-xlevel/486/fixed/8000001A",
                   "486",
                   "xlevel=0x8000001A,3dnow=on,sse4a=on,invtsc=on,npt=on,svm=on",
                   NULL, "xlevel", 0x8000001A);
    add_cpuid_test("x86/cpuid/auto-xlevel/phenom/fixed/0",
                   "486",
                   "xlevel=0,3dnow=on,sse4a=on,invtsc=on,npt=on,svm=on",
                   NULL, "xlevel", 0);

    /* if xlevel2 is already large enough, it shouldn't change: */
    add_cpuid_test("x86/cpuid/auto-xlevel2/486/fixed",
                   "486", "xlevel2=0xC0000002,xstore=on",
                   NULL, "xlevel2", 0xC0000002);

    /* Check compatibility of old machine-types that didn't
     * auto-increase level/xlevel/xlevel2: */
    if (qtest_has_machine("pc-i440fx-2.7")) {
        add_cpuid_test("x86/cpuid/auto-level/pc-2.7",
                       "486", "arat=on,avx512vbmi=on,xsaveopt=on",
                       "pc-i440fx-2.7", "level", 1);
        add_cpuid_test("x86/cpuid/auto-xlevel/pc-2.7",
                       "486", "3dnow=on,sse4a=on,invtsc=on,npt=on,svm=on",
                       "pc-i440fx-2.7", "xlevel", 0);
        add_cpuid_test("x86/cpuid/auto-xlevel2/pc-2.7",
                       "486", "xstore=on", "pc-i440fx-2.7",
                       "xlevel2", 0);
    }
    if (qtest_has_machine("pc-i440fx-2.9")) {
        add_cpuid_test("x86/cpuid/auto-level7/pc-i440fx-2.9/off",
                       "Conroe", NULL, "pc-i440fx-2.9",
                       "level", 10);
        add_cpuid_test("x86/cpuid/auto-level7/pc-i440fx-2.9/on",
                       "Conroe", "erms=on", "pc-i440fx-2.9",
                       "level", 10);
    }

    /*
     * xlevel doesn't have any feature that triggers auto-level
     * code on old machine-types.  Just check that the compat code
     * is working correctly:
     */
    if (qtest_has_machine("pc-i440fx-2.4")) {
        add_cpuid_test("x86/cpuid/xlevel-compat/pc-i440fx-2.4/npt-off",
                       "SandyBridge", NULL, "pc-i440fx-2.4",
                       "xlevel", 0x80000008);
        add_cpuid_test("x86/cpuid/xlevel-compat/pc-i440fx-2.4/npt-on",
                       "SandyBridge", "svm=on,npt=on", "pc-i440fx-2.4",
                       "xlevel", 0x80000008);
    }

    /* Test feature parsing */
    add_feature_test("x86/cpuid/features/plus",
                     "486", "+arat",
                     6, 0, "EAX", 2, true);
    add_feature_test("x86/cpuid/features/minus",
                     "pentium", "-mmx",
                     1, 0, "EDX", 23, false);
    add_feature_test("x86/cpuid/features/on",
                     "486", "arat=on",
                     6, 0, "EAX", 2, true);
    add_feature_test("x86/cpuid/features/off",
                     "pentium", "mmx=off",
                     1, 0, "EDX", 23, false);

    add_feature_test("x86/cpuid/features/max-plus-invtsc",
                     "max" , "+invtsc",
                     0x80000007, 0, "EDX", 8, true);
    add_feature_test("x86/cpuid/features/max-invtsc-on",
                     "max", "invtsc=on",
                     0x80000007, 0, "EDX", 8, true);
    add_feature_test("x86/cpuid/features/max-minus-mmx",
                     "max", "-mmx",
                     1, 0, "EDX", 23, false);
    add_feature_test("x86/cpuid/features/max-invtsc-on,mmx=off",
                     "max", "mmx=off",
                     1, 0, "EDX", 23, false);

    return g_test_run();
}
