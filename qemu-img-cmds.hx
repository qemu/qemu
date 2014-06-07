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
    "check [-q] [-f fmt] [--output=ofmt]  [-r [leaks | all]] filename")
STEXI
@item check [-q] [-f @var{fmt}] [--output=@var{ofmt}] [-r [leaks | all]] @var{filename}
ETEXI

DEF("create", img_create,
    "create [-q] [-f fmt] [-o options] filename [size]")
STEXI
@item create [-q] [-f @var{fmt}] [-o @var{options}] @var{filename} [@var{size}]
ETEXI

DEF("commit", img_commit,
    "commit [-q] [-f fmt] [-t cache] filename")
STEXI
@item commit [-q] [-f @var{fmt}] [-t @var{cache}] @var{filename}
ETEXI

DEF("compare", img_compare,
    "compare [-f fmt] [-F fmt] [-p] [-q] [-s] filename1 filename2")
STEXI
@item compare [-f @var{fmt}] [-F @var{fmt}] [-p] [-q] [-s] @var{filename1} @var{filename2}
ETEXI

DEF("convert", img_convert,
    "convert [-c] [-p] [-q] [-n] [-f fmt] [-t cache] [-O output_fmt] [-o options] [-s snapshot_id_or_name] [-l snapshot_param] [-S sparse_size] filename [filename2 [...]] output_filename")
STEXI
@item convert [-c] [-p] [-q] [-n] [-f @var{fmt}] [-t @var{cache}] [-O @var{output_fmt}] [-o @var{options}] [-s @var{snapshot_id_or_name}] [-l @var{snapshot_param}] [-S @var{sparse_size}] @var{filename} [@var{filename2} [...]] @var{output_filename}
ETEXI

DEF("info", img_info,
    "info [-f fmt] [--output=ofmt] [--backing-chain] filename")
STEXI
@item info [-f @var{fmt}] [--output=@var{ofmt}] [--backing-chain] @var{filename}
ETEXI

DEF("map", img_map,
    "map [-f fmt] [--output=ofmt] filename")
STEXI
@item map [-f @var{fmt}] [--output=@var{ofmt}] @var{filename}
ETEXI

DEF("snapshot", img_snapshot,
    "snapshot [-q] [-l | -a snapshot | -c snapshot | -d snapshot] filename")
STEXI
@item snapshot [-q] [-l | -a @var{snapshot} | -c @var{snapshot} | -d @var{snapshot}] @var{filename}
ETEXI

DEF("rebase", img_rebase,
    "rebase [-q] [-f fmt] [-t cache] [-p] [-u] -b backing_file [-F backing_fmt] filename")
STEXI
@item rebase [-q] [-f @var{fmt}] [-t @var{cache}] [-p] [-u] -b @var{backing_file} [-F @var{backing_fmt}] @var{filename}
ETEXI

DEF("resize", img_resize,
    "resize [-q] filename [+ | -]size")
STEXI
@item resize [-q] @var{filename} [+ | -]@var{size}
ETEXI

DEF("amend", img_amend,
    "amend [-q] [-f fmt] -o options filename")
STEXI
@item amend [-q] [-f @var{fmt}] -o @var{options} @var{filename}
@end table
ETEXI
