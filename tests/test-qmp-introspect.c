/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * QAPI/QMP schema introspection
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "test-qmp-introspect.h"

const char test_qmp_schema_json[] = "["
    "{\"arg-type\": \"0\", \"meta-type\": \"event\", \"name\": \"EVENT_A\"}, "
    "{\"arg-type\": \"0\", \"meta-type\": \"event\", \"name\": \"EVENT_B\"}, "
    "{\"arg-type\": \"1\", \"meta-type\": \"event\", \"name\": \"EVENT_C\"}, "
    "{\"arg-type\": \"2\", \"meta-type\": \"event\", \"name\": \"EVENT_D\"}, "
    "{\"arg-type\": \"3\", \"meta-type\": \"event\", \"name\": \"__ORG.QEMU_X-EVENT\"}, "
    "{\"arg-type\": \"4\", \"meta-type\": \"command\", \"name\": \"__org.qemu_x-command\", \"ret-type\": \"5\"}, "
    "{\"arg-type\": \"6\", \"meta-type\": \"command\", \"name\": \"guest-get-time\", \"ret-type\": \"int\"}, "
    "{\"arg-type\": \"7\", \"meta-type\": \"command\", \"name\": \"guest-sync\", \"ret-type\": \"any\"}, "
    "{\"arg-type\": \"0\", \"meta-type\": \"command\", \"name\": \"user_def_cmd\", \"ret-type\": \"0\"}, "
    "{\"arg-type\": \"8\", \"meta-type\": \"command\", \"name\": \"user_def_cmd0\", \"ret-type\": \"8\"}, "
    "{\"arg-type\": \"9\", \"meta-type\": \"command\", \"name\": \"user_def_cmd1\", \"ret-type\": \"0\"}, "
    "{\"arg-type\": \"10\", \"meta-type\": \"command\", \"name\": \"user_def_cmd2\", \"ret-type\": \"11\"}, "
    "{\"members\": [], \"meta-type\": \"object\", \"name\": \"0\"}, "
    "{\"members\": [{\"default\": null, \"name\": \"a\", \"type\": \"int\"}, {\"default\": null, \"name\": \"b\", \"type\": \"12\"}, {\"name\": \"c\", \"type\": \"str\"}], \"meta-type\": \"object\", \"name\": \"1\"}, "
    "{\"members\": [{\"name\": \"a\", \"type\": \"13\"}, {\"name\": \"b\", \"type\": \"str\"}, {\"default\": null, \"name\": \"c\", \"type\": \"str\"}, {\"default\": null, \"name\": \"enum3\", \"type\": \"14\"}], \"meta-type\": \"object\", \"name\": \"2\"}, "
    "{\"members\": [{\"name\": \"__org.qemu_x-member1\", \"type\": \"15\"}, {\"name\": \"__org.qemu_x-member2\", \"type\": \"str\"}, {\"default\": null, \"name\": \"wchar-t\", \"type\": \"int\"}], \"meta-type\": \"object\", \"name\": \"3\"}, "
    "{\"members\": [{\"name\": \"a\", \"type\": \"[15]\"}, {\"name\": \"b\", \"type\": \"[3]\"}, {\"name\": \"c\", \"type\": \"16\"}, {\"name\": \"d\", \"type\": \"17\"}], \"meta-type\": \"object\", \"name\": \"4\"}, "
    "{\"members\": [{\"name\": \"type\", \"type\": \"18\"}], \"meta-type\": \"object\", \"name\": \"5\", \"tag\": \"type\", \"variants\": [{\"case\": \"__org.qemu_x-branch\", \"type\": \"19\"}]}, "
    "{\"members\": [{\"name\": \"a\", \"type\": \"int\"}, {\"default\": null, \"name\": \"b\", \"type\": \"int\"}], \"meta-type\": \"object\", \"name\": \"6\"}, "
    "{\"json-type\": \"int\", \"meta-type\": \"builtin\", \"name\": \"int\"}, "
    "{\"members\": [{\"name\": \"arg\", \"type\": \"any\"}], \"meta-type\": \"object\", \"name\": \"7\"}, "
    "{\"json-type\": \"value\", \"meta-type\": \"builtin\", \"name\": \"any\"}, "
    "{\"members\": [], \"meta-type\": \"object\", \"name\": \"8\"}, "
    "{\"members\": [{\"name\": \"ud1a\", \"type\": \"12\"}], \"meta-type\": \"object\", \"name\": \"9\"}, "
    "{\"members\": [{\"name\": \"ud1a\", \"type\": \"12\"}, {\"default\": null, \"name\": \"ud1b\", \"type\": \"12\"}], \"meta-type\": \"object\", \"name\": \"10\"}, "
    "{\"members\": [{\"name\": \"string0\", \"type\": \"str\"}, {\"name\": \"dict1\", \"type\": \"20\"}], \"meta-type\": \"object\", \"name\": \"11\"}, "
    "{\"members\": [{\"name\": \"integer\", \"type\": \"int\"}, {\"name\": \"string\", \"type\": \"str\"}, {\"default\": null, \"name\": \"enum1\", \"type\": \"14\"}], \"meta-type\": \"object\", \"name\": \"12\"}, "
    "{\"json-type\": \"string\", \"meta-type\": \"builtin\", \"name\": \"str\"}, "
    "{\"members\": [{\"name\": \"struct1\", \"type\": \"12\"}, {\"name\": \"string\", \"type\": \"str\"}, {\"default\": null, \"name\": \"enum2\", \"type\": \"14\"}], \"meta-type\": \"object\", \"name\": \"13\"}, "
    "{\"meta-type\": \"enum\", \"name\": \"14\", \"values\": [\"value1\", \"value2\", \"value3\"]}, "
    "{\"meta-type\": \"enum\", \"name\": \"15\", \"values\": [\"__org.qemu_x-value\"]}, "
    "{\"element-type\": \"15\", \"meta-type\": \"array\", \"name\": \"[15]\"}, "
    "{\"element-type\": \"3\", \"meta-type\": \"array\", \"name\": \"[3]\"}, "
    "{\"members\": [{\"name\": \"__org.qemu_x-member1\", \"type\": \"15\"}], \"meta-type\": \"object\", \"name\": \"16\", \"tag\": \"__org.qemu_x-member1\", \"variants\": [{\"case\": \"__org.qemu_x-value\", \"type\": \"21\"}]}, "
    "{\"members\": [{\"type\": \"str\"}, {\"type\": \"22\"}], \"meta-type\": \"alternate\", \"name\": \"17\"}, "
    "{\"meta-type\": \"enum\", \"name\": \"18\", \"values\": [\"__org.qemu_x-branch\"]}, "
    "{\"members\": [{\"name\": \"data\", \"type\": \"str\"}], \"meta-type\": \"object\", \"name\": \"19\"}, "
    "{\"members\": [{\"name\": \"string1\", \"type\": \"str\"}, {\"name\": \"dict2\", \"type\": \"23\"}, {\"default\": null, \"name\": \"dict3\", \"type\": \"23\"}], \"meta-type\": \"object\", \"name\": \"20\"}, "
    "{\"members\": [{\"name\": \"array\", \"type\": \"[5]\"}], \"meta-type\": \"object\", \"name\": \"21\"}, "
    "{\"members\": [{\"name\": \"__org.qemu_x-member1\", \"type\": \"15\"}], \"meta-type\": \"object\", \"name\": \"22\"}, "
    "{\"members\": [{\"name\": \"userdef\", \"type\": \"12\"}, {\"name\": \"string\", \"type\": \"str\"}], \"meta-type\": \"object\", \"name\": \"23\"}, "
    "{\"element-type\": \"5\", \"meta-type\": \"array\", \"name\": \"[5]\"}]";
