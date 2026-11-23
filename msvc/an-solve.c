/*
 `an-solve`: source extraction and plate solving in one native process.

 This program is part of the Windows port, not of upstream Astrometry.net. It
 does what `solve-field --no-plots --no-remove-lines --uniformize 0` does for
 one image, and calls the same library functions in the same order:

   solve-field                         an-solve
   ---------------------------------   ---------------------------------
   image2pnm, ppmtopgm, an-pnmtofits   (done beforehand; see below)
   augment_xylist():
     image2xy_files()  [cfitsio]       read_image(), image2xy_run(),
                                       write_xylist()  [qfits-an]
     removelines       [Python]        not available: must be skipped
     resort_xylist()                   resort_xylist()
     uniformize        [Python]        not available: must be skipped
     cut, job headers                  augment_xylist()
   astrometry-engine (subprocess)      engine_read_job_file(),
                                       engine_run_job()

 `image2xy_files()` reads the image and writes the source list through
 cfitsio, which the port does not have. `read_image()` and `write_xylist()`
 replace only that file handling. The extraction itself is `image2xy_run()`,
 called with the arguments that `augment_xylist()` passes.

 `augment_xylist()` runs `removelines` and `uniformize` as shell commands,
 which this port cannot do, and upstream 0.99 has no C version of them.
 The program therefore stops unless the options `--no-remove-lines` and
 `--uniformize 0` skip both steps. `prepare_xylist()` runs the sort that
 `augment_xylist()` would run, and then `augment_xylist()` adds the cut and
 the job headers.

 Input: a FITS image, or a binary PGM (`P5`). JPEG and other formats have to
 be converted first, the way `solve-field` does it:

   jpegtopnm IMAGE.jpg | ppmtopgm > IMAGE.pgm

 The options are those of `solve-field` that this path supports; the ones
 that `augment-xylist` defines are parsed by `augment_xylist_parse_option()`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <libgen.h>
#include <unistd.h>
#include <getopt.h>

#include "os-features.h"
#include "an-bool.h"
#include "an-opts.h"
#include "bl.h"
#include "ioutils.h"
#include "fileutils.h"
#include "errors.h"
#include "log.h"
#include "anqfits.h"
#include "qfits_image.h"
#include "fitsioutils.h"
#include "gslutils.h"
#include "simplexy.h"
#include "image2xy.h"
#include "starxy.h"
#include "xylist.h"
#include "sip.h"
#include "sip_qfits.h"
#include "sip-utils.h"
#include "augment-xylist.h"
#include "resort-xylist.h"
#include "tabsort.h"
#include "engine.h"

/* `augment_xylist()` passes this to `image2xy_files()`: "downsample by a
 factor of 2, up to 3 times" when no source is found. */
#define DOWNSAMPLE_AS_REQUIRED 3

static an_option_t options[] = {
    {'h', "help", no_argument, NULL, "print this help message"},
    {'v', "verbose", no_argument, NULL, "be more chatty -- repeat for even more verboseness"},
    {'D', "dir", required_argument, "directory", "place all output files in the specified directory"},
    {'o', "out", required_argument, "base-filename", "name the output files with this base name"},
    {'\x89', "config", required_argument, "filename", "use this config file for the engine"},
    {'\x96', "index-dir", required_argument, "dirname", "search for index files in the given directory"},
    {'\x97', "index-file", required_argument, "filename", "add this index file"},
    {'O', "overwrite", no_argument, NULL, "overwrite output files if they already exist"},
    {'p', "no-plots", no_argument, NULL, "accepted for compatibility with solve-field; plots are never made"},
};

static void print_help(const char* progname, bl* opts) {
    printf("\nUsage:   %s [options] <image.fits | image.pgm>\n\nOptions include:\n", progname);
    opts_print_help(opts, stdout, augment_xylist_print_special_opts, NULL);
    printf("\n");
}

typedef struct {
    int W;
    int H;
    /* Exactly one of these is set, as `simplexy_t` requires. */
    uint8_t* u8;
    float* f32;
} image_t;

/* Skips whitespace and `#` comments in a PNM header, then reads one
 non-negative integer. */
static int pnm_read_int(FILE* fid, int* value) {
    int c;
    long v = 0;
    int ndigits = 0;
    for (;;) {
        c = fgetc(fid);
        if (c == '#') {
            while (c != '\n' && c != EOF)
                c = fgetc(fid);
        } else if (!isspace(c)) {
            break;
        }
    }
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (v > INT32_MAX)
            return -1;
        ndigits++;
        c = fgetc(fid);
    }
    /* The one whitespace character after the number has been consumed. */
    if (!ndigits || !(isspace(c) || c == EOF))
        return -1;
    *value = (int)v;
    return 0;
}

/* Reads a binary PGM. An 8-bit file becomes a `uint8_t` image, which is what
 `an-pnmtofits` followed by `image2xy_files()` produces from it; a 16-bit
 file becomes a `float` image. Rows keep their order, as in `an-pnmtofits`. */
static int read_pgm(const char* fn, image_t* img) {
    FILE* fid = fopen(fn, "rb");
    int maxval;
    size_t npix;
    char magic[2];
    if (!fid) {
        SYSERROR("Failed to open image file \"%s\"", fn);
        return -1;
    }
    if (fread(magic, 1, 2, fid) != 2 || magic[0] != 'P' || magic[1] != '5') {
        ERROR("File \"%s\" is not a binary PGM (P5)", fn);
        fclose(fid);
        return -1;
    }
    if (pnm_read_int(fid, &img->W) || pnm_read_int(fid, &img->H) || pnm_read_int(fid, &maxval)
        || img->W <= 0 || img->H <= 0 || maxval <= 0 || maxval > 65535) {
        ERROR("Failed to parse the PGM header of \"%s\"", fn);
        fclose(fid);
        return -1;
    }
    npix = (size_t)img->W * (size_t)img->H;
    if (maxval < 256) {
        img->u8 = malloc(npix);
        if (!img->u8 || fread(img->u8, 1, npix, fid) != npix) {
            ERROR("Failed to read %zu pixels from \"%s\"", npix, fn);
            fclose(fid);
            return -1;
        }
    } else {
        size_t i;
        uint8_t* raw = malloc(npix * 2);
        img->f32 = malloc(npix * sizeof(float));
        if (!raw || !img->f32 || fread(raw, 2, npix, fid) != npix) {
            ERROR("Failed to read %zu pixels from \"%s\"", npix, fn);
            fclose(fid);
            return -1;
        }
        /* PGM samples are big-endian. */
        for (i = 0; i < npix; i++)
            img->f32[i] = (float)((raw[2*i] << 8) | raw[2*i + 1]);
        free(raw);
    }
    fclose(fid);
    return 0;
}

/* Reads the first plane of a FITS image extension. As in `image2xy_files()`,
 a `BITPIX = 8` image is read as `uint8_t` unless downsampling was requested,
 and anything else as `float`. */
static int read_fits(const char* fn, int ext, anbool want_u8, image_t* img) {
    anqfits_t* anq = anqfits_open(fn);
    const anqfits_image_t* meta;
    if (!anq) {
        ERROR("Failed to open FITS file \"%s\"", fn);
        return -1;
    }
    meta = anqfits_get_image_const(anq, ext);
    if (!meta) {
        ERROR("Extension %i of \"%s\" is not an image", ext, fn);
        anqfits_close(anq);
        return -1;
    }
    if (meta->bitpix == 8 && want_u8)
        img->u8 = anqfits_readpix(anq, ext, 0, 0, 0, 0, 0, PTYPE_UINT8, NULL, &img->W, &img->H);
    else
        img->f32 = anqfits_readpix(anq, ext, 0, 0, 0, 0, 0, PTYPE_FLOAT, NULL, &img->W, &img->H);
    anqfits_close(anq);
    if (!img->u8 && !img->f32) {
        ERROR("Failed to read image pixels from \"%s\" extension %i", fn, ext);
        return -1;
    }
    return 0;
}

static int read_image(const char* fn, int ext, anbool want_u8, image_t* img) {
    FILE* fid = fopen(fn, "rb");
    char magic[6];
    size_t n;
    memset(img, 0, sizeof(image_t));
    if (!fid) {
        SYSERROR("Failed to open image file \"%s\"", fn);
        return -1;
    }
    n = fread(magic, 1, sizeof(magic), fid);
    fclose(fid);
    if (n == sizeof(magic) && !memcmp(magic, "SIMPLE", 6))
        return read_fits(fn, ext, want_u8, img);
    if (n >= 2 && magic[0] == 'P' && magic[1] == '5') {
        if (read_pgm(fn, img))
            return -1;
        if (img->u8 && !want_u8) {
            /* `image2xy_files()` reads an 8-bit image as `float` when
             downsampling is requested. */
            size_t i, npix = (size_t)img->W * (size_t)img->H;
            img->f32 = malloc(npix * sizeof(float));
            if (!img->f32) {
                SYSERROR("Failed to allocate image array");
                return -1;
            }
            for (i = 0; i < npix; i++)
                img->f32[i] = img->u8[i];
            free(img->u8);
            img->u8 = NULL;
        }
        return 0;
    }
    ERROR("File \"%s\" is neither a FITS image nor a binary PGM (P5). "
          "Convert it first, for example: jpegtopnm x.jpg | ppmtopgm > x.pgm", fn);
    return -1;
}

/* Writes the source list with the columns and types that `image2xy_files()`
 writes: X, Y, FLUX and BACKGROUND as 32-bit floats. */
static int write_xylist(const char* fn, const simplexy_t* s, int W, int H) {
    xylist_t* ls;
    starxy_t* xy;
    qfits_header* hdr;
    int i;

    ls = xylist_open_for_writing(fn);
    if (!ls) {
        ERROR("Failed to open xylist \"%s\" for writing", fn);
        return -1;
    }
    xylist_set_xtype(ls, TFITS_BIN_TYPE_E);
    xylist_set_ytype(ls, TFITS_BIN_TYPE_E);
    xylist_set_include_flux(ls, TRUE);
    xylist_set_include_background(ls, TRUE);
    if (xylist_write_primary_header(ls)) {
        ERROR("Failed to write the primary header of \"%s\"", fn);
        return -1;
    }
    hdr = xylist_get_header(ls);
    fits_header_add_int(hdr, "IMAGEW", W, "Input image width");
    fits_header_add_int(hdr, "IMAGEH", H, "Input image height");
    if (xylist_write_header(ls)) {
        ERROR("Failed to write the table header of \"%s\"", fn);
        return -1;
    }
    xy = starxy_new(s->npeaks, TRUE, TRUE);
    for (i = 0; i < s->npeaks; i++) {
        xy->x[i] = s->x[i];
        xy->y[i] = s->y[i];
        xy->flux[i] = s->flux[i];
        xy->background[i] = s->background[i];
    }
    if (xylist_write_field(ls, xy) || xylist_fix_header(ls)
        || xylist_fix_primary_header(ls) || xylist_close(ls)) {
        ERROR("Failed to write the source list to \"%s\"", fn);
        return -1;
    }
    starxy_free(xy);
    return 0;
}

/* `augment_xylist()` calls `image2xy_files()` when it is given an image. The
 upstream function needs cfitsio, so it is not part of this build. This
 program always hands `augment_xylist()` a source list, so a call here means
 that assumption broke. */
int image2xy_files(const char* infn, const char* outfn,
                   anbool do_u8, int downsample, int downsample_as_required,
                   int extension, int plane, simplexy_t* params) {
    (void)outfn; (void)do_u8; (void)downsample; (void)downsample_as_required;
    (void)extension; (void)plane; (void)params;
    ERROR("image2xy_files() is not available in the MSVC port (image \"%s\")", infn);
    return -1;
}

/* Runs the sort that `augment_xylist()` applies to a freshly extracted
 source list ahead of the cut, with the arguments that it uses. `main()` has
 made sure that remove lines and uniformize are turned off. Returns the name
 of the file written, and clears the sort column in `axy` so that
 `augment_xylist()` does not sort again. `tempfiles` receives the names of
 the files to delete afterwards. */
static char* prepare_xylist(augment_xylist_t* axy, char* xylsfn, sl* tempfiles) {
    const char* sortcol = axy->sortcol ? axy->sortcol : "FLUX";
    const char* bgcol = axy->bgcol ? axy->bgcol : "BACKGROUND";
    char* fn;
    anbool do_tabsort = FALSE;

    /* An image input is always sorted (`dosort = TRUE`). */
    fn = create_temp_file("sorted", axy->tempdir);
    sl_append_nocopy(tempfiles, fn);
    if (axy->resort) {
        char* err;
        int rtn;
        logverb("Sorting file \"%s\" to \"%s\" using columns flux (%s) and background (%s), %sscending\n",
                xylsfn, fn, sortcol, bgcol, axy->sort_ascending ? "a" : "de");
        errors_start_logging_to_string();
        rtn = resort_xylist(xylsfn, fn, sortcol, bgcol, axy->sort_ascending);
        err = errors_stop_logging_to_string(": ");
        if (rtn) {
            logmsg("Sorting brightness using %s and BACKGROUND columns failed; falling back to %s.\n",
                   sortcol, sortcol);
            logverb("Reason: %s\n", err);
            do_tabsort = TRUE;
        }
        free(err);
    } else
        do_tabsort = TRUE;
    if (do_tabsort) {
        logverb("Sorting by brightness: input=%s, output=%s, column=%s.\n", xylsfn, fn, sortcol);
        tabsort(xylsfn, fn, sortcol, !axy->sort_ascending);
    }
    xylsfn = fn;

    /* With an xylist input, `augment_xylist()` sorts only when a sort column
     is named. */
    axy->sortcol = NULL;
    return xylsfn;
}

/* Formats one of the output filename patterns ("%s.wcs") with the base name.
 "none" turns the output off, as in `solve-field`. */
static char* output_name(const char* pattern, const char* base) {
    char* fn;
    if (!pattern || streq(pattern, "none"))
        return NULL;
    asprintf_safe(&fn, pattern, base);
    return fn;
}

int main(int argc, char** args) {
    int c, i;
    anbool help = FALSE;
    anbool overwrite = FALSE;
    int loglvl = LOG_MSG;
    char* outdir = NULL;
    char* outbase = NULL;
    char* configfn = NULL;
    sl* index_dirs = sl_new(4);
    sl* index_files = sl_new(4);
    bl* opts;
    int nmyopts;
    /* The `augment-xylist` options for choosing the input and the output,
     which this program handles itself, as `solve-field` does. */
    const char* removeopts = "ixo\x01";
    augment_xylist_t theaxy;
    augment_xylist_t* axy = &theaxy;
    char* me;
    char* infile;
    char* base;
    char* xylsfn;
    sl* tempfiles;
    char** outputs[6];
    image_t img;
    simplexy_t sxy;
    engine_t* engine;
    job_t* job;
    int rtn = 0;

    errors_print_on_exit(stderr);
    fits_use_error_system();

    me = find_executable(args[0], NULL);
    if (!me)
        me = strdup(args[0]);

    nmyopts = sizeof(options) / sizeof(an_option_t);
    opts = opts_from_array(options, nmyopts, NULL);
    augment_xylist_add_options(opts);
    /* This program's options win over `augment-xylist` options with the same
     short letter (the same step as in `solve-field`). */
    for (i = 0; i < nmyopts; i++) {
        an_option_t* opt1 = bl_access(opts, i);
        size_t j;
        for (j = nmyopts; j < bl_size(opts); j++) {
            an_option_t* opt2 = bl_access(opts, j);
            if (opt2->shortopt == opt1->shortopt)
                bl_remove_index(opts, j);
        }
    }
    for (i = 0; i < (int)strlen(removeopts); i++) {
        size_t j;
        for (j = nmyopts; j < bl_size(opts); j++) {
            an_option_t* opt2 = bl_access(opts, j);
            if (opt2->shortopt == removeopts[i])
                bl_remove_index(opts, j);
        }
    }

    augment_xylist_init(axy);
    /* The default output files of `solve-field`. */
    axy->axyfn = "%s.axy";
    axy->matchfn = "%s.match";
    axy->rdlsfn = "%s.rdls";
    axy->solvedfn = "%s.solved";
    axy->wcsfn = "%s.wcs";
    axy->corrfn = "%s.corr";

    while (1) {
        c = opts_getopt(opts, argc, args);
        if (c == -1)
            break;
        switch (c) {
        case 'h':
            help = TRUE;
            break;
        case 'v':
            axy->verbosity++;
            loglvl++;
            break;
        case 'D':
            outdir = optarg;
            break;
        case 'o':
            outbase = optarg;
            break;
        case '\x89':
            configfn = optarg;
            break;
        case '\x96':
            sl_append(index_dirs, optarg);
            break;
        case '\x97':
            sl_append(index_files, optarg);
            break;
        case 'O':
            overwrite = TRUE;
            break;
        case 'p':
            break;
        default:
            if (augment_xylist_parse_option(c, optarg, axy)) {
                print_help(args[0], opts);
                exit(-1);
            }
        }
    }
    if (optind != argc - 1 && !help) {
        printf("ERROR: specify exactly one image file.\n");
        help = TRUE;
        rtn = -1;
    }
    if (help) {
        print_help(args[0], opts);
        exit(rtn);
    }
    bl_free(opts);
    infile = args[optind];

    log_init(loglvl);
    gslutils_use_error_system();

    if (axy->use_source_extractor) {
        ERROR("--use-source-extractor is not supported by this program");
        exit(-1);
    }
    if (!axy->no_removelines || axy->uniformize) {
        ERROR("This build has no removelines and no uniformize: "
              "`augment_xylist()` runs them as the Python programs, which cannot be started here. "
              "Pass --no-remove-lines --uniformize 0 to skip both steps.");
        exit(-1);
    }
    if (!configfn && !sl_size(index_dirs) && !sl_size(index_files)) {
        ERROR("Specify --config, --index-dir or --index-file: this program has no default config file");
        exit(-1);
    }

    /* Output file names: <dir>/<base>.wcs and so on, as in `solve-field`. */
    {
        char* copy = strdup(infile);
        char* dircopy = strdup(infile);
        char* name = outbase ? strdup(outbase) : strdup(basename(copy));
        if (!outbase) {
            char* dot = strrchr(name, '.');
            if (dot && dot != name)
                *dot = '\0';
        }
        asprintf_safe(&base, "%s/%s", outdir ? outdir : dirname(dircopy), name);
        free(copy);
        free(dircopy);
        free(name);
    }
    if (outdir && mkdir_p(outdir)) {
        ERROR("Failed to create output directory %s", outdir);
        exit(-1);
    }
    axy->axyfn = output_name(axy->axyfn, base);
    axy->matchfn = output_name(axy->matchfn, base);
    axy->rdlsfn = output_name(axy->rdlsfn, base);
    axy->solvedfn = output_name(axy->solvedfn, base);
    axy->wcsfn = output_name(axy->wcsfn, base);
    axy->corrfn = output_name(axy->corrfn, base);
    if (!axy->axyfn) {
        ERROR("The .axy file cannot be turned off: the engine reads the job from it");
        exit(-1);
    }
    outputs[0] = &axy->axyfn;
    outputs[1] = &axy->matchfn;
    outputs[2] = &axy->rdlsfn;
    outputs[3] = &axy->solvedfn;
    outputs[4] = &axy->wcsfn;
    outputs[5] = &axy->corrfn;
    for (i = 0; i < 6; i++) {
        char* fn = *outputs[i];
        if (!fn || !file_exists(fn))
            continue;
        if (!overwrite) {
            ERROR("Output file already exists: \"%s\". Use --overwrite to replace it.", fn);
            exit(-1);
        }
        if (unlink(fn)) {
            SYSERROR("Failed to delete an already-existing output file \"%s\"", fn);
            exit(-1);
        }
    }

    /* Source extraction, with the parameters that `augment_xylist()` sets
     before it calls `image2xy_files()`. */
    logmsg("Extracting sources...\n");
    if (read_image(infile, axy->extension, !axy->downsample, &img))
        exit(-1);
    memset(&sxy, 0, sizeof(simplexy_t));
    sxy.nobgsub = axy->no_bg_subtraction;
    sxy.sigma = axy->image_sigma;
    sxy.invert = axy->invert_image;
    sxy.plim = axy->image_nsigma;
    if (img.u8) {
        simplexy_fill_in_defaults_u8(&sxy);
        sxy.image_u8 = img.u8;
    } else {
        simplexy_fill_in_defaults(&sxy);
        sxy.image = img.f32;
    }
    sxy.nx = img.W;
    sxy.ny = img.H;
    if (image2xy_run(&sxy, axy->downsample, DOWNSAMPLE_AS_REQUIRED)) {
        ERROR("Source extraction failed");
        exit(-1);
    }
    logverb("simplexy: found %i sources.\n", sxy.npeaks);

    xylsfn = create_temp_file("xyls", axy->tempdir);
    if (write_xylist(xylsfn, &sxy, img.W, img.H))
        exit(-1);
    simplexy_free_contents(&sxy);
    simplexy_clean_cache();

    /* Remove lines, sort and uniformize here; `augment_xylist()` then gets the
     result as an xylist input and adds the cut and the job headers. */
    tempfiles = sl_new(4);
    sl_append_nocopy(tempfiles, xylsfn);
    axy->xylsfn = prepare_xylist(axy, xylsfn, tempfiles);
    axy->W = img.W;
    axy->H = img.H;
    if (augment_xylist(axy, me)) {
        ERROR("augment-xylist failed");
        exit(-1);
    }
    if (!axy->no_delete_temp) {
        for (i = 0; i < (int)sl_size(tempfiles); i++) {
            if (unlink(sl_get(tempfiles, i)))
                SYSERROR("Failed to delete temp file \"%s\"", sl_get(tempfiles, i));
        }
    }
    sl_free2(tempfiles);

    /* The engine, set up as in `engine-main.c`. */
    engine = engine_new();
    if (configfn && engine_parse_config_file(engine, configfn)) {
        ERROR("Failed to parse config file \"%s\"", configfn);
        exit(-1);
    }
    if (sl_size(index_dirs)) {
        sl* saved_paths = engine->index_paths;
        engine->index_paths = index_dirs;
        if (engine_autoindex_search_paths(engine)) {
            ERROR("Failed to search directories for index files");
            exit(-1);
        }
        engine->index_paths = saved_paths;
    }
    for (i = 0; i < (int)sl_size(index_files); i++) {
        if (engine_add_index(engine, sl_get(index_files, i))) {
            ERROR("Failed to add index \"%s\"", sl_get(index_files, i));
            exit(-1);
        }
    }
    if (!pl_size(engine->indexes)) {
        ERROR("No index files were found");
        exit(-1);
    }
    if (engine->minwidth <= 0.0 || engine->maxwidth <= 0.0) {
        ERROR("\"minwidth\" and \"maxwidth\" in the config file must be positive");
        exit(-1);
    }
    if (!il_size(engine->default_depths)) {
        /* The default of `engine-main.c`. */
        int depth;
        for (depth = 10; depth <= 200; depth += 10)
            il_append(engine->default_depths, depth);
    }

    logmsg("Reading file \"%s\"...\n", axy->axyfn);
    job = engine_read_job_file(engine, axy->axyfn);
    if (!job) {
        ERROR("Failed to read job file \"%s\"", axy->axyfn);
        exit(-1);
    }
    if (engine_run_job(engine, job))
        logerr("Failed to run_job()\n");
    job_free(job);
    engine_free(engine);

    /* The report that `solve-field` prints after the engine returns. */
    logmsg("Field: %s\n", infile);
    if (axy->wcsfn && file_exists(axy->wcsfn)) {
        sip_t wcs;
        double ra, dec, fieldw, fieldh;
        char rastr[32], decstr[32];
        char* fieldunits;
        if (!sip_read_header_file(axy->wcsfn, &wcs)) {
            ERROR("Failed to read WCS header from file %s", axy->wcsfn);
            exit(-1);
        }
        sip_get_radec_center(&wcs, &ra, &dec);
        sip_get_radec_center_hms_string(&wcs, rastr, decstr);
        sip_get_field_size(&wcs, &fieldw, &fieldh, &fieldunits);
        logmsg("Field center: (RA,Dec) = (%f, %f) deg.\n", ra, dec);
        logmsg("Field center: (RA H:M:S, Dec D:M:S) = (%s, %s).\n", rastr, decstr);
        logmsg("Field size: %g x %g %s\n", fieldw, fieldh, fieldunits);
    } else {
        logmsg("Did not solve (or no WCS file was written).\n");
    }

    augment_xylist_free_contents(axy);
    sl_free2(index_dirs);
    sl_free2(index_files);
    free(me);
    return 0;
}
