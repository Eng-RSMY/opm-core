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

#ifndef OPM_HYBSYS_HEADER_INCLUDED
#define OPM_HYBSYS_HEADER_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

struct hybsys {
    double *L;                  /* C2' * inv(B) * C1 - P */
    double *q;                  /* g - F2*G */
    double *F1;                 /* C1' * inv(B)          */
    double *F2;                 /* C2' * inv(B)          */
    double *r;                  /* system rhs in single cell */
    double *S;                  /* system matrix in single cell */
    double *one;                /* ones(max_nconn, 1) */
};


struct hybsys_well {
    double *F1;
    double *F2;
    double *r;

    double *w2r;
    double *r2w;
    double *w2w;

    double *data;
};


struct hybsys *
hybsys_allocate_symm(int max_nconn, int nc, int nconn_tot);

struct hybsys *
hybsys_allocate_unsymm(int max_nconn, int nc, int nconn_tot);

struct hybsys_well *
hybsys_well_allocate_symm(int max_nconn, int nc, int *cwpos);

struct hybsys_well *
hybsys_well_allocate_unsymm(int max_nconn, int nc, int *cwpos);

void
hybsys_free(struct hybsys *sys);

void
hybsys_well_free(struct hybsys_well *wsys);

void
hybsys_init(int max_nconn, struct hybsys *sys);

/*
 * Schur complement reduction (per grid cell) of block matrix
 *
 *    [  B   C   D  ]
 *    [  C'  0   0  ]
 *    [  D'  0   0  ]
 *
 */
void
hybsys_schur_comp_symm(int nc, const int *pconn,
                       const double *Binv, struct hybsys *sys);

/*
 * Schur complement reduction (per grid cell) of block matrix
 *
 *    [    B     C   D  ]
 *    [  (C-V)'  P   0  ]
 *    [    D'    0   0  ]
 *
 */
void
hybsys_schur_comp_unsymm(int nc, const int *pconn,
                         const double *Binv, const double *BIV,
                         const double *P, struct hybsys *sys);

/*
 * Schur complement reduction (per grid cell) of block matrix
 *
 *    [   B   C   D  ]
 *    [  C2'  P   0  ]
 *    [   D'  0   0  ]
 *
 */
void
hybsys_schur_comp_gen(int nc, const int *pconn,
                      const double *Binv, const double *C2,
                      const double *P, struct hybsys *sys);

void
hybsys_well_schur_comp_symm(int nc, const int *cwpos,
                            double             *WI,
                            struct hybsys      *sys,
                            struct hybsys_well *wsys);

void
hybsys_cellcontrib_symm(int c, int nconn, int p1, int p2,
                        const double *gpress, const double *src,
                        const double *Binv, struct hybsys *sys);

void
hybsys_cellcontrib_unsymm(int c, int nconn, int p1, int p2,
                          const double *gpress, const double *src,
                          const double *Binv, struct hybsys *sys);

void
hybsys_well_cellcontrib_symm(int c, int ngconn, int p1,
                             const int *cwpos,
                             const double *WI, const double *wdp,
                             struct hybsys *sys, struct hybsys_well *wsys);

void
hybsys_compute_press_flux(int nc, const int *pconn, const int *conn,
                          const double *gpress,
                          const double *Binv, const struct hybsys *sys,
                          const double *pi, double *press, double *flux,
                          double *work);

void
hybsys_compute_press_flux_well(int nc, const int *pgconn, int nf,
                               int nw, const int *pwconn, const int *wconn,
                               const double *Binv,
                               const double *WI,
                               const double *wdp,
                               const struct hybsys      *sys,
                               const struct hybsys_well *wsys,
                               const double             *pi,
                               double *cpress, double *cflux,
                               double *wpress, double *wflux,
                               double *work);


#ifdef __cplusplus
}
#endif

#endif  /* OPM_HYBSYS_HEADER_INCLUDED */
