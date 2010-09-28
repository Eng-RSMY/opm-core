#ifndef IFSH_H_INCLUDED
#define IFHS_H_INCLUDED

#include "grid.h"
#include "well.h"
#include "flow_bc.h"
#include "sparse_sys.h"


#ifdef __cplusplus
extern "C" {
#endif


struct ifsh_impl;

struct ifsh_data {
    int               max_ngconn;
    size_t            sum_ngconn2;

    /* Linear system */
    struct CSRMatrix *A;        /* Coefficient matrix */
    double           *b;        /* System RHS */
    double           *x;        /* Solution */

    /* Private implementational details. */
    struct ifsh_impl *pimpl;
};


struct ifsh_data *
ifsh_construct(grid_t *G, well_t *W);

void
ifsh_destroy(struct ifsh_data *h);

void
ifsh_assemble(flowbc_t         *bc,
              double           *src,
              double           *Binv,
              double           *gpress,
              well_control_t   *wctrl,
              double           *WI,
              double           *wdp,
              double           *totmob, /* \sum_i \lambda_i */
              double           *omega,  /* \sum_i \rho_i f_i */
              struct ifsh_data *h);

void
ifsh_press_flux(grid_t *G, struct ifsh_data *h, double *src,
                double *cpress, double *fflux,
                double *wpress, double *wflux);


#ifdef __cplusplus
}
#endif


#endif  /* IFSH_H_INCLUDED */
