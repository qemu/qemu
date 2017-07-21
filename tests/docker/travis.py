#!/usr/bin/env python
#
# Travis YAML config parser
#
# Copyright (c) 2016 Red Hat Inc.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2
# or (at your option) any later version. See the COPYING file in
# the top-level directory.

import sys
import yaml
import itertools

def load_yaml(fname):
    return yaml.load(open(fname, "r").read())

def conf_iter(conf):
    def env_to_list(env):
        return env if isinstance(env, list) else [env]
    for entry in conf["matrix"]["include"]:
        yield {"env": env_to_list(entry["env"]),
               "compiler": entry["compiler"]}
    for entry in itertools.product(conf["compiler"],
                                   conf["env"]["matrix"]):
        yield {"env": env_to_list(entry[1]),
               "compiler": entry[0]}

def main():
    if len(sys.argv) < 2:
        sys.stderr.write("Usage: %s <travis-file>\n" % sys.argv[0])
        return 1
    conf = load_yaml(sys.argv[1])
    print "\n".join((": ${%s}" % var for var in conf["env"]["global"]))
    for config in conf_iter(conf):
        print "("
        print "\n".join(config["env"])
        print "alias cc=" + config["compiler"]
        print "\n".join(conf["before_script"])
        print "\n".join(conf["script"])
        print ")"
    return 0

if __name__ == "__main__":
    sys.exit(main())
