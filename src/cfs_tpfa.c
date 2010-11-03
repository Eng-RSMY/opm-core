#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "blas_lapack.h"

#include "cfs_tpfa.h"
#include "sparse_sys.h"


struct cfs_tpfa_impl {
    double *fgrav;              /* Accumulated grav contrib/face */

    /* Linear storage */
    double *ddata;
};


/* ---------------------------------------------------------------------- */
static void
impl_deallocate(struct cfs_tpfa_impl *pimpl)
/* ---------------------------------------------------------------------- */
{
    if (pimpl != NULL) {
        free(pimpl->ddata);
    }

    free(pimpl);
}


/* ---------------------------------------------------------------------- */
static struct cfs_tpfa_impl *
impl_allocate(grid_t *G)
/* ---------------------------------------------------------------------- */
{
    struct cfs_tpfa_impl *new;

    size_t ddata_sz;

    ddata_sz  = 2 * G->number_of_cells;                /* b, x */
    ddata_sz += 1 * G->number_of_faces;                /* fgrav */

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        new->ddata = malloc(ddata_sz * sizeof *new->ddata);

        if (new->ddata == NULL) {
            impl_deallocate(new);
            new = NULL;
        }
    }

    return new;
}


/* ---------------------------------------------------------------------- */
static struct CSRMatrix *
cfs_tpfa_construct_matrix(grid_t *G)
/* ---------------------------------------------------------------------- */
{
    int    f, c1, c2;
    size_t nnz;

    struct CSRMatrix *A;

    A = csrmatrix_new_count_nnz(G->number_of_cells);

    if (A != NULL) {
        /* Self connections */
        for (c1 = 0; c1 < G->number_of_cells; c1++) {
            A->ia[ c1 + 1 ] = 1;
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

        nnz = csrmatrix_new_elms_pushback(A);
        if (nnz == 0) {
            csrmatrix_delete(A);
            A = NULL;
        }
    }

    if (A != NULL) {
        /* Fill self connections */
        for (c1 = 0; c1 < G->number_of_cells; c1++) {
            A->ja[ A->ia[ c1 + 1 ] ++ ] = c1;
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

        /* The tpfa matrix is square */
        A->n = A->m;

        assert ((size_t) A->ia[ G->number_of_cells ] == nnz);

        /* Guarantee sorted rows */
        csrmatrix_sortrows(A);
    }

    return A;
}


/* ---------------------------------------------------------------------- */
static void
solve_cellsys_core(grid_t       *G   ,
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



/* ======================================================================
 * Public interface below separator.
 * ====================================================================== */

/* ---------------------------------------------------------------------- */
struct cfs_tpfa_data *
cfs_tpfa_construct(grid_t *G)
/* ---------------------------------------------------------------------- */
{
    struct cfs_tpfa_data *new;

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        new->pimpl = impl_allocate(G);
        new->A     = cfs_tpfa_construct_matrix(G);

        if ((new->pimpl == NULL) || (new->A == NULL)) {
            cfs_tpfa_destroy(new);
            new = NULL;
        }
    }

    if (new != NULL) {
        new->b = new->pimpl->ddata;
        new->x = new->b             + new->A->m;

        new->pimpl->fgrav = new->x  + new->A->m;
    }

    return new;
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_assemble(grid_t               *G,
                  const double         *ctrans,
                  const double         *P,
                  const double         *src,
                  struct cfs_tpfa_data *h)
/* ---------------------------------------------------------------------- */
{
    int c1, c2, c, i, f, j1, j2;

    double s;

    csrmatrix_zero(         h->A);
    vector_zero   (h->A->m, h->b);

    for (c = i = 0; c < G->number_of_cells; c++) {
        j1 = csrmatrix_elm_index(c, c, h->A);

        for (; i < G->cell_facepos[c + 1]; i++) {
            f = G->cell_faces[i];

            c1 = G->face_cells[2*f + 0];
            c2 = G->face_cells[2*f + 1];

            s  = 2.0*(c1 == c) - 1.0;
            c2 = (c1 == c) ? c2 : c1;

            if (c2 >= 0) {
                j2 = csrmatrix_elm_index(c, c2, h->A);

                h->A->sa[j1] += ctrans[i];
                h->A->sa[j2] -= ctrans[i];
            }
        }

        h->b[c] += src[c];

        /* Compressible accumulation term */
        h->A->sa[j1] += P[c];
    }

    h->A->sa[0] *= 2;
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_press_flux(grid_t               *G,
                    const double         *trans,
                    struct cfs_tpfa_data *h,
                    double               *cpress,
                    double               *fflux)
/* ---------------------------------------------------------------------- */
{
    int c1, c2, f;

    /* Assign cell pressure directly from solution vector */
    memcpy(cpress, h->x, G->number_of_cells * sizeof *cpress);

    for (f = 0; f < G->number_of_faces; f++) {
        c1 = G->face_cells[2*f + 0];
        c2 = G->face_cells[2*f + 1];

        if ((c1 >= 0) && (c2 >= 0)) {
            fflux[f] = trans[f] * (cpress[c1] - cpress[c2]);
        } else {
            fflux[f] = 0.0;
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


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_small_matvec(size_t n, int sz,
                      const double *A,
                      const double *X,
                      double       *Y)
/* ---------------------------------------------------------------------- */
{
    size_t i, p1, p2;

    MAT_SIZE_T nrows, ncols, ld, incx, incy;
    double     a1, a2;

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
int
cfs_tpfa_solve_cellsys(grid_t       *G ,
                       size_t        sz,
                       const double *Ac,
                       const double *bf,
                       double       *xcf)
/* ---------------------------------------------------------------------- */
{
    int     ret;
    double *luAc;

    MAT_SIZE_T *ipiv;

    luAc = malloc(sz * sz * sizeof *luAc);
    ipiv = malloc(sz      * sizeof *ipiv);

    if ((luAc != NULL) && (ipiv != NULL)) {
        solve_cellsys_core(G, sz, Ac, bf, xcf, luAc, ipiv);
        ret = 1;
    } else {
        ret = 0;
    }

    free(ipiv);  free(luAc);

    return ret;
}


/* ---------------------------------------------------------------------- */
void
cfs_tpfa_sum_phase_contrib(grid_t       *G  ,
                           size_t        sz ,
                           const double *xcf,
                           double       *sum)
/* ---------------------------------------------------------------------- */
{
    int    c, i;
    size_t j;

    const double *v;

    for (c = i = 0, v = xcf; c < G->number_of_cells; c++, v += sz) {
        for (; i < G->cell_facepos[c + 1]; i++) {

            sum[i] = 0.0;
            for (j = 0; j < sz; j++) {
                sum[i] += v[j];
            }
        }
    }
}
