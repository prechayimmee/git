#include "../git-compat-util.h"
#include "../alloc.h"
#include "../config.h"
#include "../gettext.h"
#include "../hash.h"
#include "../hex.h"
#include "../refs.h"
#include "refs-internal.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../lockfile.h"
#include "../chdir-notify.h"
#include "../statinfo.h"
#include "../wrapper.h"
#include "../write-or-die.h"
#include "../trace2.h"

enum mmap_strategy {
	/*
	 * Don't use mmap() at all for reading `packed-refs`.
	 */
	MMAP_NONE,

	/*
	 * Can use mmap() for reading `packed-refs`, but the file must
	 * not remain mmapped. This is the usual option on Windows,
	 * where you cannot rename a new version of a file onto a file
	 * that is currently mmapped.
	 */
	MMAP_TEMPORARY,

	/*
	 * It is OK to leave the `packed-refs` file mmapped while
	 * arbitrary other code is running.
	 */
	MMAP_OK
};

#if defined(NO_MMAP)
static enum mmap_strategy mmap_strategy = MMAP_NONE;
#elif defined(MMAP_PREVENTS_DELETE)
static enum mmap_strategy mmap_strategy = MMAP_TEMPORARY;
#else
static enum mmap_strategy mmap_strategy = MMAP_OK;
#endif

/*
 * Increment the reference count of `*snapshot`.
 */
static void acquire_snapshot(struct snapshot *snapshot)
{
	snapshot->referrers++;
}

/*
 * If the buffer in `snapshot` is active, then either munmap the
 * memory and close the file, or free the memory. Then set the buffer
 * pointers to NULL.
 */
void clear_snapshot_buffer(struct snapshot *snapshot)
{
	if (snapshot->mmapped) {
		if (munmap(snapshot->buf, snapshot->eof - snapshot->buf))
			die_errno("error ummapping packed-refs file %s",
				  snapshot->refs->path);
		snapshot->mmapped = 0;
	} else {
		free(snapshot->buf);
	}
	snapshot->buf = snapshot->start = snapshot->eof = NULL;
}

/*
 * Decrease the reference count of `*snapshot`. If it goes to zero,
 * free `*snapshot` and return true; otherwise return false.
 */
int release_snapshot(struct snapshot *snapshot)
{
	if (!--snapshot->referrers) {
		stat_validity_clear(&snapshot->validity);
		clear_snapshot_buffer(snapshot);
		free(snapshot);
		return 1;
	} else {
		return 0;
	}
}

struct ref_store *packed_ref_store_create(struct repository *repo,
					  const char *gitdir,
					  unsigned int store_flags)
{
	struct packed_ref_store *refs = xcalloc(1, sizeof(*refs));
	struct ref_store *ref_store = (struct ref_store *)refs;
	struct strbuf sb = STRBUF_INIT;

	base_ref_store_init(ref_store, repo, gitdir, &refs_be_packed);
	refs->store_flags = store_flags;

	strbuf_addf(&sb, "%s/packed-refs", gitdir);
	refs->path = strbuf_detach(&sb, NULL);
	chdir_notify_reparent("packed-refs", &refs->path);
	return ref_store;
}

/*
 * Downcast `ref_store` to `packed_ref_store`. Die if `ref_store` is
 * not a `packed_ref_store`. Also die if `packed_ref_store` doesn't
 * support at least the flags specified in `required_flags`. `caller`
 * is used in any necessary error messages.
 */
static struct packed_ref_store *packed_downcast(struct ref_store *ref_store,
						unsigned int required_flags,
						const char *caller)
{
	struct packed_ref_store *refs;

	if (ref_store->be != &refs_be_packed)
		BUG("ref_store is type \"%s\" not \"packed\" in %s",
		    ref_store->be->name, caller);

	refs = (struct packed_ref_store *)ref_store;

	if ((refs->store_flags & required_flags) != required_flags)
		BUG("unallowed operation (%s), requires %x, has %x\n",
		    caller, required_flags, refs->store_flags);

	return refs;
}

static void clear_snapshot(struct packed_ref_store *refs)
{
	if (refs->snapshot) {
		struct snapshot *snapshot = refs->snapshot;

		refs->snapshot = NULL;
		release_snapshot(snapshot);
	}
}

<<<<<<< HEAD
static NORETURN void die_unterminated_line(const char *path,
					   const char *p, size_t len)
{
	if (len < 80)
		die("unterminated line in %s: %.*s", path, (int)len, p);
	else
		die("unterminated line in %s: %.75s...", path, p);
}

static NORETURN void die_invalid_line(const char *path,
				      const char *p, size_t len)
{
	const char *eol = memchr(p, '\n', len);

	if (!eol)
		die_unterminated_line(path, p, len);
	else if (eol - p < 80)
		die("unexpected line in %s: %.*s", path, (int)(eol - p), p);
	else
		die("unexpected line in %s: %.75s...", path, p);

}

struct snapshot_record {
	const char *start;
	size_t len;
};

static int cmp_packed_ref_records(const void *v1, const void *v2)
{
	const struct snapshot_record *e1 = v1, *e2 = v2;
	const char *r1 = e1->start + the_hash_algo->hexsz + 1;
	const char *r2 = e2->start + the_hash_algo->hexsz + 1;

	while (1) {
		if (*r1 == '\n')
			return *r2 == '\n' ? 0 : -1;
		if (*r1 != *r2) {
			if (*r2 == '\n')
				return 1;
			else
				return (unsigned char)*r1 < (unsigned char)*r2 ? -1 : +1;
		}
		r1++;
		r2++;
	}
}

/*
 * Compare a snapshot record at `rec` to the specified NUL-terminated
 * refname.
 */
static int cmp_record_to_refname(const char *rec, const char *refname,
				 int start)
{
	const char *r1 = rec + the_hash_algo->hexsz + 1;
	const char *r2 = refname;

	while (1) {
		if (*r1 == '\n')
			return *r2 ? -1 : 0;
		if (!*r2)
			return start ? 1 : -1;
		if (*r1 != *r2)
			return (unsigned char)*r1 < (unsigned char)*r2 ? -1 : +1;
		r1++;
		r2++;
	}
}

/*
 * `snapshot->buf` is not known to be sorted. Check whether it is, and
 * if not, sort it into new memory and munmap/free the old storage.
 */
static void sort_snapshot(struct snapshot *snapshot)
{
	struct snapshot_record *records = NULL;
	size_t alloc = 0, nr = 0;
	int sorted = 1;
	const char *pos, *eof, *eol;
	size_t len, i;
	char *new_buffer, *dst;

	pos = snapshot->start;
	eof = snapshot->eof;

	if (pos == eof)
		return;

	len = eof - pos;

	/*
	 * Initialize records based on a crude estimate of the number
	 * of references in the file (we'll grow it below if needed):
	 */
	ALLOC_GROW(records, len / 80 + 20, alloc);

	while (pos < eof) {
		eol = memchr(pos, '\n', eof - pos);
		if (!eol)
			/* The safety check should prevent this. */
			BUG("unterminated line found in packed-refs");
		if (eol - pos < the_hash_algo->hexsz + 2)
			die_invalid_line(snapshot->refs->path,
					 pos, eof - pos);
		eol++;
		if (eol < eof && *eol == '^') {
			/*
			 * Keep any peeled line together with its
			 * reference:
			 */
			const char *peeled_start = eol;

			eol = memchr(peeled_start, '\n', eof - peeled_start);
			if (!eol)
				/* The safety check should prevent this. */
				BUG("unterminated peeled line found in packed-refs");
			eol++;
		}

		ALLOC_GROW(records, nr + 1, alloc);
		records[nr].start = pos;
		records[nr].len = eol - pos;
		nr++;

		if (sorted &&
		    nr > 1 &&
		    cmp_packed_ref_records(&records[nr - 2],
					   &records[nr - 1]) >= 0)
			sorted = 0;

		pos = eol;
	}

	if (sorted)
		goto cleanup;

	/* We need to sort the memory. First we sort the records array: */
	QSORT(records, nr, cmp_packed_ref_records);

	/*
	 * Allocate a new chunk of memory, and copy the old memory to
	 * the new in the order indicated by `records` (not bothering
	 * with the header line):
	 */
	new_buffer = xmalloc(len);
	for (dst = new_buffer, i = 0; i < nr; i++) {
		memcpy(dst, records[i].start, records[i].len);
		dst += records[i].len;
	}

	/*
	 * Now munmap the old buffer and use the sorted buffer in its
	 * place:
	 */
	clear_snapshot_buffer(snapshot);
	snapshot->buf = snapshot->start = new_buffer;
	snapshot->eof = new_buffer + len;

cleanup:
	free(records);
}

/*
 * Return a pointer to the start of the record that contains the
 * character `*p` (which must be within the buffer). If no other
 * record start is found, return `buf`.
 */
static const char *find_start_of_record(const char *buf, const char *p)
{
	while (p > buf && (p[-1] != '\n' || p[0] == '^'))
		p--;
	return p;
}

/*
 * Return a pointer to the start of the record following the record
 * that contains `*p`. If none is found before `end`, return `end`.
 */
static const char *find_end_of_record(const char *p, const char *end)
{
	while (++p < end && (p[-1] != '\n' || p[0] == '^'))
		;
	return p;
}

/*
 * We want to be able to compare mmapped reference records quickly,
 * without totally parsing them. We can do so because the records are
 * LF-terminated, and the refname should start exactly (GIT_SHA1_HEXSZ
 * + 1) bytes past the beginning of the record.
 *
 * But what if the `packed-refs` file contains garbage? We're willing
 * to tolerate not detecting the problem, as long as we don't produce
 * totally garbled output (we can't afford to check the integrity of
 * the whole file during every Git invocation). But we do want to be
 * sure that we never read past the end of the buffer in memory and
 * perform an illegal memory access.
 *
 * Guarantee that minimum level of safety by verifying that the last
 * record in the file is LF-terminated, and that it has at least
 * (GIT_SHA1_HEXSZ + 1) characters before the LF. Die if either of
 * these checks fails.
 */
static void verify_buffer_safe(struct snapshot *snapshot)
{
	const char *start = snapshot->start;
	const char *eof = snapshot->eof;
	const char *last_line;

	if (start == eof)
		return;

	last_line = find_start_of_record(start, eof - 1);
	if (*(eof - 1) != '\n' || eof - last_line < the_hash_algo->hexsz + 2)
		die_invalid_line(snapshot->refs->path,
				 last_line, eof - last_line);
}

=======
>>>>>>> origin/jch
#define SMALL_FILE_SIZE (32*1024)

/*
 * Depending on `mmap_strategy`, either mmap or read the contents of
 * the `packed-refs` file into the snapshot. Return 1 if the file
 * existed and was read, or 0 if the file was absent or empty. Die on
 * errors.
 */
static int load_contents(struct snapshot *snapshot)
{
	int fd;
	struct stat st;
	ssize_t bytes_read;

	if (!packed_refs_enabled(snapshot->refs->store_flags))
		return 0;

	fd = open(snapshot->refs->path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			/*
			 * This is OK; it just means that no
			 * "packed-refs" file has been written yet,
			 * which is equivalent to it being empty,
			 * which is its state when initialized with
			 * zeros.
			 */
			return 0;
		} else {
			die_errno("couldn't read %s", snapshot->refs->path);
		}
	}

	stat_validity_update(&snapshot->validity, fd);

	if (fstat(fd, &st) < 0)
		die_errno("couldn't stat %s", snapshot->refs->path);
	snapshot->buflen = xsize_t(st.st_size);

	if (!snapshot->buflen) {
		close(fd);
		return 0;
	} else if (mmap_strategy == MMAP_NONE || snapshot->buflen <= SMALL_FILE_SIZE) {
		snapshot->buf = xmalloc(snapshot->buflen);
		bytes_read = read_in_full(fd, snapshot->buf, snapshot->buflen);
		if (bytes_read < 0 || bytes_read != snapshot->buflen)
			die_errno("couldn't read %s", snapshot->refs->path);
		snapshot->mmapped = 0;
	} else {
		snapshot->buf = xmmap(NULL, snapshot->buflen, PROT_READ, MAP_PRIVATE, fd, 0);
		snapshot->mmapped = 1;
	}
	close(fd);

	snapshot->start = snapshot->buf;
	snapshot->eof = snapshot->buf + snapshot->buflen;

	return 1;
}

<<<<<<< HEAD
static const char *find_reference_location_1(struct snapshot *snapshot,
					     const char *refname, int mustexist,
					     int start)
{
	/*
	 * This is not *quite* a garden-variety binary search, because
	 * the data we're searching is made up of records, and we
	 * always need to find the beginning of a record to do a
	 * comparison. A "record" here is one line for the reference
	 * itself and zero or one peel lines that start with '^'. Our
	 * loop invariant is described in the next two comments.
	 */

	/*
	 * A pointer to the character at the start of a record whose
	 * preceding records all have reference names that come
	 * *before* `refname`.
	 */
	const char *lo = snapshot->start;

	/*
	 * A pointer to a the first character of a record whose
	 * reference name comes *after* `refname`.
	 */
	const char *hi = snapshot->eof;

	while (lo != hi) {
		const char *mid, *rec;
		int cmp;

		mid = lo + (hi - lo) / 2;
		rec = find_start_of_record(lo, mid);
		cmp = cmp_record_to_refname(rec, refname, start);
		if (cmp < 0) {
			lo = find_end_of_record(mid, hi);
		} else if (cmp > 0) {
			hi = rec;
		} else {
			return rec;
		}
	}

	if (mustexist)
		return NULL;
	else
		return lo;
}

/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
static const char *find_reference_location(struct snapshot *snapshot,
					   const char *refname, int mustexist)
{
	return find_reference_location_1(snapshot, refname, mustexist, 1);
}

/*
 * Find the place in `snapshot->buf` after the end of the record for
 * `refname`. In other words, find the location of first thing *after*
 * `refname`.
 *
 * Other semantics are identical to the ones in
 * `find_reference_location()`.
 */
static const char *find_reference_location_end(struct snapshot *snapshot,
					       const char *refname,
					       int mustexist)
{
	return find_reference_location_1(snapshot, refname, mustexist, 0);
}

=======
>>>>>>> origin/jch
/*
 * Create a newly-allocated `snapshot` of the `packed-refs` file in
 * its current state and return it. The return value will already have
 * its reference count incremented.
 *
 * A comment line of the form "# pack-refs with: " may contain zero or
 * more traits. We interpret the traits as follows:
 *
 *   Neither `peeled` nor `fully-peeled`:
 *
 *      Probably no references are peeled. But if the file contains a
 *      peeled value for a reference, we will use it.
 *
 *   `peeled`:
 *
 *      References under "refs/tags/", if they *can* be peeled, *are*
 *      peeled in this file. References outside of "refs/tags/" are
 *      probably not peeled even if they could have been, but if we find
 *      a peeled value for such a reference we will use it.
 *
 *   `fully-peeled`:
 *
 *      All references in the file that can be peeled are peeled.
 *      Inversely (and this is more important), any references in the
 *      file for which no peeled value is recorded is not peelable. This
 *      trait should typically be written alongside "peeled" for
 *      compatibility with older clients, but we do not require it
 *      (i.e., "peeled" is a no-op if "fully-peeled" is set).
 *
 *   `sorted`:
 *
 *      The references in this file are known to be sorted by refname.
 */
static struct snapshot *create_snapshot(struct packed_ref_store *refs)
{
	struct snapshot *snapshot = xcalloc(1, sizeof(*snapshot));
	int sorted = 0;

	snapshot->refs = refs;
	acquire_snapshot(snapshot);
	snapshot->peeled = PEELED_NONE;
	snapshot->version = 1;

	if (!load_contents(snapshot))
		return snapshot;

	if ((refs->store_flags & REF_STORE_FORMAT_PACKED) &&
	    !detect_packed_format_v2_header(refs, snapshot)) {
		parse_packed_format_v1_header(refs, snapshot, &sorted);
		snapshot->version = 1;
		verify_buffer_safe_v1(snapshot);

		if (!sorted) {
			sort_snapshot_v1(snapshot);

			/*
			* Reordering the records might have moved a short one
			* to the end of the buffer, so verify the buffer's
			* safety again:
			*/
			verify_buffer_safe_v1(snapshot);
		}

		if (mmap_strategy != MMAP_OK && snapshot->mmapped) {
			/*
			* We don't want to leave the file mmapped, so we are
			* forced to make a copy now:
			*/
			char *buf_copy = xmalloc(snapshot->buflen);

<<<<<<< HEAD
		string_list_split_in_place(&traits, p, " ", -1);
=======
			memcpy(buf_copy, snapshot->start, snapshot->buflen);
			clear_snapshot_buffer(snapshot);
			snapshot->buf = snapshot->start = buf_copy;
			snapshot->eof = buf_copy + snapshot->buflen;
		}
>>>>>>> origin/jch

		return snapshot;
	}

	if (refs->store_flags & REF_STORE_FORMAT_PACKED_V2) {
		/*
		 * Assume we are in v2 format mode, now.
		 *
		 * fill_snapshot_v2() will die() if parsing fails.
		 */
		fill_snapshot_v2(snapshot);
		snapshot->version = 2;
	}

	return snapshot;
}

/*
 * Check that `refs->snapshot` (if present) still reflects the
 * contents of the `packed-refs` file. If not, clear the snapshot.
 */
static void validate_snapshot(struct packed_ref_store *refs)
{
	if (refs->snapshot &&
	    !stat_validity_check(&refs->snapshot->validity, refs->path))
		clear_snapshot(refs);
}

/*
 * Get the `snapshot` for the specified packed_ref_store, creating and
 * populating it if it hasn't been read before or if the file has been
 * changed (according to its `validity` field) since it was last read.
 * On the other hand, if we hold the lock, then assume that the file
 * hasn't been changed out from under us, so skip the extra `stat()`
 * call in `stat_validity_check()`. This function does *not* increase
 * the snapshot's reference count on behalf of the caller.
 */
static struct snapshot *get_snapshot(struct packed_ref_store *refs)
{
	if (!is_lock_file_locked(&refs->lock))
		validate_snapshot(refs);

	if (!refs->snapshot)
		refs->snapshot = create_snapshot(refs);

	return refs->snapshot;
}

static int packed_read_raw_ref(struct ref_store *ref_store, const char *refname,
			       struct object_id *oid, struct strbuf *referent UNUSED,
			       unsigned int *type, int *failure_errno)
{
	struct packed_ref_store *refs =
		packed_downcast(ref_store, REF_STORE_READ, "read_raw_ref");
	struct snapshot *snapshot = get_snapshot(refs);

	if (!snapshot) {
		/* refname is not a packed reference. */
		*failure_errno = ENOENT;
		return -1;
	}

	switch (snapshot->version) {
	case 1:
		return packed_read_raw_ref_v1(refs, snapshot, refname,
					      oid, type, failure_errno);

	case 2:
		return packed_read_raw_ref_v2(refs, snapshot, refname,
					      oid, type, failure_errno);

	default:
		return -1;
	}
}

/*
<<<<<<< HEAD
 * This value is set in `base.flags` if the peeled value of the
 * current reference is known. In that case, `peeled` contains the
 * correct peeled value for the reference, which might be `null_oid`
 * if the reference is not a tag or if it is broken.
 */
#define REF_KNOWS_PEELED 0x40

/*
 * An iterator over a snapshot of a `packed-refs` file.
 */
struct packed_ref_iterator {
	struct ref_iterator base;

	struct snapshot *snapshot;

	/* The current position in the snapshot's buffer: */
	const char *pos;

	/* The end of the part of the buffer that will be iterated over: */
	const char *eof;

	struct jump_list_entry {
		const char *start;
		const char *end;
	} *jump;
	size_t jump_nr, jump_alloc;
	size_t jump_cur;

	/* Scratch space for current values: */
	struct object_id oid, peeled;
	struct strbuf refname_buf;

	struct repository *repo;
	unsigned int flags;
};

/*
=======
>>>>>>> origin/jch
 * Move the iterator to the next record in the snapshot, without
 * respect for whether the record is actually required by the current
 * iteration. Adjust the fields in `iter` and return `ITER_OK` or
 * `ITER_DONE`. This function does not free the iterator in the case
 * of `ITER_DONE`.
 */
static int next_record(struct packed_ref_iterator *iter)
{
<<<<<<< HEAD
	const char *p, *eol;
=======
	switch (iter->version) {
	case 1:
		return next_record_v1(iter);
>>>>>>> origin/jch

	case 2:
		return next_record_v2(iter);

<<<<<<< HEAD
	/*
	 * If iter->pos is contained within a skipped region, jump past
	 * it.
	 *
	 * Note that each skipped region is considered at most once,
	 * since they are ordered based on their starting position.
	 */
	while (iter->jump_cur < iter->jump_nr) {
		struct jump_list_entry *curr = &iter->jump[iter->jump_cur];
		if (iter->pos < curr->start)
			break; /* not to the next jump yet */

		iter->jump_cur++;
		if (iter->pos < curr->end) {
			iter->pos = curr->end;
			trace2_counter_add(TRACE2_COUNTER_ID_PACKED_REFS_JUMPS, 1);
			/* jumps are coalesced, so only one jump is necessary */
			break;
		}
	}

	if (iter->pos == iter->eof)
		return ITER_DONE;

	iter->base.flags = REF_ISPACKED;
	p = iter->pos;

	if (iter->eof - p < the_hash_algo->hexsz + 2 ||
	    parse_oid_hex(p, &iter->oid, &p) ||
	    !isspace(*p++))
		die_invalid_line(iter->snapshot->refs->path,
				 iter->pos, iter->eof - iter->pos);

	eol = memchr(p, '\n', iter->eof - p);
	if (!eol)
		die_unterminated_line(iter->snapshot->refs->path,
				      iter->pos, iter->eof - iter->pos);

	strbuf_add(&iter->refname_buf, p, eol - p);
	iter->base.refname = iter->refname_buf.buf;

	if (check_refname_format(iter->base.refname, REFNAME_ALLOW_ONELEVEL)) {
		if (!refname_is_safe(iter->base.refname))
			die("packed refname is dangerous: %s",
			    iter->base.refname);
		oidclr(&iter->oid);
		iter->base.flags |= REF_BAD_NAME | REF_ISBROKEN;
=======
	default:
		return -1;
>>>>>>> origin/jch
	}
}

static int packed_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct packed_ref_iterator *iter =
		(struct packed_ref_iterator *)ref_iterator;
	int ok;

	while ((ok = next_record(iter)) == ITER_OK) {
		if (iter->flags & DO_FOR_EACH_PER_WORKTREE_ONLY &&
		    !is_per_worktree_ref(iter->base.refname))
			continue;

		if (!(iter->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
		    !ref_resolves_to_object(iter->base.refname, iter->repo,
					    &iter->oid, iter->flags))
			continue;

		return ITER_OK;
	}

	if (ref_iterator_abort(ref_iterator) != ITER_DONE)
		ok = ITER_ERROR;

	return ok;
}

static int packed_ref_iterator_peel(struct ref_iterator *ref_iterator,
				   struct object_id *peeled)
{
	struct packed_ref_iterator *iter =
		(struct packed_ref_iterator *)ref_iterator;

	if (iter->repo != the_repository)
		BUG("peeling for non-the_repository is not supported");

	if ((iter->base.flags & REF_KNOWS_PEELED)) {
		oidcpy(peeled, &iter->peeled);
		return is_null_oid(&iter->peeled) ? -1 : 0;
	} else if ((iter->base.flags & (REF_ISBROKEN | REF_ISSYMREF))) {
		return -1;
	} else {
		return peel_object(&iter->oid, peeled) ? -1 : 0;
	}
}

static int packed_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct packed_ref_iterator *iter =
		(struct packed_ref_iterator *)ref_iterator;
	int ok = ITER_DONE;

	strbuf_release(&iter->refname_buf);
	free(iter->jump);
	release_snapshot(iter->snapshot);
	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable packed_ref_iterator_vtable = {
	.advance = packed_ref_iterator_advance,
	.peel = packed_ref_iterator_peel,
	.abort = packed_ref_iterator_abort
};

static int jump_list_entry_cmp(const void *va, const void *vb)
{
	const struct jump_list_entry *a = va;
	const struct jump_list_entry *b = vb;

	if (a->start < b->start)
		return -1;
	if (a->start > b->start)
		return 1;
	return 0;
}

static int has_glob_special(const char *str)
{
	const char *p;
	for (p = str; *p; p++) {
		if (is_glob_special(*p))
			return 1;
	}
	return 0;
}

static void populate_excluded_jump_list(struct packed_ref_iterator *iter,
					struct snapshot *snapshot,
					const char **excluded_patterns)
{
	size_t i, j;
	const char **pattern;
	struct jump_list_entry *last_disjoint;

	if (!excluded_patterns)
		return;

	for (pattern = excluded_patterns; *pattern; pattern++) {
		struct jump_list_entry *e;
		const char *start, *end;

		/*
		 * We can't feed any excludes with globs in them to the
		 * refs machinery.  It only understands prefix matching.
		 * We likewise can't even feed the string leading up to
		 * the first meta-character, as something like "foo[a]"
		 * should not exclude "foobar" (but the prefix "foo"
		 * would match that and mark it for exclusion).
		 */
		if (has_glob_special(*pattern))
			continue;

		start = find_reference_location(snapshot, *pattern, 0);
		end = find_reference_location_end(snapshot, *pattern, 0);

		if (start == end)
			continue; /* nothing to jump over */

		ALLOC_GROW(iter->jump, iter->jump_nr + 1, iter->jump_alloc);

		e = &iter->jump[iter->jump_nr++];
		e->start = start;
		e->end = end;
	}

	if (!iter->jump_nr) {
		/*
		 * Every entry in exclude_patterns has a meta-character,
		 * nothing to do here.
		 */
		return;
	}

	QSORT(iter->jump, iter->jump_nr, jump_list_entry_cmp);

	/*
	 * As an optimization, merge adjacent entries in the jump list
	 * to jump forwards as far as possible when entering a skipped
	 * region.
	 *
	 * For example, if we have two skipped regions:
	 *
	 *	[[A, B], [B, C]]
	 *
	 * we want to combine that into a single entry jumping from A to
	 * C.
	 */
	last_disjoint = iter->jump;

	for (i = 1, j = 1; i < iter->jump_nr; i++) {
		struct jump_list_entry *ours = &iter->jump[i];
		if (ours->start <= last_disjoint->end) {
			/* overlapping regions extend the previous one */
			last_disjoint->end = last_disjoint->end > ours->end
				? last_disjoint->end : ours->end;
		} else {
			/* otherwise, insert a new region */
			iter->jump[j++] = *ours;
			last_disjoint = ours;
		}
	}

	iter->jump_nr = j;
	iter->jump_cur = 0;
}

static struct ref_iterator *packed_ref_iterator_begin(
		struct ref_store *ref_store,
		const char *prefix, const char **exclude_patterns,
		unsigned int flags)
{
	struct packed_ref_store *refs;
	struct snapshot *snapshot;
	const char *start;
	struct packed_ref_iterator *iter;
	struct ref_iterator *ref_iterator;
	unsigned int required_flags = REF_STORE_READ;
	size_t v2_row = 0;

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN))
		required_flags |= REF_STORE_ODB;
	refs = packed_downcast(ref_store, required_flags, "ref_iterator_begin");

	/*
	 * Note that `get_snapshot()` internally checks whether the
	 * snapshot is up to date with what is on disk, and re-reads
	 * it if not.
	 */
	snapshot = get_snapshot(refs);

	if (!snapshot || snapshot->version < 0 || snapshot->version > 2)
		return empty_ref_iterator_begin();

	if (prefix && *prefix) {
		if (snapshot->version == 1)
			start = find_reference_location_v1(snapshot, prefix, 0);
		else
			start = find_reference_location_v2(snapshot, prefix, 0,
							   &v2_row);
	} else {
		if (snapshot->version == 1)
			start = snapshot->start;
		else
			start = snapshot->refs_chunk;
	}

	if (start == snapshot->eof)
		return empty_ref_iterator_begin();

	CALLOC_ARRAY(iter, 1);
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &packed_ref_iterator_vtable, 1);

	if (exclude_patterns)
		populate_excluded_jump_list(iter, snapshot, exclude_patterns);

	iter->snapshot = snapshot;
	acquire_snapshot(snapshot);
	iter->version = snapshot->version;
	iter->row = v2_row;

	init_iterator_prefix_info(prefix, iter);

	iter->pos = start;
	iter->eof = snapshot->eof;
	strbuf_init(&iter->refname_buf, 0);

	iter->base.oid = &iter->oid;

	iter->repo = ref_store->repo;
	iter->flags = flags;

	if (prefix && *prefix)
		/* Stop iteration after we've gone *past* prefix: */
		ref_iterator = prefix_ref_iterator_begin(ref_iterator, prefix, 0);

	return ref_iterator;
}

int packed_refs_lock(struct ref_store *ref_store, int flags, struct strbuf *err)
{
	struct packed_ref_store *refs =
		packed_downcast(ref_store, REF_STORE_WRITE | REF_STORE_MAIN,
				"packed_refs_lock");
	static int timeout_configured = 0;
	static int timeout_value = 1000;

	if (!timeout_configured) {
		git_config_get_int("core.packedrefstimeout", &timeout_value);
		timeout_configured = 1;
	}

	/*
	 * Note that we close the lockfile immediately because we
	 * don't write new content to it, but rather to a separate
	 * tempfile.
	 */
	if (hold_lock_file_for_update_timeout(
			    &refs->lock,
			    refs->path,
			    flags, timeout_value) < 0) {
		unable_to_lock_message(refs->path, errno, err);
		return -1;
	}

	if (close_lock_file_gently(&refs->lock)) {
		strbuf_addf(err, "unable to close %s: %s", refs->path, strerror(errno));
		rollback_lock_file(&refs->lock);
		return -1;
	}

	/*
	 * There is a stat-validity problem might cause `update-ref -d`
	 * lost the newly commit of a ref, because a new `packed-refs`
	 * file might has the same on-disk file attributes such as
	 * timestamp, file size and inode value, but has a changed
	 * ref value.
	 *
	 * This could happen with a very small chance when
	 * `update-ref -d` is called and at the same time another
	 * `pack-refs --all` process is running.
	 *
	 * Now that we hold the `packed-refs` lock, it is important
	 * to make sure we could read the latest version of
	 * `packed-refs` file no matter we have just mmap it or not.
	 * So what need to do is clear the snapshot if we hold it
	 * already.
	 */
	clear_snapshot(refs);

	/*
	 * Now make sure that the packed-refs file as it exists in the
	 * locked state is loaded into the snapshot:
	 */
	get_snapshot(refs);
	return 0;
}

void packed_refs_unlock(struct ref_store *ref_store)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE,
			"packed_refs_unlock");

	if (!is_lock_file_locked(&refs->lock))
		BUG("packed_refs_unlock() called when not locked");
	rollback_lock_file(&refs->lock);
}

int packed_refs_is_locked(struct ref_store *ref_store)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE,
			"packed_refs_is_locked");

	return is_lock_file_locked(&refs->lock);
}

static int packed_init_db(struct ref_store *ref_store UNUSED,
			  struct strbuf *err UNUSED)
{
	/* Nothing to do. */
	return 0;
}

static void add_write_error(struct packed_ref_store *refs, struct strbuf *err)
{
	strbuf_addf(err, "error writing to %s: %s",
		    get_tempfile_path(refs->tempfile), strerror(errno));
}

int merge_iterator_and_updates(struct packed_ref_store *refs,
			       struct string_list *updates,
			       struct strbuf *err,
			       write_ref_fn write_fn,
			       void *write_data)
{
	struct ref_iterator *iter = NULL;
	int ok, i;

	/*
	 * We iterate in parallel through the current list of refs and
	 * the list of updates, processing an entry from at least one
	 * of the lists each time through the loop. When the current
	 * list of refs is exhausted, set iter to NULL. When the list
	 * of updates is exhausted, leave i set to updates->nr.
	 */
	iter = packed_ref_iterator_begin(&refs->base, "", NULL,
					 DO_FOR_EACH_INCLUDE_BROKEN);
	if ((ok = ref_iterator_advance(iter)) != ITER_OK)
		iter = NULL;

	i = 0;

	while (iter || i < updates->nr) {
		struct ref_update *update = NULL;
		int cmp;

		if (i >= updates->nr) {
			cmp = -1;
		} else {
			update = updates->items[i].util;

			if (!iter)
				cmp = +1;
			else
				cmp = strcmp(iter->refname, update->refname);
		}

		if (!cmp) {
			/*
			 * There is both an old value and an update
			 * for this reference. Check the old value if
			 * necessary:
			 */
			if ((update->flags & REF_HAVE_OLD)) {
				if (is_null_oid(&update->old_oid)) {
					strbuf_addf(err, "cannot update ref '%s': "
						    "reference already exists",
						    update->refname);
					goto error;
				} else if (!oideq(&update->old_oid, iter->oid)) {
					strbuf_addf(err, "cannot update ref '%s': "
						    "is at %s but expected %s",
						    update->refname,
						    oid_to_hex(iter->oid),
						    oid_to_hex(&update->old_oid));
					goto error;
				}
			}

			/* Now figure out what to use for the new value: */
			if ((update->flags & REF_HAVE_NEW)) {
				/*
				 * The update takes precedence. Skip
				 * the iterator over the unneeded
				 * value.
				 */
				if ((ok = ref_iterator_advance(iter)) != ITER_OK)
					iter = NULL;
				cmp = +1;
			} else {
				/*
				 * The update doesn't actually want to
				 * change anything. We're done with it.
				 */
				i++;
				cmp = -1;
			}
		} else if (cmp > 0) {
			/*
			 * There is no old value but there is an
			 * update for this reference. Make sure that
			 * the update didn't expect an existing value:
			 */
			if ((update->flags & REF_HAVE_OLD) &&
			    !is_null_oid(&update->old_oid)) {
				strbuf_addf(err, "cannot update ref '%s': "
					    "reference is missing but expected %s",
					    update->refname,
					    oid_to_hex(&update->old_oid));
				goto error;
			}
		}

		if (cmp < 0) {
			/* Pass the old reference through. */

			struct object_id peeled;
			int peel_error = ref_iterator_peel(iter, &peeled);

			if (write_fn(iter->refname,
				     iter->oid,
				     peel_error ? NULL : &peeled,
				     write_data)) {
				add_write_error(refs, err);
				goto error;
			}

			if ((ok = ref_iterator_advance(iter)) != ITER_OK)
				iter = NULL;
		} else if (is_null_oid(&update->new_oid)) {
			/*
			 * The update wants to delete the reference,
			 * and the reference either didn't exist or we
			 * have already skipped it. So we're done with
			 * the update (and don't have to write
			 * anything).
			 */
			i++;
		} else {
			struct object_id peeled;
			int peel_error = peel_object(&update->new_oid,
						     &peeled);

			if (write_fn(update->refname,
				     &update->new_oid,
				     peel_error ? NULL : &peeled,
				     write_data)) {
				add_write_error(refs, err);
				goto error;
			}

			i++;
		}
	}

error:
	if (iter)
		ref_iterator_abort(iter);
	return ok;
}

static int write_with_updates_v1(struct packed_ref_store *refs,
				 struct string_list *updates,
				 struct strbuf *err)
{
	FILE *out;

	out = fdopen_tempfile(refs->tempfile, "w");
	if (!out) {
		strbuf_addf(err, "unable to fdopen packed-refs tempfile: %s",
			    strerror(errno));
		goto error;
	}

	if (write_packed_file_header_v1(out) < 0) {
		add_write_error(refs, err);
		goto error;
	}

	return merge_iterator_and_updates(refs, updates, err,
					  write_packed_entry_v1, out);

error:
	return -1;
}

static int write_with_updates_v2(struct packed_ref_store *refs,
				 struct string_list *updates,
				 struct strbuf *err)
{
	struct write_packed_refs_v2_context *ctx = create_v2_context(refs, updates, err);
	int ok = -1;

	if ((ok = write_packed_refs_v2(ctx)) < 0)
		add_write_error(refs, err);

	free_v2_context(ctx);
	return ok;
}

/*
 * Write the packed refs from the current snapshot to the packed-refs
 * tempfile, incorporating any changes from `updates`. `updates` must
 * be a sorted string list whose keys are the refnames and whose util
 * values are `struct ref_update *`. On error, rollback the tempfile,
 * write an error message to `err`, and return a nonzero value.
 *
 * The packfile must be locked before calling this function and will
 * remain locked when it is done.
 */
static int write_with_updates(struct packed_ref_store *refs,
			      struct string_list *updates,
			      struct strbuf *err)
{
	int ok;
	struct strbuf sb = STRBUF_INIT;
	char *packed_refs_path;
	int version;

	if (!is_lock_file_locked(&refs->lock))
		BUG("write_with_updates() called while unlocked");

	/*
	 * If packed-refs is a symlink, we want to overwrite the
	 * symlinked-to file, not the symlink itself. Also, put the
	 * staging file next to it:
	 */
	packed_refs_path = get_locked_file_path(&refs->lock);
	strbuf_addf(&sb, "%s.new", packed_refs_path);
	free(packed_refs_path);
	refs->tempfile = create_tempfile(sb.buf);
	if (!refs->tempfile) {
		strbuf_addf(err, "unable to create file %s: %s",
			    sb.buf, strerror(errno));
		strbuf_release(&sb);
		return -1;
	}
	strbuf_release(&sb);

	if (!(version = git_env_ulong("GIT_TEST_PACKED_REFS_VERSION", 0)) &&
	    git_config_get_int("refs.packedrefsversion", &version)) {
		/*
		 * Set the default depending on the current extension
		 * list. Default to version 1 if available, but allow a
		 * default of 2 if only "packed-v2" exists.
		 */
		if (refs->store_flags & REF_STORE_FORMAT_PACKED)
			version = 1;
		else if (refs->store_flags & REF_STORE_FORMAT_PACKED_V2)
			version = 2;
		else
			BUG("writing a packed-refs file without an extension");
	}

	switch (version) {
	case 1:
		ok = write_with_updates_v1(refs, updates, err);
		break;

	case 2:
		/* Convert the normal error codes to ITER_DONE. */
		ok = write_with_updates_v2(refs, updates, err) ? -2 : ITER_DONE;
		break;

	default:
		strbuf_addf(err, "unknown packed-refs version: %d",
			    version);
		goto error;
	}

	if (ok != ITER_DONE) {
		strbuf_addstr(err, "unable to write packed-refs file: "
			      "error iterating over old contents");
		goto error;
	}

	if (fflush(out) ||
	    fsync_component(FSYNC_COMPONENT_REFERENCE, get_tempfile_fd(refs->tempfile)) ||
	    close_tempfile_gently(refs->tempfile)) {
		strbuf_addf(err, "error closing file %s: %s",
			    get_tempfile_path(refs->tempfile),
			    strerror(errno));
		strbuf_release(&sb);
		delete_tempfile(&refs->tempfile);
		return -1;
	}

	return 0;

error:
	delete_tempfile(&refs->tempfile);
	return -1;
}

int is_packed_transaction_needed(struct ref_store *ref_store,
				 struct ref_transaction *transaction)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ,
			"is_packed_transaction_needed");
	struct strbuf referent = STRBUF_INIT;
	size_t i;
	int ret;

	if (!is_lock_file_locked(&refs->lock))
		BUG("is_packed_transaction_needed() called while unlocked");

	/*
	 * We're only going to bother returning false for the common,
	 * trivial case that references are only being deleted, their
	 * old values are not being checked, and the old `packed-refs`
	 * file doesn't contain any of those reference(s). This gives
	 * false positives for some other cases that could
	 * theoretically be optimized away:
	 *
	 * 1. It could be that the old value is being verified without
	 *    setting a new value. In this case, we could verify the
	 *    old value here and skip the update if it agrees. If it
	 *    disagrees, we could either let the update go through
	 *    (the actual commit would re-detect and report the
	 *    problem), or come up with a way of reporting such an
	 *    error to *our* caller.
	 *
	 * 2. It could be that a new value is being set, but that it
	 *    is identical to the current packed value of the
	 *    reference.
	 *
	 * Neither of these cases will come up in the current code,
	 * because the only caller of this function passes to it a
	 * transaction that only includes `delete` updates with no
	 * `old_id`. Even if that ever changes, false positives only
	 * cause an optimization to be missed; they do not affect
	 * correctness.
	 */

	/*
	 * Start with the cheap checks that don't require old
	 * reference values to be read:
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if (update->flags & REF_HAVE_OLD)
			/* Have to check the old value -> needed. */
			return 1;

		if ((update->flags & REF_HAVE_NEW) && !is_null_oid(&update->new_oid))
			/* Have to set a new value -> needed. */
			return 1;
	}

	/*
	 * The transaction isn't checking any old values nor is it
	 * setting any nonzero new values, so it still might be able
	 * to be skipped. Now do the more expensive check: the update
	 * is needed if any of the updates is a delete, and the old
	 * `packed-refs` file contains a value for that reference.
	 */
	ret = 0;
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		int failure_errno;
		unsigned int type;
		struct object_id oid;

		if (!(update->flags & REF_HAVE_NEW))
			/*
			 * This reference isn't being deleted -> not
			 * needed.
			 */
			continue;

		if (!refs_read_raw_ref(ref_store, update->refname, &oid,
				       &referent, &type, &failure_errno) ||
		    failure_errno != ENOENT) {
			/*
			 * We have to actually delete that reference
			 * -> this transaction is needed.
			 */
			ret = 1;
			break;
		}
	}

	strbuf_release(&referent);
	return ret;
}

struct packed_transaction_backend_data {
	/* True iff the transaction owns the packed-refs lock. */
	int own_lock;

	struct string_list updates;
};

static void packed_transaction_cleanup(struct packed_ref_store *refs,
				       struct ref_transaction *transaction)
{
	struct packed_transaction_backend_data *data = transaction->backend_data;

	if (data) {
		string_list_clear(&data->updates, 0);

		if (is_tempfile_active(refs->tempfile))
			delete_tempfile(&refs->tempfile);

		if (data->own_lock && is_lock_file_locked(&refs->lock)) {
			packed_refs_unlock(&refs->base);
			data->own_lock = 0;
		}

		free(data);
		transaction->backend_data = NULL;
	}

	transaction->state = REF_TRANSACTION_CLOSED;
}

static int packed_transaction_prepare(struct ref_store *ref_store,
				      struct ref_transaction *transaction,
				      struct strbuf *err)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_prepare");
	struct packed_transaction_backend_data *data;
	size_t i;
	int ret = TRANSACTION_GENERIC_ERROR;

	/*
	 * Note that we *don't* skip transactions with zero updates,
	 * because such a transaction might be executed for the side
	 * effect of ensuring that all of the references are peeled or
	 * ensuring that the `packed-refs` file is sorted. If the
	 * caller wants to optimize away empty transactions, it should
	 * do so itself.
	 */

	CALLOC_ARRAY(data, 1);
	string_list_init_nodup(&data->updates);

	transaction->backend_data = data;

	/*
	 * Stick the updates in a string list by refname so that we
	 * can sort them:
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct string_list_item *item =
			string_list_append(&data->updates, update->refname);

		/* Store a pointer to update in item->util: */
		item->util = update;
	}
	string_list_sort(&data->updates);

	if (ref_update_reject_duplicates(&data->updates, err))
		goto failure;

	if (!is_lock_file_locked(&refs->lock)) {
		if (packed_refs_lock(ref_store, 0, err))
			goto failure;
		data->own_lock = 1;
	}

	if (write_with_updates(refs, &data->updates, err))
		goto failure;

	transaction->state = REF_TRANSACTION_PREPARED;
	return 0;

failure:
	packed_transaction_cleanup(refs, transaction);
	return ret;
}

static int packed_transaction_abort(struct ref_store *ref_store,
				    struct ref_transaction *transaction,
				    struct strbuf *err UNUSED)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_abort");

	packed_transaction_cleanup(refs, transaction);
	return 0;
}

static int packed_transaction_finish(struct ref_store *ref_store,
				     struct ref_transaction *transaction,
				     struct strbuf *err)
{
	struct packed_ref_store *refs = packed_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_finish");
	int ret = TRANSACTION_GENERIC_ERROR;
	char *packed_refs_path;

	clear_snapshot(refs);

	packed_refs_path = get_locked_file_path(&refs->lock);
	if (rename_tempfile(&refs->tempfile, packed_refs_path)) {
		strbuf_addf(err, "error replacing %s: %s",
			    refs->path, strerror(errno));
		goto cleanup;
	}

	ret = 0;

cleanup:
	free(packed_refs_path);
	packed_transaction_cleanup(refs, transaction);
	return ret;
}

static int packed_initial_transaction_commit(struct ref_store *ref_store UNUSED,
					    struct ref_transaction *transaction,
					    struct strbuf *err)
{
	return ref_transaction_commit(transaction, err);
}

static int packed_pack_refs(struct ref_store *ref_store UNUSED,
			    struct pack_refs_opts *pack_opts UNUSED)
{
	/*
	 * Packed refs are already packed. It might be that loose refs
	 * are packed *into* a packed refs store, but that is done by
	 * updating the packed references via a transaction.
	 */
	return 0;
}

static struct ref_iterator *packed_reflog_iterator_begin(struct ref_store *ref_store UNUSED)
{
	return empty_ref_iterator_begin();
}

struct ref_storage_be refs_be_packed = {
	.next = NULL,
	.name = "packed",
	.init = packed_ref_store_create,
	.init_db = packed_init_db,
	.transaction_prepare = packed_transaction_prepare,
	.transaction_finish = packed_transaction_finish,
	.transaction_abort = packed_transaction_abort,
	.initial_transaction_commit = packed_initial_transaction_commit,

	.pack_refs = packed_pack_refs,
	.create_symref = NULL,
	.rename_ref = NULL,
	.copy_ref = NULL,

	.iterator_begin = packed_ref_iterator_begin,
	.read_raw_ref = packed_read_raw_ref,
	.read_symbolic_ref = NULL,

	.reflog_iterator_begin = packed_reflog_iterator_begin,
	.for_each_reflog_ent = NULL,
	.for_each_reflog_ent_reverse = NULL,
	.reflog_exists = NULL,
	.create_reflog = NULL,
	.delete_reflog = NULL,
	.reflog_expire = NULL,
};
