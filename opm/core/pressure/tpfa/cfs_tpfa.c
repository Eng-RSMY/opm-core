#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <opm/core/well.h>
#include <opm/core/linalg/blas_lapack.h>
#include <opm/core/linalg/sparse_sys.h>
#include <opm/core/pressure/flow_bc.h>

#include <opm/core/pressure/tpfa/compr_quant.h>
#include <opm/core/pressure/tpfa/trans_tpfa.h>
#include <opm/core/pressure/tpfa/cfs_tpfa.h>


#if defined(MAX)
#undef MAX
#endif

#define MAX(a,b) (((a) > (b)) ? (a) : (b))



struct densrat_util {
    MAT_SIZE_T *ipiv;
    double     *lu;

    double     *x;
    double     *Ai_y;
    double     *psum;

    /* Storage */
    double *ddata;
};


struct cfs_tpfa_impl {
    /* Mapping (cell,face) -> half-face */
    int                 *f2hf;

    /* Reservoir flow */
    double              *ctrans;
    double              *accum;

    /* One entry per component per face */
    double              *masstrans_f; /* RB^{-1} [ phase-mobility ] */
    double              *gravtrans_f; /* RB^{-1} [ grav + capillary ] */

    /* Compressible completion flow definition */
    double              *wtrans; /* WI * sum((Ac \ Ap) [ pmob ]) */
    double              *wgpot;  /* WI * sum((Ac \ Ap) [ pmob ]'*WdP) */

    /* One entry per component per completion/perforation */
    double              *masstrans_p; /* RB^{-1} [ phase-mobility ] */
    double              *gravtrans_p; /* RB^{-1} [ grav ] */

    /* Scratch array for face pressure calculation */
    double              *scratch_f;

    struct densrat_util *ratio;

    /* Linear storage */
    double *ddata;
};


/* ---------------------------------------------------------------------- */
static void
deallocate_densrat(struct densrat_util *ratio)
/* ---------------------------------------------------------------------- */
{
    if (ratio != NULL) {
        free(ratio->ddata);
        free(ratio->ipiv);
    }

    free(ratio);
}


/* ---------------------------------------------------------------------- */
static struct densrat_util *
allocate_densrat(struct UnstructuredGrid *g, well_t *w, int np)
/* ---------------------------------------------------------------------- */
{
    int    ntotperf;
    size_t nglobconn, ntotconn, ddata_sz;

    struct densrat_util *new;

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        if (w != NULL) {
            ntotperf = w->well_connpos[ w->number_of_wells ];
        } else {
            ntotperf = 0;
        }

        nglobconn = MAX(g->number_of_faces                   , ntotperf);
        ntotconn  = MAX(g->cell_facepos[ g->number_of_cells ], ntotperf);

        ddata_sz  = np * np;        /* lu */
        ddata_sz += np * nglobconn; /* x */
        ddata_sz += np * ntotconn;  /* Ai_y */
        ddata_sz += 1  * ntotconn;  /* psum */

        new->ipiv  = malloc(np       * sizeof *new->ipiv);
        new->ddata = malloc(ddata_sz * sizeof *new->ddata);

        if ((new->ipiv == NULL) || (new->ddata == NULL)) {
            deallocate_densrat(new);
            new = NULL;
        } else {
            new->lu   = new->ddata + 0             ;
            new->x    = new->lu    + np * np       ;
            new->Ai_y = new->x     + np * nglobconn;
            new->psum = new->Ai_y  + np * ntotconn ;
        }
    }

    return new;
}


/* ---------------------------------------------------------------------- */
static void
impl_deallocate(struct cfs_tpfa_impl *pimpl)
/* ---------------------------------------------------------------------- */
{
    if (pimpl != NULL) {
        free              (pimpl->ddata);
        deallocate_densrat(pimpl->ratio);
    }

    free(pimpl);
}


/* ---------------------------------------------------------------------- */
static struct cfs_tpfa_impl *
impl_allocate(struct UnstructuredGrid *G, well_t *W, int np)
/* ---------------------------------------------------------------------- */
{
    size_t                nnu, nf, ngconn, nwperf;
    struct cfs_tpfa_impl *new;

    size_t ddata_sz;

    nnu    = G->number_of_cells;
    ngconn = G->cell_facepos[ G->number_of_cells ];
    nwperf = 0;

    if (W != NULL) {
        nnu    += W->number_of_wells;
        nwperf  = W->well_connpos[ W->number_of_wells ];
    }

    /* Linear system */
    ddata_sz  = 2  * nnu;                /* b, x */

    /* Reservoir */
    nf        = G->number_of_faces;
    ddata_sz += 1  * ngconn;             /* ctrans */
    ddata_sz += 1  * G->number_of_cells; /* accum */
    ddata_sz += np * nf;                 /* masstrans_f */
    ddata_sz += np * nf;                 /* gravtrans_f */

    /* Wells */
    ddata_sz += 1  * nwperf;             /* wtrans */
    ddata_sz += 1  * nwperf;             /* wgpot */
    ddata_sz += np * nwperf;             /* masstrans_p */
    ddata_sz += np * nwperf;             /* gravtrans_p */

    ddata_sz += 1  * nf;                 /* scratch_f */

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        new->f2hf  = malloc(2 * nf   * sizeof *new->f2hf );
        new->ddata = malloc(ddata_sz * sizeof *new->ddata);
        new->ratio = allocate_densrat(G, W, np);

        if (new->f2hf  == NULL ||
            new->ddata == NULL || new->ratio == NULL) {
            impl_deallocate(new);
            new = NULL;
        }
    }

    return new;
}


/* ---------------------------------------------------------------------- */
static struct CSRMatrix *
construct_matrix(struct UnstructuredGrid *G, well_t *W)
/* ---------------------------------------------------------------------- */
{
    int    f, c1, c2, w, i, nc, nnu;
    size_t nnz;

    struct CSRMatrix *A;

    nc = nnu = G->number_of_cells;
    if (W != NULL) {
        nnu += W->number_of_wells;
    }

    A = csrmatrix_new_count_nnz(nnu);

    if (A != NULL) {
        /* Self connections */
        for (i = 0; i < nnu; i++) {
            A->ia[ i + 1 ] = 1;
        }

        /* Other connections */
        for (f = 0; f < G->number_of_faces; f++) {
            c1 = G->face_cells[2*f + 0];
            c2 = G->face_cells[2*f + 1];

            if ((c1 >= 0) && (c2 >= 0)) {
                A->ia[ c1 + 1 ] += 1;
                A->ia[ c2 + 1 ] += 1;
            }
        }

        if (W != NULL) {
            /* Well <-> cell connections */
            for (w = i = 0; w < W->number_of_wells; w++) {
                for (; i < W->well_connpos[w + 1]; i++) {
                    c1 = W->well_cells[i];

                    A->ia[ 0  + c1 + 1 ] += 1; /* c -> w */
                    A->ia[ nc + w  + 1 ] += 1; /* w -> c */
                }
            }
        }

        nnz = csrmatrix_new_elms_pushback(A);
        if (nnz == 0) {
            csrmatrix_delete(A);
            A = NULL;
        }
    }

    if (A != NULL) {
        /* Fill self connections */
        for (i = 0; i < nnu; i++) {
            A->ja[ A->ia[ i + 1 ] ++ ] = i;
        }

        /* Fill other connections */
        for (f = 0; f < G->number_of_faces; f++) {
            c1 = G->face_cells[2*f + 0];
            c2 = G->face_cells[2*f + 1];

            if ((c1 >= 0) && (c2 >= 0)) {
                A->ja[ A->ia[ c1 + 1 ] ++ ] = c2;
                A->ja[ A->ia[ c2 + 1 ] ++ ] = c1;
            }
        }

        if (W != NULL) {
            /* Fill well <-> cell connections */
            for (w = i = 0; w < W->number_of_wells; w++) {
                for (; i < W->well_connpos[w + 1]; i++) {
                    c1 = W->well_cells[i];

                    A->ja[ A->ia[ 0  + c1 + 1 ] ++ ] = nc + w;
                    A->ja[ A->ia[ nc + w  + 1 ] ++ ] = c1    ;
                }
            }
        }

        assert ((size_t) A->ia[ nnu ] == nnz);

        /* Enforce sorted connection structure per row */
        csrmatrix_sortrows(A);
    }

    return A;
}


/* ---------------------------------------------------------------------- */
static void
solve_cellsys_core(struct UnstructuredGrid       *G   ,
                   size_t        sz  ,
                   const double *Ac  ,
                   const double *bf  ,
                   double       *xcf ,
                   double       *luAc,
                   MAT_SIZE_T   *ipiv)
/* ---------------------------------------------------------------------- */
{
    int         c, i, f;
    size_t      j, p2;
    double     *v;

    MAT_SIZE_T  nrows, ncols, ldA, ldX, nrhs, info;

    nrows = ncols = ldA = ldX = sz;
    info  = 0;

    v     = xcf;

    for (c = 0, p2 = 0; c < G->number_of_cells; c++) {
        /* Define right-hand sides for local systems */
        for (i = G->cell_facepos[c + 0], nrhs = 0;
             i < G->cell_facepos[c + 1]; i++, nrhs++) {
            f = G->cell_faces[i];

            for (j = 0; j < sz; j++) {
                v[nrhs*sz + j] = bf[f*sz + j];
            }
        }

        /* Factor Ac */
        memcpy(luAc, Ac + p2, sz * sz * sizeof *luAc);
        dgetrf_(&nrows, &ncols, luAc, &ldA, ipiv, &info);

        /* Solve local systems */
        dgetrs_("No Transpose", &nrows, &nrhs,
                luAc, &ldA, ipiv, v, &ldX, &info);

        v  += nrhs * sz;
        p2 += sz   * sz;
    }
}


/* ---------------------------------------------------------------------- */
static void
small_matvec(size_t        n,
             int           sz,
             const double *A,
             const double *X,
             double       *Y)
/* ---------------------------------------------------------------------- */
{
    size_t i, p1, p2;

    MAT_SIZE_T  nrows, ncols, ld, incx, incy;
    double      a1, a2;

    nrows = ncols = ld = sz;
    incx  = incy  = 1;

    a1 = 1.0;
    a2 = 0.0;

    for (i = p1 = p2 = 0; i < n; i++) {
        dgemv_("No Transpose", &nrows, &ncols,
               &a1, A + p2, &ld, X + p1, &incx,
               &a2,              Y + p1, &incy);

        p1 += sz;
        p2 += sz * sz;
    }
}


/* ---------------------------------------------------------------------- */
static void
solve_cellsys(struct UnstructuredGrid              *G ,
              size_t               sz,
              const double        *Ac,
              const double        *bf,
              struct densrat_util *ratio)
/* ---------------------------------------------------------------------- */
{
    solve_cellsys_core(G, sz, Ac, bf, ratio->Ai_y,
                       ratio->lu, ratio->ipiv);
}


/* ---------------------------------------------------------------------- */
static void
set_dynamic_trans(struct UnstructuredGrid                  *G    ,
                  const double            *trans,
                  struct compr_quantities *cq   ,
                  struct densrat_util     *ratio)
/* ---------------------------------------------------------------------- */
{
    int f, p, i;

    for (f = i = 0; f < G->number_of_faces; f++) {
        for (p = 0; p < cq->nphases; p++, i++) {
            ratio->x[i] = trans[f] * cq->phasemobf[i];
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
set_dynamic_grav(struct UnstructuredGrid                  *G        ,
                 struct FlowBoundaryConditions *bc       ,
                 const double            *trans    ,
                 const double            *gravcap_f,
                 struct compr_quantities *cq       ,
                 struct densrat_util     *ratio)
/* ---------------------------------------------------------------------- */
{
    int f, p, i, c1, c2;

    for (f = i = 0; f < G->number_of_faces; f++) {
        c1 = G->face_cells[2*f + 0];
        c2 = G->face_cells[2*f + 1];

        if ((c1 >= 0) && (c2 >= 0)) {
            for (p = 0; p < cq->nphases; p++, i++) {
                ratio->x[i] = trans[f] * gravcap_f[i] * cq->phasemobf[i];
            }
        } else {
            for (p = 0; p < cq->nphases; p++, i++) {
                ratio->x[i] = 0.0;
            }
        }
    }

    if (bc != NULL) {
        for (i = 0; ((size_t) i) < bc->nbc; i++) {
            if (bc->type[ i ] == BC_PRESSURE) {
                f = bc->face[ i ];

                for (p = cq->nphases * f; p < cq->nphases * (f + 1); p++) {
                    ratio->x[p] = trans[f] * gravcap_f[p] * cq->phasemobf[p];
                }
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
set_dynamic_trans_well(well_t                 *W,
                       size_t                  np,
                       struct completion_data *wdata,
                       struct densrat_util    *ratio)
/* ---------------------------------------------------------------------- */
{
    size_t p, i, k, nconn;

    nconn = W->well_connpos[ W->number_of_wells ];

    for (i = k = 0; i < nconn; i++) {
        for (p = 0; p < np; p++, k++) {
            ratio->x[k] = wdata->WI[i] * wdata->phasemob[k];
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
set_dynamic_grav_well(well_t                 *W,
                      size_t                  np,
                      struct completion_data *wdata,
                      struct densrat_util    *ratio)
/* ---------------------------------------------------------------------- */
{
    size_t p, i, k, nconn;

    nconn = W->well_connpos[ W->number_of_wells ];

    for (i = k = 0; i < nconn; i++) {
        for (p = 0; p < np; p++, k++) {
            ratio->x[k] = wdata->WI[i] * wdata->gpot[k] * wdata->phasemob[k];
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
sum_phase_contrib(struct UnstructuredGrid       *G  ,
                  size_t        sz ,
                  const double *xcf,
                  double       *sum)
/* ---------------------------------------------------------------------- */
{
    int    c, i;
    size_t j;

    const double *v;

    for (c = i = 0, v = xcf; c < G->number_of_cells; c++) {
        for (; i < G->cell_facepos[c + 1]; i++, v += sz) {

            sum[i] = 0.0;
            for (j = 0; j < sz; j++) {
                sum[i] += v[j];
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
compute_densrat_update(struct UnstructuredGrid                  *G    ,
                       struct compr_quantities *cq   ,
                       struct densrat_util     *ratio,
                       double                  *q)
/* ---------------------------------------------------------------------- */
{
    /* q = Af * x */
    small_matvec(G->number_of_faces, cq->nphases, cq->Af, ratio->x, q);

    /* ratio->Ai_y = Ac \ q */
    solve_cellsys(G, cq->nphases, cq->Ac, q, ratio);

    /* ratio->psum = sum_\alpha ratio->Ai_y */
    sum_phase_contrib(G, cq->nphases, ratio->Ai_y, ratio->psum);
}


/* ---------------------------------------------------------------------- */
static void
compute_densrat_update_well(well_t                  *W    ,
                            struct completion_data  *wdata,
                            struct compr_quantities *cq   ,
                            struct densrat_util     *ratio,
                            double                  *q)
/* ---------------------------------------------------------------------- */
{
    size_t     c, i, nconn, p, np, np2;
    MAT_SIZE_T nrows, ncols, ldA, ldX, nrhs, info, incx, incy;
    double     a1, a2;

    nconn = W->well_connpos[ W->number_of_wells ];
    np    = cq->nphases;
    np2   = np * np;

    nrows = ncols = ldA  = ldX = np;
    incx  = incy  = nrhs       = 1;

    a1 = 1.0;
    a2 = 0.0;

    for (i = 0; i < nconn; i++) {
        c = W->well_cells[i];

        /* Compute q = A*x on completion */
        dgemv_("No Transpose", &nrows, &ncols,
               &a1, wdata->A + i*np2, &ldA, ratio->x + i*np, &incx,
               &a2,                         q        + i*np, &incy);

        /* Form system RHS */
        for (p = 0; p < np; p++) {
            ratio->Ai_y[i*np + p] = q[i*np + p];
        }

        /* Factor A in cell 'c' */
        memcpy(ratio->lu, cq->Ac + c*np2, np2 * sizeof *ratio->lu);
        dgetrf_(&nrows, &ncols, ratio->lu, &ldA, ratio->ipiv, &info);

        /* Solve local system (=> Ai_y = Ac \ (A*x)) */
        dgetrs_("No Transpose"    , &nrows, &nrhs,
                ratio->lu         , &ldA, ratio->ipiv,
                ratio->Ai_y + i*np, &ldX, &info);

        /* Accumulate phase contributions */
        ratio->psum[i] = 0.0;
        for (p = 0; p < np; p++) {
            ratio->psum[i] += ratio->Ai_y[i*np + p];
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
compute_psys_contrib(struct UnstructuredGrid                  *G,
                     well_t                  *W,
                     struct completion_data  *wdata,
                     struct FlowBoundaryConditions *bc,
                     struct compr_quantities *cq,
                     double                   dt,
                     const double            *trans,
                     const double            *gravcap_f,
                     const double            *cpress0,
                     const double            *porevol,
                     struct cfs_tpfa_data    *h)
/* ---------------------------------------------------------------------- */
{
    int    c, nc, i, f;
    size_t nconn;
    double s;

    nc    = G->number_of_cells;
    nconn = G->cell_facepos[nc];

    /* Compressible one-sided transmissibilities */
    set_dynamic_trans(G, trans, cq, h->pimpl->ratio);
    compute_densrat_update(G, cq, h->pimpl->ratio,
                           h->pimpl->masstrans_f);
    memcpy(h->pimpl->ctrans,
           h->pimpl->ratio->psum,
           nconn * sizeof *h->pimpl->ctrans);

    /* Compressible gravity contributions */
    set_dynamic_grav(G, bc, trans, gravcap_f, cq, h->pimpl->ratio);
    compute_densrat_update(G, cq, h->pimpl->ratio,
                           h->pimpl->gravtrans_f);

    for (c = 0, i = 0; c < nc; c++) {
        for (; i < G->cell_facepos[c + 1]; i++) {
            f = G->cell_faces[i];
            s = 1.0 - 2.0*(G->face_cells[2*f + 0] != c);

            h->b[c] -= s * h->pimpl->ratio->psum[i];
        }

        h->b[c] += cq->voldiscr[c];
    }

    /* Compressible accumulation term (lhs and rhs) */
    compr_accum_term(nc, dt, porevol, cq->totcompr, h->pimpl->accum);
    compr_src_add_press_accum(nc, cpress0, h->pimpl->accum, h->b);

    /* Compressible well contributions */
    if ((W != NULL) && (wdata != NULL)) {
        nconn = W->well_connpos[ W->number_of_wells ];

        set_dynamic_trans_well(W, cq->nphases, wdata, h->pimpl->ratio);
        compute_densrat_update_well(W, wdata, cq, h->pimpl->ratio,
                                    h->pimpl->masstrans_p);
        memcpy(h->pimpl->wtrans,
               h->pimpl->ratio->psum, nconn * sizeof *h->pimpl->wtrans);

        set_dynamic_grav_well(W, cq->nphases, wdata, h->pimpl->ratio);
        compute_densrat_update_well(W, wdata, cq, h->pimpl->ratio,
                                    h->pimpl->gravtrans_p);
        memcpy(h->pimpl->wgpot,
               h->pimpl->ratio->psum, nconn * sizeof *h->pimpl->wgpot);
    }
}


/* ---------------------------------------------------------------------- */
static void
assemble_cell_contrib(struct UnstructuredGrid *G  ,
                      const double            *src,
                      struct cfs_tpfa_data    *h  )
/* ---------------------------------------------------------------------- */
{
    int c1, c2, c, i, f, j1, j2;

    const double *ctrans = h->pimpl->ctrans;

    for (c = i = 0; c < G->number_of_cells; c++) {
        j1 = csrmatrix_elm_index(c, c, h->A);

        for (; i < G->cell_facepos[c + 1]; i++) {
            f = G->cell_faces[i];

            c1 = G->face_cells[2*f + 0];
            c2 = G->face_cells[2*f + 1];

            c2 = (c1 == c) ? c2 : c1;

            if (c2 >= 0) {
                j2 = csrmatrix_elm_index(c, c2, h->A);

                h->A->sa[j1] += ctrans[i];
                h->A->sa[j2] -= ctrans[i];
            }
        }

        h->b[c] += src[c];

        /* Compressible accumulation term */
        h->A->sa[j1] += h->pimpl->accum[c];
    }
}


/* ---------------------------------------------------------------------- */
static int
assemble_bc_contrib(struct UnstructuredGrid       *G  ,
                    struct FlowBoundaryConditions *fbc,
                    struct cfs_tpfa_data          *h  )
/* ---------------------------------------------------------------------- */
{
    int    c1, c2, c, f, hf;
    int    is_neumann;

    size_t p, j;

    const double *ctrans = h->pimpl->ctrans;

    is_neumann = 1;

    for (p = 0; p < fbc->nbc; p++) {
        f  = fbc->face[ p ];
        c1 = G->face_cells[2*f + 0];
        c2 = G->face_cells[2*f + 1];

        assert ((c1 < 0) ^ (c2 < 0));

        c  = (c1 >= 0) ? c1 : c2;

        if (fbc->type[ p ] == BC_PRESSURE) {
            is_neumann = 0;

            hf = h->pimpl->f2hf[2*f + (c1 < 0)];

            j  = csrmatrix_elm_index(c, c, h->A);

            h->A->sa[ j ] += ctrans[ hf ];
            h->b    [ c ] += ctrans[ hf ] * fbc->value[ p ];
        }

        /* Other boundary condition types not handled. */
    }

    return is_neumann;
}


/* ---------------------------------------------------------------------- */
static int
assemble_well_contrib(size_t                nc,
                      well_t               *W,
                      well_control_t       *wctrl,
                      struct cfs_tpfa_data *h)
/* ---------------------------------------------------------------------- */
{
    int c, i, w;
    int is_neumann, is_bhp;

    size_t jc, jw;

    double wtrans, dp;

    is_neumann = 1;

    for (w = i = 0; w < W->number_of_wells; w++) {
        is_bhp = wctrl->ctrl[w] == BHP;

        for (; i < W->well_connpos[w + 1]; i++) {
            c = W->well_cells[i];

            wtrans = h->pimpl->wtrans[i]; /* WI * sum((Ac\Ap)*[pmob] */
            dp     = h->pimpl->wgpot [i]; /* WI * sum((Ac\Ap)*[pmob]'*dP */

            if (is_bhp) {
                h->b[0  + c] += dp + wtrans * wctrl->target[w];
                h->b[nc + w] +=      wtrans * wctrl->target[w];
            } else {
                jc = csrmatrix_elm_index(c, nc + w, h->A);
                h->A->sa[jc] -= wtrans;
                h->b    [ c] += dp;

                jc = csrmatrix_elm_index(nc + w, c, h->A);
                h->A->sa[jc] -= wtrans;
                h->b[nc + w] -= dp;
            }

            jc = csrmatrix_elm_index(0  + c, 0  + c, h->A);
            jw = csrmatrix_elm_index(nc + w, nc + w, h->A);

            h->A->sa[jc] += wtrans;
            h->A->sa[jw] += wtrans;
        }

        is_neumann = is_neumann && (! is_bhp);

        if (! is_bhp) {
            /* Enforce total (reservoir volume) rate constraint. */
            h->b[nc + w] += wctrl->target[w];
        }
    }

    return is_neumann;
}


/* ---------------------------------------------------------------------- */
static void
compute_fpress(struct UnstructuredGrid       *G,
               struct FlowBoundaryConditions *fbc,
               int           np,
               const double *htrans,
               const double *pmobf,
               const double *gravcap_f,
               const double *cpress,
               const double *fflux,
               double       *fpress,
               double       *scratch_f)
/* ---------------------------------------------------------------------- */
{
    int    c, i, f;

    /* Suppress warning about unused parameters. */
    (void) np;  (void) pmobf;  (void) gravcap_f;  (void) fflux;

    /*
     * Define face pressures as weighted average of connecting cell
     * pressures.  Specifically, we define
     *
     *     pf = (t1 p1 + t2 p2) / (t1 + t2)
     *
     * in which t1 and t2 are the one-sided transmissibilities and p1
     * and p2 are the associated cell pressures.
     *
     * NOTE: The formula does not account for effects of gravity or
     * flux boundary conditions.
     */
    for (f = 0; f < G->number_of_faces; f++) {
        scratch_f[f] = fpress[f] = 0.0;
    }

    for (c = i = 0; c < G->number_of_cells; c++) {
        for (; i < G->cell_facepos[c + 1]; i++) {
            f = G->cell_faces[i];
            scratch_f[f] += htrans[i];
            fpress[f]    += htrans[i] * cpress[c];
        }
    }

    for (f = 0; f < G->number_of_faces; f++) {
        fpress[f] /= scratch_f[f];
    }

    if (fbc != NULL) {
        for (i = 0; ((size_t) i) < fbc->nbc; i++) {
            if (fbc->type[ i ] == BC_PRESSURE) {
                fpress[ fbc->face[ i ] ] = fbc->value[ i ];
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
compute_flux(struct UnstructuredGrid       *G,
             struct FlowBoundaryConditions *bc,
             int           np,
             const double *trans,
             const double *pmobf,
             const double *gravcap_f,
             const double *cpress,
             double       *fflux)
/* ---------------------------------------------------------------------- */
{
    int    f, c1, c2, p, i;
    double t, dp, g;

    for (f = 0; f < G->number_of_faces; f++) {
        c1 = G->face_cells[2*f + 0];
        c2 = G->face_cells[2*f + 1];

        if ((c1 < 0) || (c2 < 0)) {
            continue;
        }

        t = g = 0.0;
        for (p = 0; p < np; p++) {
            t += pmobf[f*np + p];
            g += pmobf[f*np + p] * gravcap_f[f*np + p];
        }

        dp = cpress[c1] - cpress[c2];

        fflux[f] = trans[f] * (t*dp + g);
    }

    if (bc != NULL) {
        for (i = 0; ((size_t) i) < bc->nbc; i++) {
            f = bc->face[ i ];

            if (bc->type[ i ] == BC_FLUX_TOTVOL) {
                t = 2*(G->face_cells[2*f + 0] < 0) - 1.0;

                fflux[ f ] = t * bc->value[ i ];
            }
            else if (bc->type[ i ] == BC_PRESSURE) {
                c1 = G->face_cells[2*f + 0];
                c2 = G->face_cells[2*f + 1];

                t = g = 0.0;
                for (p = 0; p < np; p++) {
                    t += pmobf[f*np + p];
                    g += pmobf[f*np + p] * gravcap_f[f*np + p];
                }

                if (c1 < 0) {
                    dp = bc->value[ i ] - cpress[c1];
                }
                else {
                    dp = cpress[c2] - bc->value[ i ];
                }

                fflux[f] = trans[f] * (t*dp + g);
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
compute_wflux(well_t                 *W,
              size_t                  np,
              struct completion_data *wdata,
              const double           *cpress,
              const double           *wpress,
              double                 *wflux)
/* ---------------------------------------------------------------------- */
{
    int    c, i, w;
    size_t p;
    double dp;

    double *pmob, *gpot;

    pmob = wdata->phasemob;
    gpot = wdata->gpot;

    for (w = i = 0; w < W->number_of_wells; w++) {
        for (; i < W->well_connpos[w + 1]; i++) {
            c = W->well_cells[i];

            dp = wpress[w] - cpress[c];

            wflux[i] = 0.0;
            for (p = 0; p < np; p++) {
                wflux[i] += pmob[i*np + p] * (dp + gpot[i*np + p]);
            }

            wflux[i] *= wdata->WI[i];
        }
    }
}


/* ---------------------------------------------------------------------- */
static int
is_incompr(int nc, struct compr_quantities *cq)
/* ---------------------------------------------------------------------- */
{
    int c, incompr;

    for (c = 0, incompr = 1; (c < nc) && incompr; ++c) {
        incompr = cq->totcompr[c] == 0.0;
    }

    return incompr;
}


/* ---------------------------------------------------------------------- */
static void
compute_f2hf_mapping(struct UnstructuredGrid *G   ,
                     struct cfs_tpfa_data    *data)
/* ---------------------------------------------------------------------- */
{
    int c, nc, i, f, p;

    for (i = 0; i < 2 * G->number_of_faces; i++) {
        data->pimpl->f2hf[ i ] = -1;
    }

    for (c = i = 0, nc = G->number_of_cells; c < nc; c++) {
        for (; i < G->cell_facepos[c + 1]; i++) {
            f = G->cell_faces[ i ];
            p = G->cell_faces[2*f + 0] != c;

            data->pimpl->f2hf[2*f + p] = i;
        }
    }
}



/* ======================================================================
 * Public interface below separator.
 * ====================================================================== */

/* ---------------------------------------------------------------------- */
struct cfs_tpfa_data *
cfs_tpfa_construct(struct UnstructuredGrid *G, well_t *W, int nphases)
/* ---------------------------------------------------------------------- */
{
    size_t                nc, nf, ngconn, nwconn;
    struct cfs_tpfa_data *new;

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        new->pimpl = impl_allocate(G, W, nphases);
        new->A     = construct_matrix(G, W);

        if ((new->pimpl == NULL) || (new->A == NULL)) {
            cfs_tpfa_destroy(new);
            new = NULL;
        }
    }

    if (new != NULL) {
        nc     = G->number_of_cells;
        nf     = G->number_of_faces;
        ngconn = G->cell_facepos[nc];
        nwconn = 0;

        if (W != NULL) {
            nwconn = W->well_connpos[ W->number_of_wells ];
        }

        compute_f2hf_mapping(G, new);

        /* Allocate linear system components */
        new->b                  = new->pimpl->ddata       + 0;
        new->x                  = new->b                  + new->A->m;

        /* Allocate reservoir components */
        new->pimpl->ctrans      = new->x                  + new->A->m;
        new->pimpl->accum       = new->pimpl->ctrans      + ngconn;
        new->pimpl->masstrans_f = new->pimpl->accum       + nc;
        new->pimpl->gravtrans_f = new->pimpl->masstrans_f + (nphases * nf);

        /* Allocate well components */
        new->pimpl->wtrans      = new->pimpl->gravtrans_f + (nphases * nf);
        new->pimpl->wgpot       = new->pimpl->wtrans      + nwconn;
        new->pimpl->masstrans_p = new->pimpl->wgpot       + nwconn;
        new->pimpl->gravtrans_p = new->pimpl->masstrans_p + (nphases * nwconn);

        /* Allocate scratch array for face pressure calculations */
        new->pimpl->scratch_f   = new->pimpl->gravtrans_p + (nphases * nwconn);
    }

    return new;
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_assemble(struct UnstructuredGrid                  *G,
                  double                   dt,
                  well_t                  *W,
                  struct FlowBoundaryConditions *bc,
                  const double            *src,
                  struct compr_quantities *cq,
                  const double            *trans,
                  const double            *gravcap_f,
                  well_control_t          *wctrl,
                  struct completion_data  *wdata,
                  const double            *cpress0,
                  const double            *porevol,
                  struct cfs_tpfa_data    *h)
/* ---------------------------------------------------------------------- */
{
    int res_is_neumann, well_is_neumann;

    csrmatrix_zero(         h->A);
    vector_zero   (h->A->m, h->b);

    compute_psys_contrib(G, W, wdata, bc, cq, dt,
                         trans, gravcap_f, cpress0, porevol, h);

    res_is_neumann = 1;

    assemble_cell_contrib(G, src, h);

    if (bc != NULL) {
        res_is_neumann = assemble_bc_contrib(G, bc, h);
    }

    if ((W != NULL) && (wctrl != NULL)) {
        assert (wdata != NULL);
        well_is_neumann = assemble_well_contrib(G->number_of_cells,
                                                W, wctrl, h);
    } else {
        well_is_neumann = 1;
    }

    if (res_is_neumann && well_is_neumann &&
        is_incompr(G->number_of_cells, cq)) {
        h->A->sa[0] *= 2;
    }
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_press_flux(struct UnstructuredGrid                 *G,
                    struct FlowBoundaryConditions *bc,
                    well_t                 *W,
                    int                     np,
                    const double           *trans,
                    const double           *pmobf,
                    const double           *gravcap_f,
                    struct completion_data *wdata,
                    struct cfs_tpfa_data   *h,
                    double                 *cpress,
                    double                 *fflux,
                    double                 *wpress,
                    double                 *wflux)
/* ---------------------------------------------------------------------- */
{
    /* Assign cell pressure directly from solution vector */
    memcpy(cpress, h->x, G->number_of_cells * sizeof *cpress);

    compute_flux(G, bc, np, trans, pmobf, gravcap_f, cpress, fflux);

    if ((W != NULL) && (wdata != NULL)) {
        assert (wpress != NULL);
        assert (wflux  != NULL);

        /* Assign well BHP directly from solution vector */
        memcpy(wpress, h->x + G->number_of_cells,
               W->number_of_wells * sizeof *wpress);

        compute_wflux(W, np, wdata, cpress, wpress, wflux);
    }
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_fpress(struct UnstructuredGrid               *G,
                struct FlowBoundaryConditions *bc,
                int                   np,
                const double         *htrans,
                const double         *pmobf,
                const double         *gravcap_f,
                struct cfs_tpfa_data *h,
                const double         *cpress,
                const double         *fflux,
                double               *fpress)
/* ---------------------------------------------------------------------- */
{
    compute_fpress(G, bc, np, htrans, pmobf, gravcap_f,
                   cpress, fflux, fpress, h->pimpl->scratch_f);
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_retrieve_masstrans(struct UnstructuredGrid               *G,
                            int                   np,
                            struct cfs_tpfa_data *h,
                            double               *masstrans_f)
/* ---------------------------------------------------------------------- */
{
    memcpy(masstrans_f, h->pimpl->masstrans_f,
           np * G->number_of_faces * sizeof *masstrans_f);
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_retrieve_gravtrans(struct UnstructuredGrid               *G,
                            int                   np,
                            struct cfs_tpfa_data *h,
                            double               *gravtrans_f)
/* ---------------------------------------------------------------------- */
{
    memcpy(gravtrans_f, h->pimpl->gravtrans_f,
           np * G->number_of_faces * sizeof *gravtrans_f);
}


/* ---------------------------------------------------------------------- */
static double
cfs_tpfa_impes_maxtime_cell(int                      c,
                            struct UnstructuredGrid                  *G,
                            struct compr_quantities *cq,
                            const double            *trans,
                            const double            *porevol,
                            struct cfs_tpfa_data    *h,
                            const double            *dpmobf,
                            const double            *surf_dens,
                            const double            *gravity)
/* ---------------------------------------------------------------------- */
{
    /* Reference:
       K. H. Coats, "IMPES Stability: The Stable Step", SPE 69225

       Capillary pressure parts not included.
    */

    int i, j, k, f, c2;
    double f11, f12, f21, f22;
    double dp, dzg, tr, tmob, detF, eqv_flux;
    const double *pmob;
    const double *A;
    /* This is intended to be compatible with the dune-porsol blackoil
       code. This library is otherwise not depending on particular
       orderings of phases or components, so at some point this
       function should be generalized. */
    enum { Water = 0, Oil = 1, Gas = 2 };
    enum { num_phases = 3 };
    double rho[num_phases];
    double pot[num_phases];
    /* Notation: dpmob[Oil][Water] is d/ds_w(lambda_o) */
    double dpmob[num_phases][num_phases]
        = { {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} };

    assert (cq->nphases == 3);

    f11 = f12 = f21 = f22 = 0.0;

    /* Loop over neighbour faces to accumulate f11, f12 etc. */
    for (i = G->cell_facepos[c]; i < G->cell_facepos[c + 1]; ++i) {
        f  = G->cell_faces[i];
        if ((c2 = G->face_cells[2*f + 0]) == c) {
            c2  = G->face_cells[2*f + 1];
        }

        /* Initially only interior faces */
        if (c2 < 0) {
            continue;
        }

        /* Computing density */
        A = cq->Af + f*(cq->nphases)*(cq->nphases);
        for (j = 0; j < cq->nphases; ++j) {
            rho[j] = 0.0;
            for (k = 0; k < cq->nphases; ++k) {
                rho[j] += A[cq->nphases*j + k]*surf_dens[k];
            }
        }
        /* Computing gravity potentials */
        dp = h->x[c] - h->x[c2];
        dzg = 0.0;
        for (j = 0; j < G->dimensions; ++j) {
            dzg += (G->cell_centroids[G->dimensions*c + j] - G->cell_centroids[G->dimensions*c2 + j])*gravity[j];
        }
        for (j = 0; j < cq->nphases; ++j) {
            pot[j] = fabs(dp - rho[j]*dzg);
        }
        /* Filling the dpmob array from available data.
           Note that we only need the following combinations:
           (Water, Water)
           (Water, Gas)
           (Oil, Water)
           (Oil, Gas)
           (Gas, Gas)

           No derivatives w.r.t. Oil is needed, since there are only two
           independent saturation variables.

           The lack of (Gas, Water) may be due to assumptions on the
           three-phase model used (should be checked to be compatible
           with our choices).
        */
        dpmob[Water][Water] = dpmobf[9*f];
        dpmob[Water][Gas] = dpmobf[9*f + 2];
        dpmob[Oil][Water] = dpmobf[9*f + 3];
        dpmob[Oil][Gas] = dpmobf[9*f + 5];
        dpmob[Gas][Gas] = dpmobf[9*f + 8];
        /* Computing the flux parts f_ij */
        pmob = cq->phasemobf + f*cq->nphases;
        tr = trans[f];
        tmob = pmob[Water] + pmob[Oil] + pmob[Gas];
        f11 += tr*((pmob[Oil] + pmob[Gas])*dpmob[Water][Water]*pot[Water]
                   - pmob[Water]*dpmob[Oil][Water]*pot[Oil])/tmob;
        f12 += -tr*(pmob[Water]*dpmob[Oil][Gas]*pot[Oil]
                    + pmob[Water]*dpmob[Gas][Gas]*pot[Gas]
                    - (pmob[Oil] + pmob[Gas])*dpmob[Water][Gas]*pot[Water])/tmob;
        f21 += -tr*(pmob[Gas]*dpmob[Water][Water]*pot[Water]
                    + pmob[Gas]*dpmob[Oil][Water]*pot[Oil])/tmob;
        f22 += tr*(-pmob[Gas]*dpmob[Oil][Gas]*pot[Oil]
                   + (pmob[Water] + pmob[Oil])*dpmob[Gas][Gas]*pot[Gas]
                   - pmob[Gas]*dpmob[Water][Gas]*pot[Water])/tmob;
    }

    /* (from eq. 3, 4a-e, 5a-c)
       F_i = 1/2 |f11_i + f22_i + \sqrt{G}|
       G = (f11_i + f22_i)^2 - 4 det(F_i)
       fXX_i = \sum_j fXX_ij     (j runs over the neighbours)
       det(F_i) = f11_i f22_i - f12_i f21_i
    */
    detF = f11*f22 - f12*f21;
    eqv_flux = 0.5*fabs(f11 + f22 + sqrt((f11 + f22)*(f11 + f22) - 4*detF));
    /* delta_t < porevol/eqv_flux */
    return porevol[c]/eqv_flux;
}

/* ---------------------------------------------------------------------- */
double
cfs_tpfa_impes_maxtime(struct UnstructuredGrid                  *G,
                       struct compr_quantities *cq,
                       const double            *trans,
                       const double            *porevol,
                       struct cfs_tpfa_data    *h,
                       const double            *dpmobf,
                       const double            *surf_dens,
                       const double            *gravity)
/* ---------------------------------------------------------------------- */
{
    int c;
    double max_dt, cell_dt;
    max_dt = 1e100;
    for (c = 0; c < G->number_of_cells; ++c) {
        cell_dt = cfs_tpfa_impes_maxtime_cell(c, G, cq, trans, porevol, h,
                                              dpmobf, surf_dens, gravity);
        if (cell_dt < max_dt) {
            max_dt = cell_dt;
        }
    }
    return max_dt;
}



/* ---------------------------------------------------------------------- */
void
cfs_tpfa_expl_mass_transport(struct UnstructuredGrid                 *G,
                             well_t                 *W,
                             struct completion_data *wdata,
                             int                     np,
                             double                  dt,
                             const double           *porevol,
                             struct cfs_tpfa_data   *h,
                             double                 *surf_vol)
/* ---------------------------------------------------------------------- */
{
    int    c, i, f, c2, p, w;
    double dp, dz, gsgn;
    const double *masstrans_f, *gravtrans_f, *masstrans_p, *gravtrans_p;
    const double *cpress, *wpress;

    /* Suppress warning about unused parameter. */
    (void) wdata;

    masstrans_f = h->pimpl->masstrans_f;
    gravtrans_f = h->pimpl->gravtrans_f;
    masstrans_p = h->pimpl->masstrans_p;
    gravtrans_p = h->pimpl->gravtrans_p;
    cpress = h->x;
    wpress = h->x + G->number_of_cells;

    /* Transport through interior faces */
    for (c = i = 0; c < G->number_of_cells; c++) {
        for (; i < G->cell_facepos[c + 1]; i++) {
            f  = G->cell_faces[i];

            if ((c2 = G->face_cells[2*f + 0]) == c) {
              gsgn = 1.0;
                c2  = G->face_cells[2*f + 1];
            } else {
              gsgn = -1.0;
            }

            if (c2 >= 0) {
                dp = cpress[c] - cpress[c2];

                for (p = 0; p < np; p++) {
                    dz  = masstrans_f[f*np + p] * dp;
                    dz += gravtrans_f[f*np + p] * gsgn;

                    /* dz > 0 => flow from c into c2. */
                    surf_vol[c*np + p] -= dz * dt / porevol[c];
                }
            }
        }
    }

    /* Transport through well perforations */
    if (W != NULL) {
        for (w = i = 0; w < W->number_of_wells; w++) {
            for (; i < W->well_connpos[w + 1]; i++) {
                c = W->well_cells[i];
                dp = wpress[w] - cpress[c];

                for (p = 0; p < np; p++) {
                    dz = masstrans_p[i*np + p] * dp;
                    dz += gravtrans_p[i*np + p];

                    /* dz > 0 => flow from perforation into c. */
                    surf_vol[c*np + p] += dz * dt / porevol[c];
                }
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_destroy(struct cfs_tpfa_data *h)
/* ---------------------------------------------------------------------- */
{
    if (h != NULL) {
        csrmatrix_delete(h->A);
        impl_deallocate (h->pimpl);
    }

    free(h);
}
