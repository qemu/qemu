HXCOMM See docs/devel/docs.rst for the format of this file.
HXCOMM
HXCOMM Keep the list of subcommands sorted by name.
HXCOMM Use DEFHEADING() to define headings in both help text and rST
HXCOMM Text between SRST and ERST are copied to rST version and
HXCOMM discarded from C version
HXCOMM DEF(command, callback, arg_string) is used to construct
HXCOMM command structures and help message.
HXCOMM HXCOMM can be used for comments, discarded from both rST and C

HXCOMM When amending the rST sections, please remember to copy the usage
HXCOMM over to the per-command sections in docs/tools/qemu-img.rst.

DEF("amend", img_amend,
    "amend [--object objectdef] [--image-opts] [-p] [-q] [-f fmt] [-t cache] [--force] -o options filename")
SRST
.. option:: amend [--object OBJECTDEF] [--image-opts] [-p] [-q] [-f FMT] [-t CACHE] [--force] -o OPTIONS FILENAME
ERST

DEF("bench", img_bench,
    "bench [-c count] [-d depth] [-f fmt] [--flush-interval=flush_interval] [-i aio] [-n] [--no-drain] [-o offset] [--pattern=pattern] [-q] [-s buffer_size] [-S step_size] [-t cache] [-w] [-U] filename")
SRST
.. option:: bench [-c COUNT] [-d DEPTH] [-f FMT] [--flush-interval=FLUSH_INTERVAL] [-i AIO] [-n] [--no-drain] [-o OFFSET] [--pattern=PATTERN] [-q] [-s BUFFER_SIZE] [-S STEP_SIZE] [-t CACHE] [-w] [-U] FILENAME
ERST

DEF("bitmap", img_bitmap,
    "bitmap (--merge SOURCE | --add | --remove | --clear | --enable | --disable)... [-b source_file [-F source_fmt]] [-g granularity] [--object objectdef] [--image-opts | -f fmt] filename bitmap")
SRST
.. option:: bitmap (--merge SOURCE | --add | --remove | --clear | --enable | --disable)... [-b SOURCE_FILE [-F SOURCE_FMT]] [-g GRANULARITY] [--object OBJECTDEF] [--image-opts | -f FMT] FILENAME BITMAP
ERST

DEF("check", img_check,
    "check [--object objectdef] [--image-opts] [-q] [-f fmt] [--output=ofmt] [-r [leaks | all]] [-T src_cache] [-U] filename")
SRST
.. option:: check [--object OBJECTDEF] [--image-opts] [-q] [-f FMT] [--output=OFMT] [-r [leaks | all]] [-T SRC_CACHE] [-U] FILENAME
ERST

DEF("commit", img_commit,
    "commit [--object objectdef] [--image-opts] [-q] [-f fmt] [-t cache] [-b base] [-r rate_limit] [-d] [-p] filename")
SRST
.. option:: commit [--object OBJECTDEF] [--image-opts] [-q] [-f FMT] [-t CACHE] [-b BASE] [-r RATE_LIMIT] [-d] [-p] FILENAME
ERST

DEF("compare", img_compare,
    "compare [--object objectdef] [--image-opts] [-f fmt] [-F fmt] [-T src_cache] [-p] [-q] [-s] [-U] filename1 filename2")
SRST
.. option:: compare [--object OBJECTDEF] [--image-opts] [-f FMT] [-F FMT] [-T SRC_CACHE] [-p] [-q] [-s] [-U] FILENAME1 FILENAME2
ERST

DEF("convert", img_convert,
    "convert [--object objectdef] [--image-opts] [--target-image-opts] [--target-is-zero] [--bitmaps] [-U] [-C] [-c] [-p] [-q] [-n] [-f fmt] [-t cache] [-T src_cache] [-O output_fmt] [-B backing_file [-F backing_fmt]] [-o options] [-l snapshot_param] [-S sparse_size] [-r rate_limit] [-m num_coroutines] [-W] [--salvage] filename [filename2 [...]] output_filename")
SRST
.. option:: convert [--object OBJECTDEF] [--image-opts] [--target-image-opts] [--target-is-zero] [--bitmaps] [-U] [-C] [-c] [-p] [-q] [-n] [-f FMT] [-t CACHE] [-T SRC_CACHE] [-O OUTPUT_FMT] [-B BACKING_FILE [-F BACKING_FMT]] [-o OPTIONS] [-l SNAPSHOT_PARAM] [-S SPARSE_SIZE] [-r RATE_LIMIT] [-m NUM_COROUTINES] [-W] [--salvage] FILENAME [FILENAME2 [...]] OUTPUT_FILENAME
ERST

DEF("create", img_create,
    "create [--object objectdef] [-q] [-f fmt] [-b backing_file [-F backing_fmt]] [-u] [-o options] filename [size]")
SRST
.. option:: create [--object OBJECTDEF] [-q] [-f FMT] [-b BACKING_FILE [-F BACKING_FMT]] [-u] [-o OPTIONS] FILENAME [SIZE]
ERST

DEF("dd", img_dd,
    "dd [--image-opts] [-U] [-f fmt] [-O output_fmt] [bs=block_size] [count=blocks] [skip=blocks] if=input of=output")
SRST
.. option:: dd [--image-opts] [-U] [-f FMT] [-O OUTPUT_FMT] [bs=BLOCK_SIZE] [count=BLOCKS] [skip=BLOCKS] if=INPUT of=OUTPUT
ERST

DEF("info", img_info,
    "info [--object objectdef] [--image-opts] [-f fmt] [--output=ofmt] [--backing-chain] [-U] filename")
SRST
.. option:: info [--object OBJECTDEF] [--image-opts] [-f FMT] [--output=OFMT] [--backing-chain] [-U] FILENAME
ERST

DEF("map", img_map,
    "map [--object objectdef] [--image-opts] [-f fmt] [--start-offset=offset] [--max-length=len] [--output=ofmt] [-U] filename")
SRST
.. option:: map [--object OBJECTDEF] [--image-opts] [-f FMT] [--start-offset=OFFSET] [--max-length=LEN] [--output=OFMT] [-U] FILENAME
ERST

DEF("measure", img_measure,
"measure [--output=ofmt] [-O output_fmt] [-o options] [--size N | [--object objectdef] [--image-opts] [-f fmt] [-l snapshot_param] filename]")
SRST
.. option:: measure [--output=OFMT] [-O OUTPUT_FMT] [-o OPTIONS] [--size N | [--object OBJECTDEF] [--image-opts] [-f FMT] [-l SNAPSHOT_PARAM] FILENAME]
ERST

DEF("snapshot", img_snapshot,
    "snapshot [--object objectdef] [-f fmt | --image-opts] [-U] [-q] [-l | -a snapshot | -c snapshot | -d snapshot] filename")
SRST
.. option:: snapshot [--object OBJECTDEF] [-f FMT | --image-opts] [-U] [-q] [-l | -a SNAPSHOT | -c SNAPSHOT | -d SNAPSHOT] FILENAME
ERST

DEF("rebase", img_rebase,
    "rebase [--object objectdef] [--image-opts] [-U] [-q] [-f fmt] [-t cache] [-T src_cache] [-p] [-u] [-c] -b backing_file [-F backing_fmt] filename")
SRST
.. option:: rebase [--object OBJECTDEF] [--image-opts] [-U] [-q] [-f FMT] [-t CACHE] [-T SRC_CACHE] [-p] [-u] [-c] -b BACKING_FILE [-F BACKING_FMT] FILENAME
ERST

DEF("resize", img_resize,
    "resize [--object objectdef] [--image-opts] [-f fmt] [--preallocation=prealloc] [-q] [--shrink] filename [+ | -]size")
SRST
.. option:: resize [--object OBJECTDEF] [--image-opts] [-f FMT] [--preallocation=PREALLOC] [-q] [--shrink] FILENAME [+ | -]SIZE
ERST
