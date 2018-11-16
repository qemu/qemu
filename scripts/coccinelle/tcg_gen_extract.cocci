// optimize TCG using extract op
//
// Copyright: (C) 2017 Philippe Mathieu-Daudé. GPLv2+.
// Confidence: High
// Options: --macro-file scripts/cocci-macro-file.h
//
// Nikunj A Dadhania optimization:
// http://lists.nongnu.org/archive/html/qemu-devel/2017-02/msg05211.html
// Aurelien Jarno optimization:
// http://lists.nongnu.org/archive/html/qemu-devel/2017-05/msg01466.html
//
// This script can be run either using spatch locally or via a docker image:
//
// $ spatch \
//     --macro-file scripts/cocci-macro-file.h \
//     --sp-file scripts/coccinelle/tcg_gen_extract.cocci \
//     --keep-comments --in-place \
//     --use-gitgrep --dir target
//
// $ docker run --rm -v $PWD:$PWD -w $PWD philmd/coccinelle \
//     --macro-file scripts/cocci-macro-file.h \
//     --sp-file scripts/coccinelle/tcg_gen_extract.cocci \
//     --keep-comments --in-place \
//     --use-gitgrep --dir target

@initialize:python@
@@
import sys
fd = sys.stderr
def debug(msg="", trailer="\n"):
    fd.write("[DBG] " + msg + trailer)
def low_bits_count(value):
    bits_count = 0
    while (value & (1 << bits_count)):
        bits_count += 1
    return bits_count
def Mn(order): # Mersenne number
    return (1 << order) - 1

@match@
identifier ret;
metavariable arg;
constant ofs, msk;
position shr_p, and_p;
@@
(
    tcg_gen_shri_i32@shr_p
|
    tcg_gen_shri_i64@shr_p
|
    tcg_gen_shri_tl@shr_p
)(ret, arg, ofs);
...  WHEN != ret
(
    tcg_gen_andi_i32@and_p
|
    tcg_gen_andi_i64@and_p
|
    tcg_gen_andi_tl@and_p
)(ret, ret, msk);

@script:python verify_len depends on match@
ret_s << match.ret;
msk_s << match.msk;
shr_p << match.shr_p;
extract_len;
@@
is_optimizable = False
debug("candidate at %s:%s" % (shr_p[0].file, shr_p[0].line))
try: # only eval integer, no #define like 'SR_M' (cpp did this, else some headers are missing).
    msk_v = long(msk_s.strip("UL"), 0)
    msk_b = low_bits_count(msk_v)
    if msk_b == 0:
        debug("  value: 0x%x low_bits: %d" % (msk_v, msk_b))
    else:
        debug("  value: 0x%x low_bits: %d [Mersenne number: 0x%x]" % (msk_v, msk_b, Mn(msk_b)))
        is_optimizable = Mn(msk_b) == msk_v # check low_bits
        coccinelle.extract_len = "%d" % msk_b
    debug("  candidate %s optimizable" % ("IS" if is_optimizable else "is NOT"))
except:
    debug("  ERROR (check included headers?)")
cocci.include_match(is_optimizable)
debug()

@replacement depends on verify_len@
identifier match.ret;
metavariable match.arg;
constant match.ofs, match.msk;
position match.shr_p, match.and_p;
identifier verify_len.extract_len;
@@
(
-tcg_gen_shri_i32@shr_p(ret, arg, ofs);
+tcg_gen_extract_i32(ret, arg, ofs, extract_len);
...  WHEN != ret
-tcg_gen_andi_i32@and_p(ret, ret, msk);
|
-tcg_gen_shri_i64@shr_p(ret, arg, ofs);
+tcg_gen_extract_i64(ret, arg, ofs, extract_len);
...  WHEN != ret
-tcg_gen_andi_i64@and_p(ret, ret, msk);
|
-tcg_gen_shri_tl@shr_p(ret, arg, ofs);
+tcg_gen_extract_tl(ret, arg, ofs, extract_len);
...  WHEN != ret
-tcg_gen_andi_tl@and_p(ret, ret, msk);
)
