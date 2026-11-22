/*
# This file is part of the Astrometry.net suite.
# Licensed under a 3-clause BSD style license - see LICENSE
*/
#ifndef XYLIST_FILTER_H
#define XYLIST_FILTER_H

/*
 C versions of the programs "removelines.py" and "uniformize.py", which
 augment-xylist runs on a list of sources before sorting and cutting it.

 Both read the first table extension of the xylist *infn* and write the
 whole file to *outfn* with rows of that table removed or reordered.  The
 X and Y columns must be 32-bit floats ("E"), as image2xy writes them.

 Both return 0 on success.
 */

// The default "cut" (significance level) of removelines.py.
#define REMOVELINES_DEFAULT_CUT 100.0

/*
 Removes sources that lie in horizontal or vertical lines: histograms the
 X and the Y coordinates in bins one pixel wide and drops every source in a
 bin that holds improbably many.
 */
int removelines_xylist(const char* infn, const char* outfn,
                       const char* xcol, const char* ycol, double cut);

/*
 Reorders the sources so that the list sweeps over a grid of about *n*
 cells, taking one source from each cell per pass, in the input order
 within each cell.
 */
int uniformize_xylist(const char* infn, const char* outfn, int n,
                      const char* xcol, const char* ycol);

#endif
