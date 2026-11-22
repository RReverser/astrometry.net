/*
 # This file is part of the Astrometry.net suite.
 # Licensed under a 3-clause BSD style license - see LICENSE
 */

/*
 C versions of removelines.py and uniformize.py.

 The Python programs read the table with fits_table(), select or reorder
 rows, and write the table back.  Here the rows are copied as bytes, so
 every column keeps its type and value.

 The X and Y columns are 32-bit floats, and numpy computes with them in a
 mix of 32-bit and 64-bit arithmetic that depends on whether an operand is
 an array, a numpy scalar or a Python number.  The comments below give the
 type of each intermediate value under numpy 1.x (checked against numpy
 1.26).  numpy 2 keeps more of the scalar arithmetic in 32 bits; the places
 where that could change a result are marked.

 Every 32-bit intermediate value is stored in a "volatile float", so that
 the compiler can neither keep extra precision nor contract a multiply and
 an add into one operation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/types.h>

#include "os-features.h"
#include "xylist-filter.h"
#include "anqfits.h"
#include "qfits_header.h"
#include "qfits_table.h"
#include "fitsioutils.h"
#include "ioutils.h"
#include "errors.h"
#include "log.h"

struct xytable {
    // byte offsets in the input file
    off_t ext_header_start;
    off_t data_start;
    int rowsize;
    int nrows;
    // all rows, as stored (big-endian)
    unsigned char* rows;
    // X and Y of each row
    float* x;
    float* y;
    qfits_header* ext_header;
};
typedef struct xytable xytable_t;

static void xytable_free(xytable_t* t) {
    free(t->rows);
    free(t->x);
    free(t->y);
    if (t->ext_header)
        qfits_header_destroy(t->ext_header);
    memset(t, 0, sizeof(xytable_t));
}

static float read_be_float(const unsigned char* p) {
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    float f;
    memcpy(&f, &u, sizeof(float));
    return f;
}

// np.isfinite().  This tests the bits rather than calling isfinite(),
// because the default build flags include -ffinite-math-only, which lets
// the compiler assume that isfinite() is always true.
static anbool float_is_finite(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(float));
    return (u & 0x7f800000U) != 0x7f800000U;
}

// Finds a 32-bit float column and returns its "off_beg": the byte offset of
// the column's first value from the start of the file.  A column of another
// type would be read by numpy with other arithmetic than this file
// reproduces, so it is refused.
static int float_column_offset(const qfits_table* table, const char* name, const char* fn) {
    int c = fits_find_column(table, name);
    if (c == -1) {
        ERROR("Column \"%s\" not found in the first table of \"%s\"", name, fn);
        return -1;
    }
    if (table->col[c].atom_type != TFITS_BIN_TYPE_E || table->col[c].atom_nb != 1) {
        ERROR("Column \"%s\" of \"%s\" is not a scalar 32-bit float column; "
              "only that type is supported", name, fn);
        return -1;
    }
    return table->col[c].off_beg;
}

static int xytable_read(const char* fn, const char* xcol, const char* ycol, xytable_t* t) {
    anqfits_t* anq;
    const qfits_table* table;
    int xoff, yoff, i;
    FILE* fid;
    size_t nbytes;

    memset(t, 0, sizeof(xytable_t));
    anq = anqfits_open(fn);
    if (!anq) {
        ERROR("Failed to open FITS file \"%s\"", fn);
        return -1;
    }
    table = anqfits_get_table_const(anq, 1);
    if (!table) {
        ERROR("Extension 1 of \"%s\" is not a table", fn);
        anqfits_close(anq);
        return -1;
    }
    xoff = float_column_offset(table, xcol, fn);
    yoff = float_column_offset(table, ycol, fn);
    if (xoff == -1 || yoff == -1) {
        anqfits_close(anq);
        return -1;
    }
    t->ext_header_start = anqfits_header_start(anq, 1);
    t->data_start = anqfits_data_start(anq, 1);
    t->rowsize = table->tab_w;
    t->nrows = table->nr;
    t->ext_header = anqfits_get_header(anq, 1);
    anqfits_close(anq);
    if (!t->ext_header) {
        ERROR("Failed to read the header of extension 1 of \"%s\"", fn);
        return -1;
    }
    // "off_beg" counts from the start of the file, not of the row.
    xoff -= (int)t->data_start;
    yoff -= (int)t->data_start;
    if (xoff < 0 || yoff < 0 ||
        xoff + (int)sizeof(float) > t->rowsize || yoff + (int)sizeof(float) > t->rowsize) {
        ERROR("Column offsets %i and %i are outside the %i-byte rows of \"%s\"",
              xoff, yoff, t->rowsize, fn);
        xytable_free(t);
        return -1;
    }

    nbytes = (size_t)t->rowsize * (size_t)t->nrows;
    // one spare element each, so that an empty table does not ask for zero bytes.
    t->rows = malloc(nbytes + 1);
    t->x = malloc(((size_t)t->nrows + 1) * sizeof(float));
    t->y = malloc(((size_t)t->nrows + 1) * sizeof(float));
    if (!t->rows || !t->x || !t->y) {
        SYSERROR("Failed to allocate memory for %i rows of \"%s\"", t->nrows, fn);
        xytable_free(t);
        return -1;
    }
    fid = fopen(fn, "rb");
    if (!fid) {
        SYSERROR("Failed to open \"%s\"", fn);
        xytable_free(t);
        return -1;
    }
    if (fseeko(fid, t->data_start, SEEK_SET) || fread(t->rows, 1, nbytes, fid) != nbytes) {
        SYSERROR("Failed to read %i rows from \"%s\"", t->nrows, fn);
        fclose(fid);
        xytable_free(t);
        return -1;
    }
    fclose(fid);
    for (i=0; i<t->nrows; i++) {
        t->x[i] = read_be_float(t->rows + (size_t)i * t->rowsize + xoff);
        t->y[i] = read_be_float(t->rows + (size_t)i * t->rowsize + yoff);
    }
    return 0;
}

// Writes "outfn": everything of "infn" ahead of the table extension, then
// the table with rows order[0..n-1] of the input.
static int xytable_write(const xytable_t* t, const char* infn, const char* outfn,
                         const int* order, int n) {
    FILE* fin = NULL;
    FILE* fout = NULL;
    int i;

    fin = fopen(infn, "rb");
    if (!fin) {
        SYSERROR("Failed to open \"%s\"", infn);
        return -1;
    }
    fout = fopen(outfn, "wb");
    if (!fout) {
        SYSERROR("Failed to open \"%s\" for writing", outfn);
        fclose(fin);
        return -1;
    }
    if (pipe_file_offset(fin, 0, t->ext_header_start, fout)) {
        ERROR("Failed to copy the primary HDU of \"%s\" to \"%s\"", infn, outfn);
        goto bailout;
    }
    fits_header_mod_int(t->ext_header, "NAXIS2", n, "number of rows in table");
    if (qfits_header_dump(t->ext_header, fout)) {
        ERROR("Failed to write the table header to \"%s\"", outfn);
        goto bailout;
    }
    for (i=0; i<n; i++) {
        if (fwrite(t->rows + (size_t)order[i] * t->rowsize, 1, t->rowsize, fout) != (size_t)t->rowsize) {
            SYSERROR("Failed to write row %i to \"%s\"", i, outfn);
            goto bailout;
        }
    }
    if (fits_pad_file(fout)) {
        ERROR("Failed to pad \"%s\"", outfn);
        goto bailout;
    }
    fclose(fin);
    if (fclose(fout)) {
        SYSERROR("Failed to close \"%s\"", outfn);
        return -1;
    }
    return 0;
 bailout:
    fclose(fin);
    fclose(fout);
    return -1;
}

/*
 hist_remove_lines(x, binwidth=1, binoffset=0.5, logcut)

 Sets keep[i] = 0 for every source in a bin whose count is improbably high.

   bins = -binoffset + np.arange(0, max(x)+binwidth+1, binwidth)
   (counts, thebins) = np.histogram(x, bins)

 max(x) is a float32 scalar; adding the Python integers promotes it to
 float64 in numpy 1.x, so the upper limit is exact.  np.arange(0, stop, 1)
 has ceil(stop) elements, the bin edges are k - 0.5, and np.histogram
 compares the float32 values with the float64 edges exactly.  Bin k
 therefore holds the sources with k - 0.5 <= x < k + 0.5, that is
 k = floor(x + 0.5) in double arithmetic.  (numpy 2 adds in float32: the
 limit can differ by one rounding, which changes only the number of empty
 bins at the end.)

 The last bin of np.histogram also includes its right edge.  No source can
 be there, because the last edge is at least max(x) + 0.5.
 */
static int hist_remove_lines(const float* x, int n, double logcut, unsigned char* keep) {
    double maxx, stop, mean, logmean;
    long nedges, nbins, k;
    long* counts;
    long noccupied = 0;
    double sumk = 0.0;
    int i;

    maxx = x[0];
    for (i=1; i<n; i++)
        if (x[i] > maxx)
            maxx = x[i];
    stop = maxx + 1.0 + 1.0;
    nedges = (long)ceil(stop);
    nbins = nedges - 1;
    if (nbins < 1)
        // np.histogram needs at least one bin; no bin is bad.
        return 0;
    counts = calloc((size_t)nbins, sizeof(long));
    if (!counts) {
        SYSERROR("Failed to allocate %li histogram bins", nbins);
        return -1;
    }
    for (i=0; i<n; i++) {
        double b = floor((double)x[i] + 0.5);
        // values below the first edge (-0.5) are outside every bin.
        if (b >= 0 && b < (double)nbins)
            counts[(long)b]++;
    }

    //   occupied = np.nonzero(counts > 1)[0]
    //   k = (counts[occupied] - 1)
    //   mean = sum(k) / float(noccupied)
    // Integer sums, then one float64 division.
    for (k=0; k<nbins; k++) {
        if (counts[k] > 1) {
            noccupied++;
            sumk += (double)(counts[k] - 1);
        }
    }
    if (noccupied == 0) {
        free(counts);
        return 0;
    }
    mean = sumk / (double)noccupied;
    logmean = log(mean);

    //   logpoisson = k*np.log(mean) - mean - np.array([sum(np.arange(kk)) for kk in k])
    //   badbins = occupied[logpoisson < logcut]
    // All float64.  sum(np.arange(kk)) is 0 + 1 + ... + (kk-1) = kk (kk-1) / 2,
    // an exact integer.  The expression is evaluated left to right.
    for (i=0; i<n; i++) {
        double b = floor((double)x[i] + 0.5);
        long c, kk;
        volatile double logpoisson;
        if (!(b >= 0 && b < (double)nbins))
            continue;
        c = counts[(long)b];
        if (c <= 1)
            continue;
        kk = c - 1;
        logpoisson = (double)kk * logmean;
        logpoisson = logpoisson - mean;
        logpoisson = logpoisson - (double)(kk * (kk - 1) / 2);
        //   badpoints = sum((x >= L)*(x < R) for (L,R) in zip(badleft, badright))
        // A source is bad when it lies in a bad bin, with the same edges.
        if (logpoisson < logcut)
            keep[i] = 0;
    }
    free(counts);
    return 0;
}

int removelines_xylist(const char* infn, const char* outfn,
                       const char* xcol, const char* ycol, double cut) {
    xytable_t t;
    unsigned char* keep = NULL;
    int* order = NULL;
    int i, nkeep = 0;
    int rtn = -1;

    if (xytable_read(infn, xcol, ycol, &t))
        return -1;
    order = malloc(((size_t)t.nrows + 1) * sizeof(int));
    keep = malloc((size_t)t.nrows + 1);
    if (!order || !keep) {
        SYSERROR("Failed to allocate memory for %i rows", t.nrows);
        goto done;
    }
    memset(keep, 1, (size_t)t.nrows + 1);
    if (t.nrows == 0) {
        logmsg("removelines: Input file contains no sources.\n");
    } else {
        //   ix = hist_remove_lines(T.get(xcol), 1, 0.5, logcut=-cut)
        //   iy = hist_remove_lines(T.get(ycol), 1, 0.5, logcut=-cut)
        //   T.cut(ix * iy)
        if (hist_remove_lines(t.x, t.nrows, -cut, keep) ||
            hist_remove_lines(t.y, t.nrows, -cut, keep))
            goto done;
    }
    for (i=0; i<t.nrows; i++)
        if (keep[i])
            order[nkeep++] = i;
    logverb("removelines: Removed %i sources\n", t.nrows - nkeep);
    rtn = xytable_write(&t, infn, outfn, order, nkeep);
 done:
    free(order);
    free(keep);
    xytable_free(&t);
    return rtn;
}

static int compare_ints(const void* v1, const void* v2) {
    int i1 = *(const int*)v1;
    int i2 = *(const int*)v2;
    return (i1 > i2) - (i1 < i2);
}

int uniformize_xylist(const char* infn, const char* outfn, int n,
                      const char* xcol, const char* ycol) {
    xytable_t t;
    int* order = NULL;
    int* finite = NULL;
    int* cell = NULL;
    int* cellcount = NULL;
    int* cellstart = NULL;
    int* cellfill = NULL;
    int* members = NULL;
    int* thisrow = NULL;
    int nf = 0, nout = 0;
    int i, pass, c;
    int NX, NY, ncells, maxlen = 0;
    volatile float minx, maxx, miny, maxy, W, H, WH;
    volatile double cellsize, nxd, nyd;
    int rtn = -1;

    if (xytable_read(infn, xcol, ycol, &t))
        return -1;
    order = malloc(((size_t)t.nrows + 1) * sizeof(int));
    finite = malloc(((size_t)t.nrows + 1) * sizeof(int));
    cell = malloc(((size_t)t.nrows + 1) * sizeof(int));
    members = malloc(((size_t)t.nrows + 1) * sizeof(int));
    if (!order || !finite || !cell || !members) {
        SYSERROR("Failed to allocate memory for %i rows", t.nrows);
        goto done;
    }
    if (t.nrows == 0) {
        logmsg("uniformize: No sources\n");
        rtn = xytable_write(&t, infn, outfn, order, 0);
        goto done;
    }

    //   I = np.logical_and(np.isfinite(x), np.isfinite(y))
    //   if not all(I): x = x[I]; y = y[I]; T.cut(I)
    for (i=0; i<t.nrows; i++)
        if (float_is_finite(t.x[i]) && float_is_finite(t.y[i]))
            finite[nf++] = i;
    if (nf != t.nrows)
        logmsg("uniformize: %i source positions are not finite.\n", t.nrows - nf);
    if (nf == 0) {
        // max(x) of an empty array raises an exception in Python.
        ERROR("uniformize: no source of \"%s\" has a finite position", infn);
        goto done;
    }

    //   W = max(x) - min(x)
    //   H = max(y) - min(y)
    // float32 scalars: the differences are rounded to float32.
    minx = maxx = t.x[finite[0]];
    miny = maxy = t.y[finite[0]];
    for (i=1; i<nf; i++) {
        float x = t.x[finite[i]];
        float y = t.y[finite[i]];
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    W = maxx - minx;
    H = maxy - miny;
    if (W == 0 || H == 0) {
        logmsg("uniformize: the sources span a rectangle with no area; not reordering.\n");
        rtn = xytable_write(&t, infn, outfn, finite, nf);
        goto done;
    }

    //   NX = int(max(1, np.round(W / np.sqrt(W*H / float(n)))))
    //   NY = int(max(1, np.round(n / float(NX))))
    // W*H is a float32 product.  Dividing it by the Python float promotes
    // to float64 in numpy 1.x, and the square root, the division of W by
    // it and np.round (round half to even, which is rint) are float64.
    // (numpy 2 stays in float32 up to np.round; NX can differ only when
    // the quotient is within float32 rounding of a half-integer.)
    WH = W * H;
    cellsize = sqrt((double)WH / (double)n);
    nxd = rint((double)W / cellsize);
    NX = (int)(nxd > 1.0 ? nxd : 1.0);
    nyd = rint((double)n / (double)NX);
    NY = (int)(nyd > 1.0 ? nyd : 1.0);
    ncells = NX * NY;
    logverb("uniformize: Uniformizing into %i x %i bins\n", NX, NY);

    //   ix = (np.clip(np.floor((x - min(x)) / float(W) * NX), 0, NX-1)).astype(int)
    //   iy = (np.clip(np.floor((y - min(y)) / float(H) * NY), 0, NY-1)).astype(int)
    //   I = iy * NX + ix
    // x is a float32 array, and an array combined with a scalar keeps the
    // array's type: the subtraction, the division and the multiplication
    // are three float32 operations, each rounded.
    for (i=0; i<nf; i++) {
        volatile float fx, fy;
        int ix, iy;
        fx = t.x[finite[i]] - minx;
        fx = fx / W;
        fx = fx * (float)NX;
        fx = floorf(fx);
        fy = t.y[finite[i]] - miny;
        fy = fy / H;
        fy = fy * (float)NY;
        fy = floorf(fy);
        ix = (fx < 0) ? 0 : (fx > (float)(NX - 1)) ? NX - 1 : (int)fx;
        iy = (fy < 0) ? 0 : (fy > (float)(NY - 1)) ? NY - 1 : (int)fy;
        cell[i] = iy * NX + ix;
    }

    //   bins = [[] for i in range(NX*NY)]
    //   for j,i in enumerate(I): bins[int(i)].append(j)
    // "members" holds the bins one after another, each in input order.
    cellcount = calloc((size_t)ncells, sizeof(int));
    cellstart = calloc((size_t)ncells + 1, sizeof(int));
    cellfill = calloc((size_t)ncells, sizeof(int));
    thisrow = malloc((size_t)ncells * sizeof(int));
    if (!cellcount || !cellstart || !cellfill || !thisrow) {
        SYSERROR("Failed to allocate memory for %i bins", ncells);
        goto done;
    }
    for (i=0; i<nf; i++)
        cellcount[cell[i]]++;
    for (c=0; c<ncells; c++) {
        cellstart[c + 1] = cellstart[c] + cellcount[c];
        if (cellcount[c] > maxlen)
            maxlen = cellcount[c];
    }
    for (i=0; i<nf; i++)
        members[cellstart[cell[i]] + cellfill[cell[i]]++] = i;

    //   for i in range(maxlen):
    //       thisrow = [b[i] for b in bins if i < len(b)]
    //       thisrow.sort()
    //       J += thisrow
    //   T.cut(J)
    // The entries of "thisrow" are distinct row numbers, so the sort has no
    // ties and its stability does not matter.
    for (pass=0; pass<maxlen; pass++) {
        int nrow = 0;
        for (c=0; c<ncells; c++)
            if (pass < cellcount[c])
                thisrow[nrow++] = members[cellstart[c] + pass];
        qsort(thisrow, (size_t)nrow, sizeof(int), compare_ints);
        for (i=0; i<nrow; i++)
            order[nout++] = finite[thisrow[i]];
    }
    rtn = xytable_write(&t, infn, outfn, order, nout);
 done:
    free(order);
    free(finite);
    free(cell);
    free(cellcount);
    free(cellstart);
    free(cellfill);
    free(members);
    free(thisrow);
    xytable_free(&t);
    return rtn;
}
