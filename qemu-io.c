/*
 * Command line utility to exercise the QEMU I/O path.
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <sys/time.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>
#include <libgen.h>

#include "qemu-common.h"
#include "block_int.h"
#include "cmd.h"

#define VERSION	"0.0.1"

#define CMD_NOFILE_OK	0x01

char *progname;
static BlockDriverState *bs;

static int misalign;

/*
 * Parse the pattern argument to various sub-commands.
 *
 * Because the pattern is used as an argument to memset it must evaluate
 * to an unsigned integer that fits into a single byte.
 */
static int parse_pattern(const char *arg)
{
	char *endptr = NULL;
	long pattern;

	pattern = strtol(arg, &endptr, 0);
	if (pattern < 0 || pattern > UCHAR_MAX || *endptr != '\0') {
		printf("%s is not a valid pattern byte\n", arg);
		return -1;
	}

	return pattern;
}

/*
 * Memory allocation helpers.
 *
 * Make sure memory is aligned by default, or purposefully misaligned if
 * that is specified on the command line.
 */

#define MISALIGN_OFFSET		16
static void *qemu_io_alloc(size_t len, int pattern)
{
	void *buf;

	if (misalign)
		len += MISALIGN_OFFSET;
	buf = qemu_blockalign(bs, len);
	memset(buf, pattern, len);
	if (misalign)
		buf += MISALIGN_OFFSET;
	return buf;
}

static void qemu_io_free(void *p)
{
	if (misalign)
		p -= MISALIGN_OFFSET;
	qemu_vfree(p);
}

static void
dump_buffer(const void *buffer, int64_t offset, int len)
{
	int i, j;
	const uint8_t *p;

	for (i = 0, p = buffer; i < len; i += 16) {
		const uint8_t *s = p;

                printf("%08" PRIx64 ":  ", offset + i);
		for (j = 0; j < 16 && i + j < len; j++, p++)
			printf("%02x ", *p);
		printf(" ");
		for (j = 0; j < 16 && i + j < len; j++, s++) {
			if (isalnum(*s))
				printf("%c", *s);
			else
				printf(".");
		}
		printf("\n");
	}
}

static void
print_report(const char *op, struct timeval *t, int64_t offset,
		int count, int total, int cnt, int Cflag)
{
	char s1[64], s2[64], ts[64];

	timestr(t, ts, sizeof(ts), Cflag ? VERBOSE_FIXED_TIME : 0);
	if (!Cflag) {
		cvtstr((double)total, s1, sizeof(s1));
		cvtstr(tdiv((double)total, *t), s2, sizeof(s2));
                printf("%s %d/%d bytes at offset %" PRId64 "\n",
                       op, total, count, offset);
		printf("%s, %d ops; %s (%s/sec and %.4f ops/sec)\n",
			s1, cnt, ts, s2, tdiv((double)cnt, *t));
	} else {/* bytes,ops,time,bytes/sec,ops/sec */
		printf("%d,%d,%s,%.3f,%.3f\n",
			total, cnt, ts,
			tdiv((double)total, *t),
			tdiv((double)cnt, *t));
	}
}

/*
 * Parse multiple length statements for vectored I/O, and construct an I/O
 * vector matching it.
 */
static void *
create_iovec(QEMUIOVector *qiov, char **argv, int nr_iov, int pattern)
{
	size_t *sizes = calloc(nr_iov, sizeof(size_t));
	size_t count = 0;
	void *buf = NULL;
	void *p;
	int i;

	for (i = 0; i < nr_iov; i++) {
		char *arg = argv[i];
                int64_t len;

		len = cvtnum(arg);
		if (len < 0) {
			printf("non-numeric length argument -- %s\n", arg);
			goto fail;
		}

		/* should be SIZE_T_MAX, but that doesn't exist */
		if (len > INT_MAX) {
			printf("too large length argument -- %s\n", arg);
			goto fail;
		}

		if (len & 0x1ff) {
                        printf("length argument %" PRId64
                               " is not sector aligned\n", len);
			goto fail;
		}

		sizes[i] = len;
		count += len;
	}

	qemu_iovec_init(qiov, nr_iov);

	buf = p = qemu_io_alloc(count, pattern);

	for (i = 0; i < nr_iov; i++) {
		qemu_iovec_add(qiov, p, sizes[i]);
		p += sizes[i];
	}

fail:
	free(sizes);
	return buf;
}

static int do_read(char *buf, int64_t offset, int count, int *total)
{
	int ret;

	ret = bdrv_read(bs, offset >> 9, (uint8_t *)buf, count >> 9);
	if (ret < 0)
		return ret;
	*total = count;
	return 1;
}

static int do_write(char *buf, int64_t offset, int count, int *total)
{
	int ret;

	ret = bdrv_write(bs, offset >> 9, (uint8_t *)buf, count >> 9);
	if (ret < 0)
		return ret;
	*total = count;
	return 1;
}

static int do_pread(char *buf, int64_t offset, int count, int *total)
{
	*total = bdrv_pread(bs, offset, (uint8_t *)buf, count);
	if (*total < 0)
		return *total;
	return 1;
}

static int do_pwrite(char *buf, int64_t offset, int count, int *total)
{
	*total = bdrv_pwrite(bs, offset, (uint8_t *)buf, count);
	if (*total < 0)
		return *total;
	return 1;
}

static int do_load_vmstate(char *buf, int64_t offset, int count, int *total)
{
	*total = bdrv_load_vmstate(bs, (uint8_t *)buf, offset, count);
	if (*total < 0)
		return *total;
	return 1;
}

static int do_save_vmstate(char *buf, int64_t offset, int count, int *total)
{
	*total = bdrv_save_vmstate(bs, (uint8_t *)buf, offset, count);
	if (*total < 0)
		return *total;
	return 1;
}

#define NOT_DONE 0x7fffffff
static void aio_rw_done(void *opaque, int ret)
{
	*(int *)opaque = ret;
}

static int do_aio_readv(QEMUIOVector *qiov, int64_t offset, int *total)
{
	BlockDriverAIOCB *acb;
	int async_ret = NOT_DONE;

	acb = bdrv_aio_readv(bs, offset >> 9, qiov, qiov->size >> 9,
			     aio_rw_done, &async_ret);
	if (!acb)
		return -EIO;

	while (async_ret == NOT_DONE)
		qemu_aio_wait();

	*total = qiov->size;
	return async_ret < 0 ? async_ret : 1;
}

static int do_aio_writev(QEMUIOVector *qiov, int64_t offset, int *total)
{
	BlockDriverAIOCB *acb;
	int async_ret = NOT_DONE;

	acb = bdrv_aio_writev(bs, offset >> 9, qiov, qiov->size >> 9,
			      aio_rw_done, &async_ret);
	if (!acb)
		return -EIO;

	while (async_ret == NOT_DONE)
		qemu_aio_wait();

	*total = qiov->size;
	return async_ret < 0 ? async_ret : 1;
}

struct multiwrite_async_ret {
	int num_done;
	int error;
};

static void multiwrite_cb(void *opaque, int ret)
{
	struct multiwrite_async_ret *async_ret = opaque;

	async_ret->num_done++;
	if (ret < 0) {
		async_ret->error = ret;
	}
}

static int do_aio_multiwrite(BlockRequest* reqs, int num_reqs, int *total)
{
	int i, ret;
	struct multiwrite_async_ret async_ret = {
		.num_done = 0,
		.error = 0,
	};

	*total = 0;
	for (i = 0; i < num_reqs; i++) {
		reqs[i].cb = multiwrite_cb;
		reqs[i].opaque = &async_ret;
		*total += reqs[i].qiov->size;
	}

	ret = bdrv_aio_multiwrite(bs, reqs, num_reqs);
	if (ret < 0) {
		return ret;
	}

	while (async_ret.num_done < num_reqs) {
		qemu_aio_wait();
	}

	return async_ret.error < 0 ? async_ret.error : 1;
}

static void
read_help(void)
{
	printf(
"\n"
" reads a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'read -v 512 1k' - dumps 1 kilobyte read from 512 bytes into the file\n"
"\n"
" Reads a segment of the currently open file, optionally dumping it to the\n"
" standard output stream (with -v option) for subsequent inspection.\n"
" -b, -- read from the VM state rather than the virtual disk\n"
" -C, -- report statistics in a machine parsable format\n"
" -l, -- length for pattern verification (only with -P)\n"
" -p, -- use bdrv_pread to read the file\n"
" -P, -- use a pattern to verify read data\n"
" -q, -- quiet mode, do not show I/O statistics\n"
" -s, -- start offset for pattern verification (only with -P)\n"
" -v, -- dump buffer to standard output\n"
"\n");
}

static int read_f(int argc, char **argv);

static const cmdinfo_t read_cmd = {
	.name		= "read",
	.altname	= "r",
	.cfunc		= read_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-abCpqv] [-P pattern [-s off] [-l len]] off len",
	.oneline	= "reads a number of bytes at a specified offset",
	.help		= read_help,
};

static int
read_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, pflag = 0, qflag = 0, vflag = 0;
	int Pflag = 0, sflag = 0, lflag = 0, bflag = 0;
	int c, cnt;
	char *buf;
	int64_t offset;
	int count;
        /* Some compilers get confused and warn if this is not initialized.  */
        int total = 0;
	int pattern = 0, pattern_offset = 0, pattern_count = 0;

	while ((c = getopt(argc, argv, "bCl:pP:qs:v")) != EOF) {
		switch (c) {
		case 'b':
			bflag = 1;
			break;
		case 'C':
			Cflag = 1;
			break;
		case 'l':
			lflag = 1;
			pattern_count = cvtnum(optarg);
			if (pattern_count < 0) {
				printf("non-numeric length argument -- %s\n", optarg);
				return 0;
			}
			break;
		case 'p':
			pflag = 1;
			break;
		case 'P':
			Pflag = 1;
			pattern = parse_pattern(optarg);
			if (pattern < 0)
				return 0;
			break;
		case 'q':
			qflag = 1;
			break;
		case 's':
			sflag = 1;
			pattern_offset = cvtnum(optarg);
			if (pattern_offset < 0) {
				printf("non-numeric length argument -- %s\n", optarg);
				return 0;
			}
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			return command_usage(&read_cmd);
		}
	}

	if (optind != argc - 2)
		return command_usage(&read_cmd);

	if (bflag && pflag) {
		printf("-b and -p cannot be specified at the same time\n");
		return 0;
	}

	offset = cvtnum(argv[optind]);
	if (offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}

	optind++;
	count = cvtnum(argv[optind]);
	if (count < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}

    if (!Pflag && (lflag || sflag)) {
        return command_usage(&read_cmd);
    }

    if (!lflag) {
        pattern_count = count - pattern_offset;
    }

    if ((pattern_count < 0) || (pattern_count + pattern_offset > count))  {
        printf("pattern verfication range exceeds end of read data\n");
        return 0;
    }

	if (!pflag)
		if (offset & 0x1ff) {
                        printf("offset %" PRId64 " is not sector aligned\n",
                               offset);
			return 0;

		if (count & 0x1ff) {
			printf("count %d is not sector aligned\n",
				count);
			return 0;
		}
	}

	buf = qemu_io_alloc(count, 0xab);

	gettimeofday(&t1, NULL);
	if (pflag)
		cnt = do_pread(buf, offset, count, &total);
	else if (bflag)
		cnt = do_load_vmstate(buf, offset, count, &total);
	else
		cnt = do_read(buf, offset, count, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("read failed: %s\n", strerror(-cnt));
		goto out;
	}

	if (Pflag) {
		void* cmp_buf = malloc(pattern_count);
		memset(cmp_buf, pattern, pattern_count);
		if (memcmp(buf + pattern_offset, cmp_buf, pattern_count)) {
			printf("Pattern verification failed at offset %"
                               PRId64 ", %d bytes\n",
                               offset + pattern_offset, pattern_count);
		}
		free(cmp_buf);
	}

	if (qflag)
		goto out;

        if (vflag)
		dump_buffer(buf, offset, count);

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("read", &t2, offset, count, total, cnt, Cflag);

out:
	qemu_io_free(buf);

	return 0;
}

static void
readv_help(void)
{
	printf(
"\n"
" reads a range of bytes from the given offset into multiple buffers\n"
"\n"
" Example:\n"
" 'readv -v 512 1k 1k ' - dumps 2 kilobytes read from 512 bytes into the file\n"
"\n"
" Reads a segment of the currently open file, optionally dumping it to the\n"
" standard output stream (with -v option) for subsequent inspection.\n"
" Uses multiple iovec buffers if more than one byte range is specified.\n"
" -C, -- report statistics in a machine parsable format\n"
" -P, -- use a pattern to verify read data\n"
" -v, -- dump buffer to standard output\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int readv_f(int argc, char **argv);

static const cmdinfo_t readv_cmd = {
	.name		= "readv",
	.cfunc		= readv_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cqv] [-P pattern ] off len [len..]",
	.oneline	= "reads a number of bytes at a specified offset",
	.help		= readv_help,
};

static int
readv_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, qflag = 0, vflag = 0;
	int c, cnt;
	char *buf;
	int64_t offset;
        /* Some compilers get confused and warn if this is not initialized.  */
        int total = 0;
	int nr_iov;
	QEMUIOVector qiov;
	int pattern = 0;
	int Pflag = 0;

	while ((c = getopt(argc, argv, "CP:qv")) != EOF) {
		switch (c) {
		case 'C':
			Cflag = 1;
			break;
		case 'P':
			Pflag = 1;
			pattern = parse_pattern(optarg);
			if (pattern < 0)
				return 0;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			return command_usage(&readv_cmd);
		}
	}

	if (optind > argc - 2)
		return command_usage(&readv_cmd);


	offset = cvtnum(argv[optind]);
	if (offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}
	optind++;

	if (offset & 0x1ff) {
                printf("offset %" PRId64 " is not sector aligned\n",
                       offset);
		return 0;
	}

	nr_iov = argc - optind;
	buf = create_iovec(&qiov, &argv[optind], nr_iov, 0xab);

	gettimeofday(&t1, NULL);
	cnt = do_aio_readv(&qiov, offset, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("readv failed: %s\n", strerror(-cnt));
		goto out;
	}

	if (Pflag) {
		void* cmp_buf = malloc(qiov.size);
		memset(cmp_buf, pattern, qiov.size);
		if (memcmp(buf, cmp_buf, qiov.size)) {
			printf("Pattern verification failed at offset %"
                               PRId64 ", %zd bytes\n",
                               offset, qiov.size);
		}
		free(cmp_buf);
	}

	if (qflag)
		goto out;

        if (vflag)
		dump_buffer(buf, offset, qiov.size);

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("read", &t2, offset, qiov.size, total, cnt, Cflag);

out:
	qemu_io_free(buf);
	return 0;
}

static void
write_help(void)
{
	printf(
"\n"
" writes a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'write 512 1k' - writes 1 kilobyte at 512 bytes into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd).\n"
" -b, -- write to the VM state rather than the virtual disk\n"
" -p, -- use bdrv_pwrite to write the file\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int write_f(int argc, char **argv);

static const cmdinfo_t write_cmd = {
	.name		= "write",
	.altname	= "w",
	.cfunc		= write_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-abCpq] [-P pattern ] off len",
	.oneline	= "writes a number of bytes at a specified offset",
	.help		= write_help,
};

static int
write_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, pflag = 0, qflag = 0, bflag = 0;
	int c, cnt;
	char *buf;
	int64_t offset;
	int count;
        /* Some compilers get confused and warn if this is not initialized.  */
        int total = 0;
	int pattern = 0xcd;

	while ((c = getopt(argc, argv, "bCpP:q")) != EOF) {
		switch (c) {
		case 'b':
			bflag = 1;
			break;
		case 'C':
			Cflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'P':
			pattern = parse_pattern(optarg);
			if (pattern < 0)
				return 0;
			break;
		case 'q':
			qflag = 1;
			break;
		default:
			return command_usage(&write_cmd);
		}
	}

	if (optind != argc - 2)
		return command_usage(&write_cmd);

	if (bflag && pflag) {
		printf("-b and -p cannot be specified at the same time\n");
		return 0;
	}

	offset = cvtnum(argv[optind]);
	if (offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}

	optind++;
	count = cvtnum(argv[optind]);
	if (count < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}

	if (!pflag) {
		if (offset & 0x1ff) {
                        printf("offset %" PRId64 " is not sector aligned\n",
                               offset);
			return 0;
		}

		if (count & 0x1ff) {
			printf("count %d is not sector aligned\n",
				count);
			return 0;
		}
	}

	buf = qemu_io_alloc(count, pattern);

	gettimeofday(&t1, NULL);
	if (pflag)
		cnt = do_pwrite(buf, offset, count, &total);
	else if (bflag)
		cnt = do_save_vmstate(buf, offset, count, &total);
	else
		cnt = do_write(buf, offset, count, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("write failed: %s\n", strerror(-cnt));
		goto out;
	}

	if (qflag)
		goto out;

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("wrote", &t2, offset, count, total, cnt, Cflag);

out:
	qemu_io_free(buf);

	return 0;
}

static void
writev_help(void)
{
	printf(
"\n"
" writes a range of bytes from the given offset source from multiple buffers\n"
"\n"
" Example:\n"
" 'write 512 1k 1k' - writes 2 kilobytes at 512 bytes into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd).\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int writev_f(int argc, char **argv);

static const cmdinfo_t writev_cmd = {
	.name		= "writev",
	.cfunc		= writev_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cq] [-P pattern ] off len [len..]",
	.oneline	= "writes a number of bytes at a specified offset",
	.help		= writev_help,
};

static int
writev_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, qflag = 0;
	int c, cnt;
	char *buf;
	int64_t offset;
        /* Some compilers get confused and warn if this is not initialized.  */
        int total = 0;
	int nr_iov;
	int pattern = 0xcd;
	QEMUIOVector qiov;

	while ((c = getopt(argc, argv, "CqP:")) != EOF) {
		switch (c) {
		case 'C':
			Cflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'P':
			pattern = parse_pattern(optarg);
			if (pattern < 0)
				return 0;
			break;
		default:
			return command_usage(&writev_cmd);
		}
	}

	if (optind > argc - 2)
		return command_usage(&writev_cmd);

	offset = cvtnum(argv[optind]);
	if (offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}
	optind++;

	if (offset & 0x1ff) {
                printf("offset %" PRId64 " is not sector aligned\n",
                       offset);
		return 0;
	}

	nr_iov = argc - optind;
	buf = create_iovec(&qiov, &argv[optind], nr_iov, pattern);

	gettimeofday(&t1, NULL);
	cnt = do_aio_writev(&qiov, offset, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("writev failed: %s\n", strerror(-cnt));
		goto out;
	}

	if (qflag)
		goto out;

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("wrote", &t2, offset, qiov.size, total, cnt, Cflag);
out:
	qemu_io_free(buf);
	return 0;
}

static void
multiwrite_help(void)
{
	printf(
"\n"
" writes a range of bytes from the given offset source from multiple buffers,\n"
" in a batch of requests that may be merged by qemu\n"
"\n"
" Example:\n"
" 'multiwrite 512 1k 1k ; 4k 1k' \n"
"  writes 2 kB at 512 bytes and 1 kB at 4 kB into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd). The pattern byte is increased\n"
" by one for each request contained in the multiwrite command.\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int multiwrite_f(int argc, char **argv);

static const cmdinfo_t multiwrite_cmd = {
	.name		= "multiwrite",
	.cfunc		= multiwrite_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cq] [-P pattern ] off len [len..] [; off len [len..]..]",
	.oneline	= "issues multiple write requests at once",
	.help		= multiwrite_help,
};

static int
multiwrite_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, qflag = 0;
	int c, cnt;
	char **buf;
	int64_t offset, first_offset = 0;
	/* Some compilers get confused and warn if this is not initialized.  */
	int total = 0;
	int nr_iov;
	int nr_reqs;
	int pattern = 0xcd;
	QEMUIOVector *qiovs;
	int i;
	BlockRequest *reqs;

	while ((c = getopt(argc, argv, "CqP:")) != EOF) {
		switch (c) {
		case 'C':
			Cflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'P':
			pattern = parse_pattern(optarg);
			if (pattern < 0)
				return 0;
			break;
		default:
			return command_usage(&writev_cmd);
		}
	}

	if (optind > argc - 2)
		return command_usage(&writev_cmd);

	nr_reqs = 1;
	for (i = optind; i < argc; i++) {
		if (!strcmp(argv[i], ";")) {
			nr_reqs++;
		}
	}

	reqs = qemu_malloc(nr_reqs * sizeof(*reqs));
	buf = qemu_malloc(nr_reqs * sizeof(*buf));
	qiovs = qemu_malloc(nr_reqs * sizeof(*qiovs));

	for (i = 0; i < nr_reqs; i++) {
		int j;

		/* Read the offset of the request */
		offset = cvtnum(argv[optind]);
		if (offset < 0) {
			printf("non-numeric offset argument -- %s\n", argv[optind]);
			return 0;
		}
		optind++;

		if (offset & 0x1ff) {
			printf("offset %lld is not sector aligned\n",
				(long long)offset);
			return 0;
		}

        if (i == 0) {
            first_offset = offset;
        }

		/* Read lengths for qiov entries */
		for (j = optind; j < argc; j++) {
			if (!strcmp(argv[j], ";")) {
				break;
			}
		}

		nr_iov = j - optind;

		/* Build request */
		reqs[i].qiov = &qiovs[i];
		buf[i] = create_iovec(reqs[i].qiov, &argv[optind], nr_iov, pattern);
		reqs[i].sector = offset >> 9;
		reqs[i].nb_sectors = reqs[i].qiov->size >> 9;

		optind = j + 1;

		offset += reqs[i].qiov->size;
		pattern++;
	}

	gettimeofday(&t1, NULL);
	cnt = do_aio_multiwrite(reqs, nr_reqs, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("aio_multiwrite failed: %s\n", strerror(-cnt));
		goto out;
	}

	if (qflag)
		goto out;

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("wrote", &t2, first_offset, total, total, cnt, Cflag);
out:
	for (i = 0; i < nr_reqs; i++) {
		qemu_io_free(buf[i]);
		qemu_iovec_destroy(&qiovs[i]);
	}
	qemu_free(buf);
	qemu_free(reqs);
	qemu_free(qiovs);
	return 0;
}

struct aio_ctx {
	QEMUIOVector qiov;
	int64_t offset;
	char *buf;
	int qflag;
	int vflag;
	int Cflag;
	int Pflag;
	int pattern;
	struct timeval t1;
};

static void
aio_write_done(void *opaque, int ret)
{
	struct aio_ctx *ctx = opaque;
	struct timeval t2;

	gettimeofday(&t2, NULL);


	if (ret < 0) {
		printf("aio_write failed: %s\n", strerror(-ret));
		goto out;
	}

	if (ctx->qflag) {
		goto out;
	}

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, ctx->t1);
	print_report("wrote", &t2, ctx->offset, ctx->qiov.size,
		     ctx->qiov.size, 1, ctx->Cflag);
out:
	qemu_io_free(ctx->buf);
	free(ctx);
}

static void
aio_read_done(void *opaque, int ret)
{
	struct aio_ctx *ctx = opaque;
	struct timeval t2;

	gettimeofday(&t2, NULL);

	if (ret < 0) {
		printf("readv failed: %s\n", strerror(-ret));
		goto out;
	}

	if (ctx->Pflag) {
		void *cmp_buf = malloc(ctx->qiov.size);

		memset(cmp_buf, ctx->pattern, ctx->qiov.size);
		if (memcmp(ctx->buf, cmp_buf, ctx->qiov.size)) {
			printf("Pattern verification failed at offset %"
                               PRId64 ", %zd bytes\n",
                               ctx->offset, ctx->qiov.size);
		}
		free(cmp_buf);
	}

	if (ctx->qflag) {
		goto out;
	}

	if (ctx->vflag) {
		dump_buffer(ctx->buf, ctx->offset, ctx->qiov.size);
	}

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, ctx->t1);
	print_report("read", &t2, ctx->offset, ctx->qiov.size,
		     ctx->qiov.size, 1, ctx->Cflag);
out:
	qemu_io_free(ctx->buf);
	free(ctx);
}

static void
aio_read_help(void)
{
	printf(
"\n"
" asynchronously reads a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'aio_read -v 512 1k 1k ' - dumps 2 kilobytes read from 512 bytes into the file\n"
"\n"
" Reads a segment of the currently open file, optionally dumping it to the\n"
" standard output stream (with -v option) for subsequent inspection.\n"
" The read is performed asynchronously and the aio_flush command must be\n"
" used to ensure all outstanding aio requests have been completed\n"
" -C, -- report statistics in a machine parsable format\n"
" -P, -- use a pattern to verify read data\n"
" -v, -- dump buffer to standard output\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int aio_read_f(int argc, char **argv);

static const cmdinfo_t aio_read_cmd = {
	.name		= "aio_read",
	.cfunc		= aio_read_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cqv] [-P pattern ] off len [len..]",
	.oneline	= "asynchronously reads a number of bytes",
	.help		= aio_read_help,
};

static int
aio_read_f(int argc, char **argv)
{
	int nr_iov, c;
	struct aio_ctx *ctx = calloc(1, sizeof(struct aio_ctx));
	BlockDriverAIOCB *acb;

	while ((c = getopt(argc, argv, "CP:qv")) != EOF) {
		switch (c) {
		case 'C':
			ctx->Cflag = 1;
			break;
		case 'P':
			ctx->Pflag = 1;
			ctx->pattern = parse_pattern(optarg);
			if (ctx->pattern < 0) {
                                free(ctx);
				return 0;
                        }
			break;
		case 'q':
			ctx->qflag = 1;
			break;
		case 'v':
			ctx->vflag = 1;
			break;
		default:
			free(ctx);
			return command_usage(&aio_read_cmd);
		}
	}

	if (optind > argc - 2) {
		free(ctx);
		return command_usage(&aio_read_cmd);
	}

	ctx->offset = cvtnum(argv[optind]);
	if (ctx->offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		free(ctx);
		return 0;
	}
	optind++;

	if (ctx->offset & 0x1ff) {
		printf("offset %" PRId64 " is not sector aligned\n",
                       ctx->offset);
		free(ctx);
		return 0;
	}

	nr_iov = argc - optind;
	ctx->buf = create_iovec(&ctx->qiov, &argv[optind], nr_iov, 0xab);

	gettimeofday(&ctx->t1, NULL);
	acb = bdrv_aio_readv(bs, ctx->offset >> 9, &ctx->qiov,
			      ctx->qiov.size >> 9, aio_read_done, ctx);
	if (!acb) {
		free(ctx->buf);
		free(ctx);
		return -EIO;
	}

	return 0;
}

static void
aio_write_help(void)
{
	printf(
"\n"
" asynchronously writes a range of bytes from the given offset source \n"
" from multiple buffers\n"
"\n"
" Example:\n"
" 'aio_write 512 1k 1k' - writes 2 kilobytes at 512 bytes into the open file\n"
"\n"
" Writes into a segment of the currently open file, using a buffer\n"
" filled with a set pattern (0xcdcdcdcd).\n"
" The write is performed asynchronously and the aio_flush command must be\n"
" used to ensure all outstanding aio requests have been completed\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int aio_write_f(int argc, char **argv);

static const cmdinfo_t aio_write_cmd = {
	.name		= "aio_write",
	.cfunc		= aio_write_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cq] [-P pattern ] off len [len..]",
	.oneline	= "asynchronously writes a number of bytes",
	.help		= aio_write_help,
};

static int
aio_write_f(int argc, char **argv)
{
	int nr_iov, c;
	int pattern = 0xcd;
	struct aio_ctx *ctx = calloc(1, sizeof(struct aio_ctx));
	BlockDriverAIOCB *acb;

	while ((c = getopt(argc, argv, "CqP:")) != EOF) {
		switch (c) {
		case 'C':
			ctx->Cflag = 1;
			break;
		case 'q':
			ctx->qflag = 1;
			break;
		case 'P':
			pattern = parse_pattern(optarg);
			if (pattern < 0)
				return 0;
			break;
		default:
			free(ctx);
			return command_usage(&aio_write_cmd);
		}
	}

	if (optind > argc - 2) {
		free(ctx);
		return command_usage(&aio_write_cmd);
	}

	ctx->offset = cvtnum(argv[optind]);
	if (ctx->offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		free(ctx);
		return 0;
	}
	optind++;

	if (ctx->offset & 0x1ff) {
		printf("offset %" PRId64 " is not sector aligned\n",
                       ctx->offset);
		free(ctx);
		return 0;
	}

	nr_iov = argc - optind;
	ctx->buf = create_iovec(&ctx->qiov, &argv[optind], nr_iov, pattern);

	gettimeofday(&ctx->t1, NULL);
	acb = bdrv_aio_writev(bs, ctx->offset >> 9, &ctx->qiov,
			      ctx->qiov.size >> 9, aio_write_done, ctx);
	if (!acb) {
		free(ctx->buf);
		free(ctx);
		return -EIO;
	}

	return 0;
}

static int
aio_flush_f(int argc, char **argv)
{
	qemu_aio_flush();
	return 0;
}

static const cmdinfo_t aio_flush_cmd = {
	.name		= "aio_flush",
	.cfunc		= aio_flush_f,
	.oneline	= "completes all outstanding aio requests"
};

static int
flush_f(int argc, char **argv)
{
	bdrv_flush(bs);
	return 0;
}

static const cmdinfo_t flush_cmd = {
	.name		= "flush",
	.altname	= "f",
	.cfunc		= flush_f,
	.oneline	= "flush all in-core file state to disk",
};

static int
truncate_f(int argc, char **argv)
{
	int64_t offset;
	int ret;

	offset = cvtnum(argv[1]);
	if (offset < 0) {
		printf("non-numeric truncate argument -- %s\n", argv[1]);
		return 0;
	}

	ret = bdrv_truncate(bs, offset);
	if (ret < 0) {
		printf("truncate: %s\n", strerror(-ret));
		return 0;
	}

	return 0;
}

static const cmdinfo_t truncate_cmd = {
	.name		= "truncate",
	.altname	= "t",
	.cfunc		= truncate_f,
	.argmin		= 1,
	.argmax		= 1,
	.args		= "off",
	.oneline	= "truncates the current file at the given offset",
};

static int
length_f(int argc, char **argv)
{
        int64_t size;
	char s1[64];

	size = bdrv_getlength(bs);
	if (size < 0) {
		printf("getlength: %s\n", strerror(-size));
		return 0;
	}

	cvtstr(size, s1, sizeof(s1));
	printf("%s\n", s1);
	return 0;
}


static const cmdinfo_t length_cmd = {
	.name		= "length",
	.altname	= "l",
	.cfunc		= length_f,
	.oneline	= "gets the length of the current file",
};


static int
info_f(int argc, char **argv)
{
	BlockDriverInfo bdi;
	char s1[64], s2[64];
	int ret;

	if (bs->drv && bs->drv->format_name)
		printf("format name: %s\n", bs->drv->format_name);
	if (bs->drv && bs->drv->protocol_name)
		printf("format name: %s\n", bs->drv->protocol_name);

	ret = bdrv_get_info(bs, &bdi);
	if (ret)
		return 0;

	cvtstr(bdi.cluster_size, s1, sizeof(s1));
	cvtstr(bdi.vm_state_offset, s2, sizeof(s2));

	printf("cluster size: %s\n", s1);
	printf("vm state offset: %s\n", s2);

	return 0;
}



static const cmdinfo_t info_cmd = {
	.name		= "info",
	.altname	= "i",
	.cfunc		= info_f,
	.oneline	= "prints information about the current file",
};

static void
discard_help(void)
{
	printf(
"\n"
" discards a range of bytes from the given offset\n"
"\n"
" Example:\n"
" 'discard 512 1k' - discards 1 kilobyte from 512 bytes into the file\n"
"\n"
" Discards a segment of the currently open file.\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quiet mode, do not show I/O statistics\n"
"\n");
}

static int discard_f(int argc, char **argv);

static const cmdinfo_t discard_cmd = {
	.name		= "discard",
	.altname	= "d",
	.cfunc		= discard_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cq] off len",
	.oneline	= "discards a number of bytes at a specified offset",
	.help		= discard_help,
};

static int
discard_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, qflag = 0;
	int c, ret;
	int64_t offset;
	int count;

	while ((c = getopt(argc, argv, "Cq")) != EOF) {
		switch (c) {
		case 'C':
			Cflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		default:
			return command_usage(&discard_cmd);
		}
	}

	if (optind != argc - 2) {
		return command_usage(&discard_cmd);
	}

	offset = cvtnum(argv[optind]);
	if (offset < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}

	optind++;
	count = cvtnum(argv[optind]);
	if (count < 0) {
		printf("non-numeric length argument -- %s\n", argv[optind]);
		return 0;
	}

	gettimeofday(&t1, NULL);
	ret = bdrv_discard(bs, offset >> BDRV_SECTOR_BITS, count >> BDRV_SECTOR_BITS);
	gettimeofday(&t2, NULL);

	if (ret < 0) {
		printf("discard failed: %s\n", strerror(-ret));
		goto out;
	}

	/* Finally, report back -- -C gives a parsable format */
	if (!qflag) {
		t2 = tsub(t2, t1);
		print_report("discard", &t2, offset, count, count, 1, Cflag);
	}

out:
	return 0;
}

static int
alloc_f(int argc, char **argv)
{
	int64_t offset;
	int nb_sectors, remaining;
	char s1[64];
	int num, sum_alloc;
	int ret;

	offset = cvtnum(argv[1]);
	if (offset & 0x1ff) {
                printf("offset %" PRId64 " is not sector aligned\n",
                       offset);
		return 0;
	}

	if (argc == 3)
		nb_sectors = cvtnum(argv[2]);
	else
		nb_sectors = 1;

	remaining = nb_sectors;
	sum_alloc = 0;
	while (remaining) {
		ret = bdrv_is_allocated(bs, offset >> 9, nb_sectors, &num);
		remaining -= num;
		if (ret) {
			sum_alloc += num;
		}
	}

	cvtstr(offset, s1, sizeof(s1));

	printf("%d/%d sectors allocated at offset %s\n",
	       sum_alloc, nb_sectors, s1);
	return 0;
}

static const cmdinfo_t alloc_cmd = {
	.name		= "alloc",
	.altname	= "a",
	.argmin		= 1,
	.argmax		= 2,
	.cfunc		= alloc_f,
	.args		= "off [sectors]",
	.oneline	= "checks if a sector is present in the file",
};

static int
map_f(int argc, char **argv)
{
	int64_t offset;
	int64_t nb_sectors;
	char s1[64];
	int num, num_checked;
	int ret;
	const char *retstr;

	offset = 0;
	nb_sectors = bs->total_sectors;

	do {
		num_checked = MIN(nb_sectors, INT_MAX);
		ret = bdrv_is_allocated(bs, offset, num_checked, &num);
		retstr = ret ? "    allocated" : "not allocated";
		cvtstr(offset << 9ULL, s1, sizeof(s1));
		printf("[% 24" PRId64 "] % 8d/% 8d sectors %s at offset %s (%d)\n",
				offset << 9ULL, num, num_checked, retstr, s1, ret);

		offset += num;
		nb_sectors -= num;
	} while(offset < bs->total_sectors);

	return 0;
}

static const cmdinfo_t map_cmd = {
       .name           = "map",
       .argmin         = 0,
       .argmax         = 0,
       .cfunc          = map_f,
       .args           = "",
       .oneline        = "prints the allocated areas of a file",
};


static int
close_f(int argc, char **argv)
{
	bdrv_close(bs);
	bs = NULL;
	return 0;
}

static const cmdinfo_t close_cmd = {
	.name		= "close",
	.altname	= "c",
	.cfunc		= close_f,
	.oneline	= "close the current open file",
};

static int openfile(char *name, int flags, int growable)
{
	if (bs) {
		fprintf(stderr, "file open already, try 'help close'\n");
		return 1;
	}

	if (growable) {
		if (bdrv_file_open(&bs, name, flags)) {
			fprintf(stderr, "%s: can't open device %s\n", progname, name);
			return 1;
		}
	} else {
		bs = bdrv_new("hda");

		if (bdrv_open(bs, name, flags, NULL) < 0) {
			fprintf(stderr, "%s: can't open device %s\n", progname, name);
			bs = NULL;
			return 1;
		}
	}

	return 0;
}

static void
open_help(void)
{
	printf(
"\n"
" opens a new file in the requested mode\n"
"\n"
" Example:\n"
" 'open -Cn /tmp/data' - creates/opens data file read-write and uncached\n"
"\n"
" Opens a file for subsequent use by all of the other qemu-io commands.\n"
" -r, -- open file read-only\n"
" -s, -- use snapshot file\n"
" -n, -- disable host cache\n"
" -g, -- allow file to grow (only applies to protocols)"
"\n");
}

static int open_f(int argc, char **argv);

static const cmdinfo_t open_cmd = {
	.name		= "open",
	.altname	= "o",
	.cfunc		= open_f,
	.argmin		= 1,
	.argmax		= -1,
	.flags		= CMD_NOFILE_OK,
	.args		= "[-Crsn] [path]",
	.oneline	= "open the file specified by path",
	.help		= open_help,
};

static int
open_f(int argc, char **argv)
{
	int flags = 0;
	int readonly = 0;
	int growable = 0;
	int c;

	while ((c = getopt(argc, argv, "snrg")) != EOF) {
		switch (c) {
		case 's':
			flags |= BDRV_O_SNAPSHOT;
			break;
		case 'n':
			flags |= BDRV_O_NOCACHE;
			break;
		case 'r':
			readonly = 1;
			break;
		case 'g':
			growable = 1;
			break;
		default:
			return command_usage(&open_cmd);
		}
	}

	if (!readonly) {
            flags |= BDRV_O_RDWR;
        }

	if (optind != argc - 1)
		return command_usage(&open_cmd);

	return openfile(argv[optind], flags, growable);
}

static int
init_args_command(
        int     index)
{
	/* only one device allowed so far */
	if (index >= 1)
		return 0;
	return ++index;
}

static int
init_check_command(
	const cmdinfo_t *ct)
{
	if (ct->flags & CMD_FLAG_GLOBAL)
		return 1;
	if (!(ct->flags & CMD_NOFILE_OK) && !bs) {
		fprintf(stderr, "no file open, try 'help open'\n");
		return 0;
	}
	return 1;
}

static void usage(const char *name)
{
	printf(
"Usage: %s [-h] [-V] [-rsnm] [-c cmd] ... [file]\n"
"QEMU Disk exerciser\n"
"\n"
"  -c, --cmd            command to execute\n"
"  -r, --read-only      export read-only\n"
"  -s, --snapshot       use snapshot file\n"
"  -n, --nocache        disable host cache\n"
"  -g, --growable       allow file to grow (only applies to protocols)\n"
"  -m, --misalign       misalign allocations for O_DIRECT\n"
"  -k, --native-aio     use kernel AIO implementation (on Linux only)\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n",
	name);
}


int main(int argc, char **argv)
{
	int readonly = 0;
	int growable = 0;
	const char *sopt = "hVc:rsnmgk";
        const struct option lopt[] = {
		{ "help", 0, NULL, 'h' },
		{ "version", 0, NULL, 'V' },
		{ "offset", 1, NULL, 'o' },
		{ "cmd", 1, NULL, 'c' },
		{ "read-only", 0, NULL, 'r' },
		{ "snapshot", 0, NULL, 's' },
		{ "nocache", 0, NULL, 'n' },
		{ "misalign", 0, NULL, 'm' },
		{ "growable", 0, NULL, 'g' },
		{ "native-aio", 0, NULL, 'k' },
		{ NULL, 0, NULL, 0 }
	};
	int c;
	int opt_index = 0;
	int flags = 0;

	progname = basename(argv[0]);

	while ((c = getopt_long(argc, argv, sopt, lopt, &opt_index)) != -1) {
		switch (c) {
		case 's':
			flags |= BDRV_O_SNAPSHOT;
			break;
		case 'n':
			flags |= BDRV_O_NOCACHE;
			break;
		case 'c':
			add_user_command(optarg);
			break;
		case 'r':
			readonly = 1;
			break;
		case 'm':
			misalign = 1;
			break;
		case 'g':
			growable = 1;
			break;
		case 'k':
			flags |= BDRV_O_NATIVE_AIO;
			break;
		case 'V':
			printf("%s version %s\n", progname, VERSION);
			exit(0);
		case 'h':
			usage(progname);
			exit(0);
		default:
			usage(progname);
			exit(1);
		}
	}

	if ((argc - optind) > 1) {
		usage(progname);
		exit(1);
	}

	bdrv_init();

	/* initialize commands */
	quit_init();
	help_init();
	add_command(&open_cmd);
	add_command(&close_cmd);
	add_command(&read_cmd);
	add_command(&readv_cmd);
	add_command(&write_cmd);
	add_command(&writev_cmd);
	add_command(&multiwrite_cmd);
	add_command(&aio_read_cmd);
	add_command(&aio_write_cmd);
	add_command(&aio_flush_cmd);
	add_command(&flush_cmd);
	add_command(&truncate_cmd);
	add_command(&length_cmd);
	add_command(&info_cmd);
	add_command(&discard_cmd);
	add_command(&alloc_cmd);
	add_command(&map_cmd);

	add_args_command(init_args_command);
	add_check_command(init_check_command);

	/* open the device */
	if (!readonly) {
            flags |= BDRV_O_RDWR;
        }

	if ((argc - optind) == 1)
		openfile(argv[optind], flags, growable);
	command_loop();

	/*
	 * Make sure all outstanding requests get flushed the program exits.
	 */
	qemu_aio_flush();

	if (bs)
		bdrv_close(bs);
	return 0;
}
