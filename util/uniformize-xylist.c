/*
 # This file is part of the Astrometry.net suite.
 # Licensed under a 3-clause BSD style license - see LICENSE
 */

/*
 A C version of the program "uniformize" (uniformize.py), with the same
 command line.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "xylist-filter.h"
#include "fitsioutils.h"
#include "errors.h"
#include "log.h"

const char* OPTIONS = "hn:X:Y:e:v";

static void printHelp(char* progname) {
    printf("Usage:   %s [options] <input-xylist> <output-xylist>\n"
           "      -n <boxes>  Number of boxes, approximately (default: 10)\n"
           "      -X <x-column-name>  (default: X)\n"
           "      -Y <y-column-name>  (default: Y)\n"
           "      -e <extension>  FITS extension to read; only 1 is supported\n"
           "      [-v]: add verboseness.\n"
           "\n", progname);
}

int main(int argc, char** args) {
    int argchar;
    char* progname = args[0];
    char* xcol = "X";
    char* ycol = "Y";
    int n = 10;
    int loglvl = LOG_MSG;

    while ((argchar = getopt (argc, args, OPTIONS)) != -1)
        switch (argchar) {
        case 'n':
            n = atoi(optarg);
            break;
        case 'X':
            xcol = optarg;
            break;
        case 'Y':
            ycol = optarg;
            break;
        case 'e':
            // uniformize.py accepts this option and then reads the first
            // table whatever its value.
            if (atoi(optarg) != 1) {
                printf("Only extension 1 is supported.\n");
                exit(-1);
            }
            break;
        case 'v':
            loglvl++;
            break;
        case '?':
        case 'h':
            printHelp(progname);
            return 0;
        default:
            return -1;
        }
    log_init(loglvl);

    if (optind != argc-2 || n < 1) {
        printHelp(progname);
        exit(-1);
    }

    fits_use_error_system();

    if (uniformize_xylist(args[optind], args[optind+1], n, xcol, ycol)) {
        ERROR("Failed to uniformize xylist \"%s\"", args[optind]);
        exit(-1);
    }
    return 0;
}
