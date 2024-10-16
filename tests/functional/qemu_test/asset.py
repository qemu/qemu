# Test utilities for fetching & caching assets
#
# Copyright 2024 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import hashlib
import logging
import os
import subprocess
import sys
import unittest
import urllib.request
from time import sleep
from pathlib import Path
from shutil import copyfileobj


# Instances of this class must be declared as class level variables
# starting with a name "ASSET_". This enables the pre-caching logic
# to easily find all referenced assets and download them prior to
# execution of the tests.
class Asset:

    def __init__(self, url, hashsum):
        self.url = url
        self.hash = hashsum
        cache_dir_env = os.getenv('QEMU_TEST_CACHE_DIR')
        if cache_dir_env:
            self.cache_dir = Path(cache_dir_env, "download")
        else:
            self.cache_dir = Path(Path("~").expanduser(),
                                  ".cache", "qemu", "download")
        self.cache_file = Path(self.cache_dir, hashsum)
        self.log = logging.getLogger('qemu-test')

    def __repr__(self):
        return "Asset: url=%s hash=%s cache=%s" % (
            self.url, self.hash, self.cache_file)

    def _check(self, cache_file):
        if self.hash is None:
            return True
        if len(self.hash) == 64:
            hl = hashlib.sha256()
        elif len(self.hash) == 128:
            hl = hashlib.sha512()
        else:
            raise Exception("unknown hash type")

        # Calculate the hash of the file:
        with open(cache_file, 'rb') as file:
            while True:
                chunk = file.read(1 << 20)
                if not chunk:
                    break
                hl.update(chunk)

        return self.hash == hl.hexdigest()

    def valid(self):
        return self.cache_file.exists() and self._check(self.cache_file)

    def _wait_for_other_download(self, tmp_cache_file):
        # Another thread already seems to download the asset, so wait until
        # it is done, while also checking the size to see whether it is stuck
        try:
            current_size = tmp_cache_file.stat().st_size
            new_size = current_size
        except:
            if os.path.exists(self.cache_file):
                return True
            raise
        waittime = lastchange = 600
        while waittime > 0:
            sleep(1)
            waittime -= 1
            try:
                new_size = tmp_cache_file.stat().st_size
            except:
                if os.path.exists(self.cache_file):
                    return True
                raise
            if new_size != current_size:
                lastchange = waittime
                current_size = new_size
            elif lastchange - waittime > 90:
                return False

        self.log.debug("Time out while waiting for %s!", tmp_cache_file)
        raise

    def fetch(self):
        if not self.cache_dir.exists():
            self.cache_dir.mkdir(parents=True, exist_ok=True)

        if self.valid():
            self.log.debug("Using cached asset %s for %s",
                           self.cache_file, self.url)
            return str(self.cache_file)

        if os.environ.get("QEMU_TEST_NO_DOWNLOAD", False):
            raise Exception("Asset cache is invalid and downloads disabled")

        self.log.info("Downloading %s to %s...", self.url, self.cache_file)
        tmp_cache_file = self.cache_file.with_suffix(".download")

        for retries in range(3):
            try:
                with tmp_cache_file.open("xb") as dst:
                    with urllib.request.urlopen(self.url) as resp:
                        copyfileobj(resp, dst)
                break
            except FileExistsError:
                self.log.debug("%s already exists, "
                               "waiting for other thread to finish...",
                               tmp_cache_file)
                if self._wait_for_other_download(tmp_cache_file):
                    return str(self.cache_file)
                self.log.debug("%s seems to be stale, "
                               "deleting and retrying download...",
                               tmp_cache_file)
                tmp_cache_file.unlink()
                continue
            except Exception as e:
                self.log.error("Unable to download %s: %s", self.url, e)
                tmp_cache_file.unlink()
                raise

        try:
            # Set these just for informational purposes
            os.setxattr(str(tmp_cache_file), "user.qemu-asset-url",
                        self.url.encode('utf8'))
            os.setxattr(str(tmp_cache_file), "user.qemu-asset-hash",
                        self.hash.encode('utf8'))
        except Exception as e:
            self.log.debug("Unable to set xattr on %s: %s", tmp_cache_file, e)
            pass

        if not self._check(tmp_cache_file):
            tmp_cache_file.unlink()
            raise Exception("Hash of %s does not match %s" %
                            (self.url, self.hash))
        tmp_cache_file.replace(self.cache_file)

        self.log.info("Cached %s at %s" % (self.url, self.cache_file))
        return str(self.cache_file)

    def precache_test(test):
        log = logging.getLogger('qemu-test')
        log.setLevel(logging.DEBUG)
        handler = logging.StreamHandler(sys.stdout)
        handler.setLevel(logging.DEBUG)
        formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
        handler.setFormatter(formatter)
        log.addHandler(handler)
        for name, asset in vars(test.__class__).items():
            if name.startswith("ASSET_") and type(asset) == Asset:
                log.info("Attempting to cache '%s'" % asset)
                asset.fetch()
        log.removeHandler(handler)

    def precache_suite(suite):
        for test in suite:
            if isinstance(test, unittest.TestSuite):
                Asset.precache_suite(test)
            elif isinstance(test, unittest.TestCase):
                Asset.precache_test(test)

    def precache_suites(path, cacheTstamp):
        loader = unittest.loader.defaultTestLoader
        tests = loader.loadTestsFromNames([path], None)

        with open(cacheTstamp, "w") as fh:
            Asset.precache_suite(tests)
