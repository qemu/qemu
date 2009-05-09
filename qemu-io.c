/*
 * Command line utility to exercise the QEMU I/O path.
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>

#include "qemu-common.h"
#include "block_int.h"
#include "cmd.h"

#define VERSION	"0.0.1"

#define CMD_NOFILE_OK	0x01

char *progname;
static BlockDriverState *bs;

static int misalign;

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
	buf = qemu_memalign(512, len);
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
dump_buffer(char *buffer, int64_t offset, int len)
{
	int i, j;
	char *p;

	for (i = 0, p = buffer; i < len; i += 16) {
		char    *s = p;

		printf("%08llx:  ", (unsigned long long)offset + i);
		for (j = 0; j < 16 && i + j < len; j++, p++)
			printf("%02x ", *p);
		printf(" ");
		for (j = 0; j < 16 && i + j < len; j++, s++) {
			if (isalnum((int)*s))
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
		printf("%s %d/%d bytes at offset %lld\n",
			op, total, count, (long long)offset);
		printf("%s, %d ops; %s (%s/sec and %.4f ops/sec)\n",
			s1, cnt, ts, s2, tdiv((double)cnt, *t));
	} else {/* bytes,ops,time,bytes/sec,ops/sec */
		printf("%d,%d,%s,%.3f,%.3f\n",
			total, cnt, ts,
			tdiv((double)total, *t),
			tdiv((double)cnt, *t));
	}
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


static const cmdinfo_t read_cmd;

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
" -C, -- report statistics in a machine parsable format\n"
" -l, -- length for pattern verification (only with -P)\n"
" -p, -- use bdrv_pread to read the file\n"
" -P, -- use a pattern to verify read data\n"
" -q, -- quite mode, do not show I/O statistics\n"
" -s, -- start offset for pattern verification (only with -P)\n"
" -v, -- dump buffer to standard output\n"
"\n");
}

static int
read_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, pflag = 0, qflag = 0, vflag = 0;
	int Pflag = 0, sflag = 0, lflag = 0;
	int c, cnt;
	char *buf;
	int64_t offset;
	int count;
        /* Some compilers get confused and warn if this is not initialized.  */
        int total = 0;
	int pattern = 0, pattern_offset = 0, pattern_count = 0;

	while ((c = getopt(argc, argv, "Cl:pP:qs:v")) != EOF) {
		switch (c) {
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
			pattern = atoi(optarg);
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
			printf("offset %lld is not sector aligned\n",
				(long long)offset);
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
	else
		cnt = do_read(buf, offset, count, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("read failed: %s\n", strerror(-cnt));
		return 0;
	}

	if (Pflag) {
		void* cmp_buf = malloc(pattern_count);
		memset(cmp_buf, pattern, pattern_count);
		if (memcmp(buf + pattern_offset, cmp_buf, pattern_count)) {
			printf("Pattern verification failed at offset %lld, "
				"%d bytes\n",
				(long long) offset + pattern_offset, pattern_count);
		}
		free(cmp_buf);
	}

	if (qflag)
		return 0;

        if (vflag)
		dump_buffer(buf, offset, count);

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("read", &t2, offset, count, total, cnt, Cflag);

	qemu_io_free(buf);

	return 0;
}

static const cmdinfo_t read_cmd = {
	.name		= "read",
	.altname	= "r",
	.cfunc		= read_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-aCpqv] [-P pattern [-s off] [-l len]] off len",
	.oneline	= "reads a number of bytes at a specified offset",
	.help		= read_help,
};

static const cmdinfo_t readv_cmd;

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
" -q, -- quite mode, do not show I/O statistics\n"
"\n");
}

static int
readv_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, qflag = 0, vflag = 0;
	int c, cnt;
	char *buf, *p;
	int64_t offset;
	int count = 0, total;
	int nr_iov, i;
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
			pattern = atoi(optarg);
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
		printf("offset %lld is not sector aligned\n",
			(long long)offset);
		return 0;
	}

	if (count & 0x1ff) {
		printf("count %d is not sector aligned\n",
			count);
		return 0;
	}

	for (i = optind; i < argc; i++) {
	        size_t len;

		len = cvtnum(argv[i]);
		if (len < 0) {
			printf("non-numeric length argument -- %s\n", argv[i]);
			return 0;
		}
		count += len;
	}

	nr_iov = argc - optind;
	qemu_iovec_init(&qiov, nr_iov);
	buf = p = qemu_io_alloc(count, 0xab);
	for (i = 0; i < nr_iov; i++) {
	        size_t len;

		len = cvtnum(argv[optind]);
		if (len < 0) {
			printf("non-numeric length argument -- %s\n",
				argv[optind]);
			return 0;
		}

		qemu_iovec_add(&qiov, p, len);
		p += len;
		optind++;
	}

	gettimeofday(&t1, NULL);
	cnt = do_aio_readv(&qiov, offset, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("readv failed: %s\n", strerror(-cnt));
		return 0;
	}

	if (Pflag) {
		void* cmp_buf = malloc(count);
		memset(cmp_buf, pattern, count);
		if (memcmp(buf, cmp_buf, count)) {
			printf("Pattern verification failed at offset %lld, "
				"%d bytes\n",
				(long long) offset, count);
		}
		free(cmp_buf);
	}

	if (qflag)
		return 0;

        if (vflag)
		dump_buffer(buf, offset, qiov.size);

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("read", &t2, offset, qiov.size, total, cnt, Cflag);

	qemu_io_free(buf);

	return 0;
}

static const cmdinfo_t readv_cmd = {
	.name		= "readv",
	.cfunc		= readv_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-Cqv] [-P pattern ] off len [len..]",
	.oneline	= "reads a number of bytes at a specified offset",
	.help		= readv_help,
};

static const cmdinfo_t write_cmd;

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
" -p, -- use bdrv_pwrite to write the file\n"
" -P, -- use different pattern to fill file\n"
" -C, -- report statistics in a machine parsable format\n"
" -q, -- quite mode, do not show I/O statistics\n"
"\n");
}

static int
write_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, pflag = 0, qflag = 0;
	int c, cnt;
	char *buf;
	int64_t offset;
	int count;
        /* Some compilers get confused and warn if this is not initialized.  */
        int total = 0;
	int pattern = 0xcd;

	while ((c = getopt(argc, argv, "CpP:q")) != EOF) {
		switch (c) {
		case 'C':
			Cflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'P':
			pattern = atoi(optarg);
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
			printf("offset %lld is not sector aligned\n",
				(long long)offset);
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
	else
		cnt = do_write(buf, offset, count, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("write failed: %s\n", strerror(-cnt));
		return 0;
	}

	if (qflag)
		return 0;

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("wrote", &t2, offset, count, total, cnt, Cflag);

	qemu_io_free(buf);

	return 0;
}

static const cmdinfo_t write_cmd = {
	.name		= "write",
	.altname	= "w",
	.cfunc		= write_f,
	.argmin		= 2,
	.argmax		= -1,
	.args		= "[-aCpq] [-P pattern ] off len",
	.oneline	= "writes a number of bytes at a specified offset",
	.help		= write_help,
};

static const cmdinfo_t writev_cmd;

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
" -q, -- quite mode, do not show I/O statistics\n"
"\n");
}

static int
writev_f(int argc, char **argv)
{
	struct timeval t1, t2;
	int Cflag = 0, qflag = 0;
	int c, cnt;
	char *buf, *p;
	int64_t offset;
	int count = 0, total;
	int nr_iov, i;
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
			pattern = atoi(optarg);
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
		printf("offset %lld is not sector aligned\n",
			(long long)offset);
		return 0;
	}

	if (count & 0x1ff) {
		printf("count %d is not sector aligned\n",
			count);
		return 0;
	}


	for (i = optind; i < argc; i++) {
	        size_t len;

		len = cvtnum(argv[optind]);
		if (len < 0) {
			printf("non-numeric length argument -- %s\n", argv[i]);
			return 0;
		}
		count += len;
	}

	nr_iov = argc - optind;
	qemu_iovec_init(&qiov, nr_iov);
	buf = p = qemu_io_alloc(count, pattern);
	for (i = 0; i < nr_iov; i++) {
	        size_t len;

		len = cvtnum(argv[optind]);
		if (len < 0) {
			printf("non-numeric length argument -- %s\n",
				argv[optind]);
			return 0;
		}

		qemu_iovec_add(&qiov, p, len);
		p += len;
		optind++;
	}

	gettimeofday(&t1, NULL);
	cnt = do_aio_writev(&qiov, offset, &total);
	gettimeofday(&t2, NULL);

	if (cnt < 0) {
		printf("writev failed: %s\n", strerror(-cnt));
		return 0;
	}

	if (qflag)
		return 0;

	/* Finally, report back -- -C gives a parsable format */
	t2 = tsub(t2, t1);
	print_report("wrote", &t2, offset, qiov.size, total, cnt, Cflag);

	qemu_io_free(buf);

	return 0;
}

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
		printf("truncate: %s", strerror(ret));
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
		printf("getlength: %s", strerror(size));
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

static int
alloc_f(int argc, char **argv)
{
	int64_t offset;
	int nb_sectors;
	char s1[64];
	int num;
	int ret;
	const char *retstr;

	offset = cvtnum(argv[1]);
	if (offset & 0x1ff) {
		printf("offset %lld is not sector aligned\n",
			(long long)offset);
		return 0;
	}

	if (argc == 3)
		nb_sectors = cvtnum(argv[2]);
	else
		nb_sectors = 1;

	ret = bdrv_is_allocated(bs, offset >> 9, nb_sectors, &num);

	cvtstr(offset, s1, sizeof(s1));

	retstr = ret ? "allocated" : "not allocated";
	if (nb_sectors == 1)
		printf("sector %s at offset %s\n", retstr, s1);
	else
		printf("%d/%d sectors %s at offset %s\n",
			num, nb_sectors, retstr, s1);
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

static int openfile(char *name, int flags)
{
	if (bs) {
		fprintf(stderr, "file open already, try 'help close'\n");
		return 1;
	}

	bs = bdrv_new("hda");
	if (!bs)
		return 1;

	if (bdrv_open(bs, name, flags) == -1) {
		fprintf(stderr, "%s: can't open device %s\n", progname, name);
		bs = NULL;
		return 1;
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
" -C, -- create new file if it doesn't exist\n"
" -r, -- open file read-only\n"
" -s, -- use snapshot file\n"
" -n, -- disable host cache\n"
"\n");
}

static const cmdinfo_t open_cmd;

static int
open_f(int argc, char **argv)
{
	int flags = 0;
	int readonly = 0;
	int c;

	while ((c = getopt(argc, argv, "snCr")) != EOF) {
		switch (c) {
		case 's':
			flags |= BDRV_O_SNAPSHOT;
			break;
		case 'n':
			flags |= BDRV_O_NOCACHE;
			break;
		case 'C':
			flags |= BDRV_O_CREAT;
			break;
		case 'r':
			readonly = 1;
			break;
		default:
			return command_usage(&open_cmd);
		}
	}

	if (readonly)
		flags |= BDRV_O_RDONLY;
	else
		flags |= BDRV_O_RDWR;

	if (optind != argc - 1)
		return command_usage(&open_cmd);

	return openfile(argv[optind], flags);
}

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
"Usage: %s [-h] [-V] [-Crsnm] [-c cmd] ... [file]\n"
"QEMU Disk excerciser\n"
"\n"
"  -C, --create         create new file if it doesn't exist\n"
"  -c, --cmd            command to execute\n"
"  -r, --read-only      export read-only\n"
"  -s, --snapshot       use snapshot file\n"
"  -n, --nocache        disable host cache\n"
"  -m, --misalign       misalign allocations for O_DIRECT\n"
"  -h, --help           display this help and exit\n"
"  -V, --version        output version information and exit\n"
"\n",
	name);
}


int main(int argc, char **argv)
{
	int readonly = 0;
	const char *sopt = "hVc:Crsnm";
	struct option lopt[] = {
		{ "help", 0, 0, 'h' },
		{ "version", 0, 0, 'V' },
		{ "offset", 1, 0, 'o' },
		{ "cmd", 1, 0, 'c' },
		{ "create", 0, 0, 'C' },
		{ "read-only", 0, 0, 'r' },
		{ "snapshot", 0, 0, 's' },
		{ "nocache", 0, 0, 'n' },
		{ "misalign", 0, 0, 'm' },
		{ NULL, 0, 0, 0 }
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
		case 'C':
			flags |= BDRV_O_CREAT;
			break;
		case 'r':
			readonly = 1;
			break;
		case 'm':
			misalign = 1;
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
	add_command(&flush_cmd);
	add_command(&truncate_cmd);
	add_command(&length_cmd);
	add_command(&info_cmd);
	add_command(&alloc_cmd);

	add_args_command(init_args_command);
	add_check_command(init_check_command);

	/* open the device */
	if (readonly)
		flags |= BDRV_O_RDONLY;
	else
		flags |= BDRV_O_RDWR;

	if ((argc - optind) == 1)
		openfile(argv[optind], flags);
	command_loop();

	if (bs)
		bdrv_close(bs);
	return 0;
}
