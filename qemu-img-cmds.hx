HXCOMM Use DEFHEADING() to define headings in both help text and texi
HXCOMM Text between STEXI and ETEXI are copied to texi version and
HXCOMM discarded from C version
HXCOMM DEF(command, callback, arg_string) is used to construct
HXCOMM command structures and help message.
HXCOMM HXCOMM can be used for comments, discarded from both texi and C

STEXI
@table @option
ETEXI

DEF("check", img_check,
    "check [-q] [--object objectdef] [-f fmt] [--output=ofmt] [-r [leaks | all]] [-T src_cache] filename")
STEXI
@item check [--object @var{objectdef}] [-q] [-f @var{fmt}] [--output=@var{ofmt}] [-r [leaks | all]] [-T @var{src_cache}] @var{filename}
ETEXI

DEF("create", img_create,
    "create [-q] [--object objectdef] [-f fmt] [-o options] filename [size]")
STEXI
@item create [--object @var{objectdef}] [-q] [-f @var{fmt}] [-o @var{options}] @var{filename} [@var{size}]
ETEXI

DEF("commit", img_commit,
    "commit [-q] [--object objectdef] [-f fmt] [-t cache] [-b base] [-d] [-p] filename")
STEXI
@item commit [--object @var{objectdef}] [-q] [-f @var{fmt}] [-t @var{cache}] [-b @var{base}] [-d] [-p] @var{filename}
ETEXI

DEF("compare", img_compare,
    "compare [--object objectdef] [-f fmt] [-F fmt] [-T src_cache] [-p] [-q] [-s] filename1 filename2")
STEXI
@item compare [--object @var{objectdef}] [-f @var{fmt}] [-F @var{fmt}] [-T @var{src_cache}] [-p] [-q] [-s] @var{filename1} @var{filename2}
ETEXI

DEF("convert", img_convert,
    "convert [--object objectdef] [-c] [-p] [-q] [-n] [-f fmt] [-t cache] [-T src_cache] [-O output_fmt] [-o options] [-s snapshot_id_or_name] [-l snapshot_param] [-S sparse_size] filename [filename2 [...]] output_filename")
STEXI
@item convert [--object @var{objectdef}] [-c] [-p] [-q] [-n] [-f @var{fmt}] [-t @var{cache}] [-T @var{src_cache}] [-O @var{output_fmt}] [-o @var{options}] [-s @var{snapshot_id_or_name}] [-l @var{snapshot_param}] [-S @var{sparse_size}] @var{filename} [@var{filename2} [...]] @var{output_filename}
ETEXI

DEF("info", img_info,
    "info [--object objectdef] [-f fmt] [--output=ofmt] [--backing-chain] filename")
STEXI
@item info [--object @var{objectdef}] [-f @var{fmt}] [--output=@var{ofmt}] [--backing-chain] @var{filename}
ETEXI

DEF("map", img_map,
    "map [--object objectdef] [-f fmt] [--output=ofmt] filename")
STEXI
@item map [--object @var{objectdef}] [-f @var{fmt}] [--output=@var{ofmt}] @var{filename}
ETEXI

DEF("snapshot", img_snapshot,
    "snapshot [--object objectdef] [-q] [-l | -a snapshot | -c snapshot | -d snapshot] filename")
STEXI
@item snapshot [--object @var{objectdef}] [-q] [-l | -a @var{snapshot} | -c @var{snapshot} | -d @var{snapshot}] @var{filename}
ETEXI

DEF("rebase", img_rebase,
    "rebase [--object objectdef] [-q] [-f fmt] [-t cache] [-T src_cache] [-p] [-u] -b backing_file [-F backing_fmt] filename")
STEXI
@item rebase [--object @var{objectdef}] [-q] [-f @var{fmt}] [-t @var{cache}] [-T @var{src_cache}] [-p] [-u] -b @var{backing_file} [-F @var{backing_fmt}] @var{filename}
ETEXI

DEF("resize", img_resize,
    "resize [--object objectdef] [-q] filename [+ | -]size")
STEXI
@item resize [--object @var{objectdef}] [-q] @var{filename} [+ | -]@var{size}
ETEXI

DEF("amend", img_amend,
    "amend [--object objectdef] [-p] [-q] [-f fmt] [-t cache] -o options filename")
STEXI
@item amend [--object @var{objectdef}] [-p] [-q] [-f @var{fmt}] [-t @var{cache}] -o @var{options} @var{filename}
@end table
ETEXI
