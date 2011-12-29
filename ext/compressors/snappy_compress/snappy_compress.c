#include <errno.h>
#include <snappy-c.h>
#include <stdlib.h>
#include <string.h>

#include <wiredtiger.h>
#include <wiredtiger_ext.h>

WT_EXTENSION_API *wt_api;

static int
wt_snappy_compress(WT_COMPRESSOR *, WT_SESSION *,
    uint8_t *, size_t, uint8_t *, size_t, size_t *, int *);
static int
wt_snappy_decompress(WT_COMPRESSOR *, WT_SESSION *,
    uint8_t *, size_t, uint8_t *, size_t, size_t *);
static int
wt_snappy_pre_size(WT_COMPRESSOR *, WT_SESSION *, uint8_t *, size_t, size_t *);
	
static WT_COMPRESSOR wt_snappy_compressor = {
    wt_snappy_compress, wt_snappy_decompress, wt_snappy_pre_size };

#define	__UNUSED(v)	((void)(v))

int
wiredtiger_extension_init(
    WT_SESSION *session, WT_EXTENSION_API *api, const char *config)
{
	WT_CONNECTION *conn;

	__UNUSED(config);

	wt_api = api;
	conn = session->connection;

	return (conn->add_compressor(
	    conn, "snappy_compress", &wt_snappy_compressor, NULL));
}

/* Snappy WT_COMPRESSOR for WT_CONNECTION::add_compressor. */
/*
 * wt_snappy_error --
 *	Output an error message, and return a standard error code.
 */
static int
wt_snappy_error(WT_SESSION *session, const char *call, snappy_status snret)
{
	const char *msg;

	switch (snret) {
	case SNAPPY_BUFFER_TOO_SMALL:
		msg = "SNAPPY_BUFFER_TOO_SMALL";
		break;
	case SNAPPY_INVALID_INPUT:
		msg = "SNAPPY_INVALID_INPUT";
		break;
	default:
		msg = "unknown error";
		break;
	}

	wiredtiger_err_printf(
	    session, "snappy error: %s: %s: %d", call, msg, snret);
	return (WT_ERROR);
}

static int
wt_snappy_compress(WT_COMPRESSOR *compressor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len,
    size_t *result_lenp, int *compression_failed)
{
	snappy_status snret;
	size_t snaplen;
	char *snapbuf;

	__UNUSED(compressor);

	/*
	 * dst_len was computed in wt_snappy_pre_size, so we know it's big
	 * enough.  Skip past the space we'll use to store the final count
	 * of compressed bytes.
	 */
	snaplen = dst_len - sizeof(size_t);
	snapbuf = dst + sizeof(size_t);

	/* snaplen is an input and an output arg. */
	snret = snappy_compress(src, src_len, snapbuf, &snaplen);

	if (snret == SNAPPY_OK) {
		/*
		 * On decompression, snappy requires the exact compressed byte
		 * count (the current value of snaplen).  WiredTiger does not
		 * preserve that value, so save snaplen at the beginning of the
		 * destination buffer.
		 */
		if (snaplen + sizeof(size_t) < src_len) {
			*(size_t *)dst = snaplen;
			*result_lenp = snaplen + sizeof(size_t);
			*compression_failed = 0;
		} else
			/* The compressor failed to produce a smaller result. */
			*compression_failed = 1;
		return (0);
	}
	return (wt_snappy_error(session, "snappy_compress", snret));
}

static int
wt_snappy_decompress(WT_COMPRESSOR *compressor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    uint8_t *dst, size_t dst_len,
    size_t *result_lenp)
{
	snappy_status snret;
	size_t snaplen;

	__UNUSED(compressor);

	/* retrieve the saved length */
	snaplen = *(size_t *)src;
	if (snaplen + sizeof(size_t) > src_len) {
		wiredtiger_err_printf(
		    session,
		    "wt_snappy_decompress: stored size exceeds buffer size");
		return (WT_ERROR);
	}

	/* dst_len is an input and an output arg. */
	snret = snappy_uncompress(src + sizeof(size_t), snaplen, dst, &dst_len);

	if (snret == SNAPPY_OK) {
		*result_lenp = dst_len;
		return (0);
	}

	return (wt_snappy_error(session, "snappy_decompress", snret));
}

static int
wt_snappy_pre_size(WT_COMPRESSOR *compressor, WT_SESSION *session,
    uint8_t *src, size_t src_len,
    size_t *result_lenp)
{
	__UNUSED(compressor);
	__UNUSED(session);
	__UNUSED(src);

	/*
	 * Snappy requires the dest buffer be somewhat larger than the source.
	 * Fortunately, this is fast to compute, and will give us a dest buffer
	 * in wt_snappy_compress that we can compress to directly.  We add space
	 * in the dest buffer to store the accurate compressed size.
	 */
	*result_lenp = snappy_max_compressed_length(src_len) + sizeof(size_t);
	return (0);
}
/* End Snappy WT_COMPRESSOR for WT_CONNECTION::add_compressor. */
