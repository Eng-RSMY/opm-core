/*
  Copyright 2010 SINTEF ICT, Applied Mathematics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_CFS_TPFA_HEADER_INCLUDED
#define OPM_CFS_TPFA_HEADER_INCLUDED

#include "grid.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cfs_tpfa_impl;
struct CSRMatrix;

struct cfs_tpfa_data {
    struct CSRMatrix     *A;
    double               *b;
    double               *x;

    struct cfs_tpfa_impl *pimpl;
};


struct cfs_tpfa_data *
cfs_tpfa_construct(grid_t *G);

void
cfs_tpfa_assemble(grid_t               *G,
                  const double         *ctrans,
                  const double         *P,
                  const double         *src,
                  struct cfs_tpfa_data *h);

void
cfs_tpfa_press_flux(grid_t               *G,
                    const double         *trans,
                    struct cfs_tpfa_data *h,
                    double               *cpress,
                    double               *fflux);

void
cfs_tpfa_destroy(struct cfs_tpfa_data *h);

#ifdef __cplusplus
}
#endif

#endif  /* OPM_CFS_TPFA_HEADER_INCLUDED */
