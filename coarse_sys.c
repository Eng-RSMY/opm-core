/*
 * Copyright (c) 2010 SINTEF ICT, Applied Mathemathics
 */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>


#ifndef DEBUG_OUTPUT
#define DEBUG_OUTPUT 0
#endif

#if DEBUG_OUTPUT
#include <stdio.h>
#endif


#include "blas_lapack.h"
#include "coarse_conn.h"
#include "coarse_sys.h"
#include "hybsys.h"
#include "hybsys_global.h"
#include "mimetic.h"
#include "partition.h"
#include "sparse_sys.h"


#if defined(MAX)
#undef MAX
#endif
#define MAX(a,b)  (((a) > (b)) ? (a) : (b))


/* ======================================================================
 * Data structures
 * ====================================================================== */

struct coarse_sys_meta {
    size_t max_ngconn;          /* max(diff(face_pos)) */
    size_t sum_ngconn2;         /* sum(diff(face_pos)^2) */

    size_t max_blk_cells;       /* max(accumarray(p,1)) */
    size_t max_blk_nhf;         /* max(accumarray(p,diff(face_pos))) */
    size_t max_blk_nintf;       /* Maximum # block internal faces */
    size_t max_blk_sum_nhf2;    /* max_i \sum_{j\in\Omega_i} ncf(j)^2 */
    size_t max_cf_nf;           /* Maximum # fs faces in a coarse face */
    size_t n_act_bf;            /* Number of active coarse connections */

    int *blk_nhf;               /* Number of fs hfaces per block */
    int *blk_nintf;             /* Number of internal fs faces per block */

    int *loc_fno;               /* Local (fs) face numbering */
    int *ncf;                   /* diff(face_pos) */
    int *pconn2;                /* cumsum([0; diff(face_pos).^2]) */

    int *pb2c, *b2c;            /* Block->cell mapping (CSR packed) */

    int *bfno;                  /* Active basis function numbering */
    int *loc_dofno;             /* Block-local DOF number of a CF */

    /* -------------------------------------------------------------- */
    int *data;                  /* Linear storage.  Don't touch. */
};


struct bf_asm_data {
    struct hybsys *fsys;        /* Fine-scale hybrid system contributions */

    struct CSRMatrix *A;        /* BF coefficient matrix */
    double           *b;        /* BF system RHS */
    double           *x;        /* BF system solution */

    double           *v;        /* BF (hf) flux.  Symmetrised */
    double           *p;        /* BF pressure. */

    double           *gpress;   /* BF gravity contrib. (== 0) */

    int              *pdof;     /* Indirection pointer to linearised DOF */
    int              *dof;      /* Linearised DOFs per BF */

    /* -------------------------------------------------------------- */
    int    *idata;              /* Linear (integer) storage */
    double *ddata;              /* Linear (double) storage */
};


/* ======================================================================
 * Memory management
 * ====================================================================== */


/* ---------------------------------------------------------------------- */
static void
coarse_sys_meta_destroy(struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    if (m != NULL) {
        free(m->data);
    }

    free(m);
}


/* ---------------------------------------------------------------------- */
struct coarse_sys_meta *
coarse_sys_meta_allocate(size_t nblocks, size_t nfaces_c,
                         size_t nc     , size_t nfaces_f)
/* ---------------------------------------------------------------------- */
{
    size_t                  alloc_sz;
    struct coarse_sys_meta *new;

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        alloc_sz  = nblocks;     /* blk_nhf */
        alloc_sz += nblocks;     /* blk_nintf */
        alloc_sz += nc;          /* ncf */
        alloc_sz += nc + 1;      /* pconn2 */
        alloc_sz += nfaces_f;    /* loc_fno */
        alloc_sz += nblocks + 1; /* pb2c */
        alloc_sz += nc;          /* b2c */
        alloc_sz += nfaces_c;    /* bfno */
        alloc_sz += 2*nfaces_c;  /* loc_dofno */

        new->data = calloc(alloc_sz, sizeof *new->data);

        if (new->data == NULL) {
            coarse_sys_meta_destroy(new);

            new = NULL;
        } else {
            new->blk_nhf   = new->data;
            new->blk_nintf = new->blk_nhf   + nblocks;
            new->ncf       = new->blk_nintf + nblocks;
            new->pconn2    = new->ncf       + nc;
            new->loc_fno   = new->pconn2    + nc + 1;

            new->pb2c      = new->loc_fno   + nfaces_f;
            new->b2c       = new->pb2c      + nblocks + 1;

            new->bfno      = new->b2c       + nc;
            new->loc_dofno = new->bfno      + nfaces_c;
        }
    }

    return new;
}


/* ---------------------------------------------------------------------- */
static void
bf_asm_data_deallocate(struct bf_asm_data *data)
/* ---------------------------------------------------------------------- */
{
    if (data != NULL) {
        free            (data->ddata);
        free            (data->idata);
        csrmatrix_delete(data->A);
        hybsys_free     (data->fsys);
    }

    free(data);
}


/* ---------------------------------------------------------------------- */
static struct bf_asm_data *
bf_asm_data_allocate(grid_t                 *g,
                     struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    size_t              max_nhf, max_cells, max_faces, nnz;
    size_t              alloc_sz;
    struct bf_asm_data *new;

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        max_nhf   = 2 * m->max_blk_nhf;
        max_cells = 2 * m->max_blk_cells;
        max_faces = 2 * m->max_blk_nintf + m->max_cf_nf;
        nnz       = 2 * m->max_blk_sum_nhf2;

        new->fsys = hybsys_allocate_symm((int) m->max_ngconn,
                                         g->number_of_cells,
                                         g->cell_facepos[g->number_of_cells]);

        new->A = csrmatrix_new_known_nnz(max_faces, nnz);

        alloc_sz   = max_cells + 1; /* pdof */
        alloc_sz  += max_nhf;       /* dof */

        new->idata = malloc(alloc_sz * sizeof *new->idata);

        alloc_sz   = 2 * max_faces; /* b, x */
        alloc_sz  += 1 * max_nhf;   /* v */
        alloc_sz  += 1 * max_cells; /* p */

        /* gpress */
        alloc_sz  += g->cell_facepos[ g->number_of_cells ];

        new->ddata = malloc(alloc_sz * sizeof *new->ddata);

        if ((new->fsys  == NULL) || (new->A     == NULL) ||
            (new->idata == NULL) || (new->ddata == NULL)) {
            bf_asm_data_deallocate(new);
            new = NULL;
        } else {
            new->pdof = new->idata;
            new->dof  = new->pdof   + max_cells + 1;

            new->b    = new->ddata;
            new->x    = new->b      + max_faces;
            new->v    = new->x      + max_faces;
            new->p    = new->v      + max_nhf;

            new->gpress = new->p    + max_cells;

            hybsys_init((int) m->max_ngconn, new->fsys);
        }
    }

    return new;
}


/* ======================================================================
 * Utilities.
 * ====================================================================== */


/* max(diff(p(1:n))) */
/* ---------------------------------------------------------------------- */
static int
max_diff(int n, int *p)
/* ---------------------------------------------------------------------- */
{
    int i, d, ret;

    assert ((n > 0) && (p != NULL));

    ret = p[1] - p[0];               assert (ret >= 0);
    for (i = 1; i < n; i++) {
        d   = p[i + 1] - p[i];
        ret = (d > ret) ? d : ret;
    }

    return ret;
}


/* Enumerate active basis functions/coarse connetions according to
 * block proximity.
 *
 * Returns number of active connections. */
/* ---------------------------------------------------------------------- */
static int
enumerate_active_bf(struct coarse_topology *ct,
                    struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    int act, cf, b_in, b_out, b1, b2, p;

    memset(m->bfno, -1, ct->nfaces * sizeof *m->bfno);

    act = 0;

    for (b_in = p = 0; b_in < ct->nblocks; b_in++) {
        for (; p < ct->blkfacepos[b_in + 1]; p++) {
            cf = ct->blkfaces[p];

            if (m->bfno[cf] < 0) { /* Previously undiscovered */
                b1 = ct->neighbours[2*cf + 0];
                b2 = ct->neighbours[2*cf + 1];

                assert (b1 != b2);

                b_out = (b1 == b_in) ? b2 : b1;

                if (b_out >= 0) {
                    m->bfno[cf] = act++;
                }
            }
        }
    }

    return act;
}


/* ---------------------------------------------------------------------- */
static void
compute_loc_dofno(struct coarse_topology *ct,
                  struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    int b, nb, p, cf, locno, col_off;

    memset(m->loc_dofno, -1, 2 * ct->nfaces * sizeof *m->loc_dofno);

    nb = ct->nblocks;

    for (b = p = 0; b < nb; b++) {
        locno = 0;

        for (; p < ct->blkfacepos[b + 1]; p++) {
            cf = ct->blkfaces[p];

            if (m->bfno[cf] >= 0) {
                col_off = 1 - (ct->neighbours[2*cf + 0] == b);

                assert (m->loc_dofno[2*cf + col_off] == -1);
                
                m->loc_dofno[2*cf + col_off] = locno++;
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
coarse_sys_meta_fill(int nc, const int *pgconn,
                     size_t nneigh, const int *neigh,
                     const int *p,
                     struct coarse_topology *ct,
                     struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    int    c1, b1, c2, b2, i, n;
    size_t f, blk_sum_nhf2;

    m->max_blk_nhf = m->max_blk_nintf = 0;

    for (f = 0; f < nneigh; f++) {
        c1 = neigh[2*f + 0];   b1 = (c1 >= 0) ? p[c1] : -1;
        c2 = neigh[2*f + 1];   b2 = (c2 >= 0) ? p[c2] : -1;

        assert ((b1 >= 0) || (b2 >= 0));

        if (b1 == b2) {
            m->blk_nintf[b1] += 1;
            m->max_blk_nintf  = MAX(m->max_blk_nintf,
                                    (size_t) m->blk_nintf[b1]);
        }

        if (b1 >= 0) {
            m->blk_nhf[b1] += 1;
            m->max_blk_nhf  = MAX(m->max_blk_nhf,
                                  (size_t) m->blk_nhf[b1]);
        }

        if (b2 >= 0) {
            m->blk_nhf[b2] += 1;
            m->max_blk_nhf  = MAX(m->max_blk_nhf,
                                  (size_t) m->blk_nhf[b2]);
        }
    }

    memset(m->loc_fno, -1, nneigh * sizeof *m->loc_fno);

    m->max_cf_nf = 0;

    for (f = 0; f < (size_t) ct->nfaces; f++) {
        m->max_cf_nf = MAX(m->max_cf_nf,
                           (size_t) (ct->subfacepos[f + 1] -
                                     ct->subfacepos[f]));
    }

    m->max_ngconn = m->sum_ngconn2 = 0;
    for (c1 = 0; c1 < nc; c1++) {
        n = pgconn[c1 + 1] - pgconn[c1];

        m->max_ngconn   = MAX(m->max_ngconn, (size_t) n);
        m->sum_ngconn2 += n * n;

        m->ncf[c1]        = n;
        m->pconn2[c1 + 1] = m->pconn2[c1] + (n * n);
    }

    partition_invert(nc, p, m->pb2c, m->b2c);

    m->max_blk_cells = 0;
    for (b1 = 0; b1 < ct->nblocks; b1++) {
        m->max_blk_cells = MAX(m->max_blk_cells,
                               (size_t) (m->pb2c[b1 + 1] -
                                         m->pb2c[b1 + 0]));
    }

    m->max_blk_sum_nhf2 = 0;
    for (b1 = 0; b1 < ct->nblocks; b1++) {
        blk_sum_nhf2 = 0;

        for (i = m->pb2c[b1]; i < m->pb2c[b1 + 1]; i++) {
            c1 = m->b2c[i];
            blk_sum_nhf2 += m->pconn2[c1 + 1] - m->pconn2[c1];
        }

        m->max_blk_sum_nhf2 = MAX(m->max_blk_sum_nhf2, blk_sum_nhf2);
    }

    m->n_act_bf = enumerate_active_bf(ct, m);

    compute_loc_dofno(ct, m);
}


/* ---------------------------------------------------------------------- */
static struct coarse_sys_meta *
coarse_sys_meta_construct(grid_t *g, const int *p,
                          struct coarse_topology *ct)
/* ---------------------------------------------------------------------- */
{
    struct coarse_sys_meta *m;

    m = coarse_sys_meta_allocate(ct->nblocks, ct->nfaces,
                                 g->number_of_cells, g->number_of_faces);

    if (m != NULL) {
        coarse_sys_meta_fill(g->number_of_cells,
                             g->cell_facepos,
                             g->number_of_faces,
                             g->face_cells, p, ct, m);
    }

    return m;
}


/* ---------------------------------------------------------------------- */
static double *
compute_fs_ip(grid_t *g, const double *perm,
              const struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    double *Binv;

    Binv = malloc(m->sum_ngconn2 * sizeof *Binv);

    if (Binv != NULL) {
        mim_ip_simple_all(g->number_of_cells, g->dimensions, m->max_ngconn,
                          m->ncf, g->cell_facepos, g->cell_faces,
                          g->face_cells, g->face_centroids, g->face_normals,
                          g->face_areas, g->cell_centroids, g->cell_volumes,
                          (double *) perm, Binv); /* const_cast<>() */
    }

    return Binv;
}


/* Create basis function weighting source term (unsigned) based on
 * trace of permeability.
 *
 * Returns valid pointer (one scalar per cell in underlying fine-scale
 * grid model) if succesful, and NULL otherwise. */
/* ---------------------------------------------------------------------- */
static double *
perm_weighting(size_t nc, size_t nd,
               const double *perm,
               const double *cvol)
/* ---------------------------------------------------------------------- */
{
    size_t c, d, off;
    double t;
    double *w;

    w = malloc(nc * sizeof *w);

    if (w != NULL) {
        for (c = off = 0; c < nc; c++, off += nd*nd) {
            t = 0.0;

            for (d = 0; d < nd; d++) {
                t += perm[off + d*(nd + 1)];
            }

            w[c] = t * cvol[c];
        }
    }

    return w;
}


/* Use prescribed sources if applicable (replace synthetic source term).
 *
 * Returns one (1) if successful (needs to allocate internal data) and
 * zero (0) otherwise. */
/* ---------------------------------------------------------------------- */
static int
enforce_explicit_source(size_t nc, size_t nb, const int *p,
                        const double *src, struct coarse_sys_meta *m,
                        double *w)
/* ---------------------------------------------------------------------- */
{
    int     i, ret;
    size_t  c, b;
    int    *has_src;

    ret     = 0;
    has_src = calloc(nb, sizeof *has_src);

    if (has_src != NULL) {
        for (c = 0; c < nc; c++) {
            has_src[p[c]] += fabs(src[c]) > 0.0;
        }

        for (b = 0; b < nb; b++) {
            if (has_src[b]) {
                for (i = m->pb2c[b]; i < m->pb2c[b + 1]; i++) {
                    w[m->b2c[i]] = 0.0;
                }
            }
        }

        for (c = 0; c < nc; c++) {
            if (fabs(src[c]) > 0.0) {
                w[c] = src[c];
            }
        }

        ret = 1;
    }

    free(has_src);

    return ret;
}


/* Enforce \int_{\Omega_i} w(x) dx == 1 for all blocks, \Omega_i.
 *
 * Allocates internal work array.
 *
 * Returns one (1) if successful, and zero (0) otherwise. */
/* ---------------------------------------------------------------------- */
static int
normalize_weighting(size_t nc, size_t nb, const int *p, double *w)
/* ---------------------------------------------------------------------- */
{
    int     ret;
    size_t  c, b;
    double *bw;

    ret = 0;
    bw  = malloc(nb * sizeof *bw);

    if (bw != NULL) {
        for (b = 0; b < nb; b++) { bw[ b  ]  =  0.0; }
        for (c = 0; c < nc; c++) { bw[p[c]] += w[c]; }

        for (c = 0; c < nc; c++) {
            assert (fabs(bw[p[c]]) > 0.0);

            w[c] /= bw[p[c]];
        }

        ret = 1;
    }

    free(bw);

    return ret;
}


/* Create basis function weighting term (unsigned), one scalar per
 * grid cell in the underlying fine-scale grid.  Integral one per
 * block.  Satisfies any prescribed external source terms.
 *
 * Returns valid ponter if successful and NULL if not. */
/* ---------------------------------------------------------------------- */
static double *
coarse_weight(grid_t *g, size_t nb,
              const int              *p,
              struct coarse_sys_meta *m,
              const double           *perm, const double *src)
/* ---------------------------------------------------------------------- */
{
    int     ok;
    double *w;

    ok = 0;
    w  = perm_weighting(nb, g->dimensions, perm, g->cell_volumes);

    if (w != NULL) {
        ok = enforce_explicit_source(g->number_of_cells,
                                     nb, p, src, m, w);
    }

    if (ok) {
        ok = normalize_weighting(g->number_of_cells, nb, p, w);
    }

    if (!ok) { free(w);  w = NULL; }

    return w;
}


/* ---------------------------------------------------------------------- */
static int
blkdof_fill(struct coarse_topology *ct,
            struct coarse_sys_meta *m,
            struct coarse_sys      *sys)
/* ---------------------------------------------------------------------- */
{
    int    p, ret, dof;
    size_t b, nb;

    nb = ct->nblocks;

    sys->blkdof_pos = calloc(nb + 1, sizeof *sys->blkdof_pos);

    if (sys->blkdof_pos != NULL) {
        for (b = 0, p = 0; b < nb; b++) {
            for (; p < ct->blkfacepos[b + 1]; p++) {
                dof = m->bfno[ ct->blkfaces[p] ];

                sys->blkdof_pos[b + 1] += dof >= 0;
            }
        }

        for (b = 1; b <= nb; b++) {
            sys->blkdof_pos[0] += sys->blkdof_pos[b];
            sys->blkdof_pos[b]  = sys->blkdof_pos[0] - sys->blkdof_pos[b];
        }

        sys->blkdof = malloc(sys->blkdof_pos[0] * sizeof *sys->blkdof);

        if (sys->blkdof == NULL) {
            free(sys->blkdof_pos);
            sys->blkdof_pos = NULL;
            ret = 0;
        } else {
            sys->blkdof_pos[0] = 0;

            for (b = 0, p = 0; b < nb; b++) {
                for (; p < ct->blkfacepos[b + 1]; p++) {
                    dof = m->bfno[ ct->blkfaces[p] ];

                    if (dof >= 0) {
                        sys->blkdof[ sys->blkdof_pos[b + 1] ++ ] = dof;
                    }
                }
            }
        }
    }

    return ret;
}


/* ---------------------------------------------------------------------- */
static void
compute_alloc_sizes(size_t                  nb,
                    struct coarse_sys_meta *m,
                    struct coarse_sys      *sys,
                    size_t *bf_asz, size_t *ip_asz, size_t *Binv_asz)
/* ---------------------------------------------------------------------- */
{
    size_t nf, nc, b;

    *bf_asz = *ip_asz = *Binv_asz = 0;

    for (b = 0; b < nb; b++) {
        nf = sys->blkdof_pos[b + 1] - sys->blkdof_pos[b]; /* # coarse dof */
        nc = m  ->pb2c      [b + 1] - m  ->pb2c      [b]; /* # block c */

        *bf_asz   += nf                * m->blk_nhf[b];
        *ip_asz   += nf * (nf + 1) / 2 * nc;
        *Binv_asz += nf * nf;
    }
}


/* ---------------------------------------------------------------------- */
static struct coarse_sys *
coarse_sys_allocate(struct coarse_topology *ct,
                    struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    int               alloc_ok;
    size_t            nb;
    size_t            bf_asz, ip_asz, Binv_asz; /* Allocation sizes */

    struct coarse_sys *new;

    new = malloc(1 * sizeof *new);

    if (new != NULL) {
        nb = ct->nblocks;

        alloc_ok = blkdof_fill(ct, m, new);

        if (alloc_ok) {
            compute_alloc_sizes(nb, m, new, &bf_asz, &ip_asz, &Binv_asz);

            new->basis_pos   = calloc(nb + 1, sizeof *new->basis_pos  );
            new->cell_ip_pos = calloc(nb + 1, sizeof *new->cell_ip_pos);

            new->basis       = malloc(bf_asz   * sizeof *new->basis  );
            new->cell_ip     = malloc(ip_asz   * sizeof *new->cell_ip);
            new->Binv        = malloc(Binv_asz * sizeof *new->Binv   );

            alloc_ok += new->basis_pos   != NULL;
            alloc_ok += new->cell_ip_pos != NULL;
            alloc_ok += new->basis       != NULL;
            alloc_ok += new->cell_ip     != NULL;
            alloc_ok += new->Binv        != NULL;
        }

        if (alloc_ok < 6) {
            coarse_sys_destroy(new);
            new = NULL;
        }
    }

    return new;
}


/* ---------------------------------------------------------------------- */
static void
set_csys_block_pointers(struct coarse_topology *ct,
                        struct coarse_sys_meta *m ,
                        struct coarse_sys      *sys)
/* ---------------------------------------------------------------------- */
{
    int b, nb, nc, ndof, npairs;

    nb = ct->nblocks;

    sys->basis_pos[0] = sys->cell_ip_pos[0] = 0;

    for (b = 0; b < nb; b++) {
        ndof = sys->blkdof_pos[b + 1] - sys->blkdof_pos[b];
        nc   = m  ->pb2c      [b + 1] - m  ->pb2c      [b];

        npairs = ndof * (ndof + 1) / 2;

        sys->basis_pos[b + 1] = sys->basis_pos[b] + ndof*m->blk_nhf[b];

        sys->cell_ip_pos[b + 1] = sys->cell_ip_pos[b] + npairs*nc;
    }
}


/* Create local numbering of the fine-scale faces contained in a pair
 * of blocks denoted by 'cf'.
 *
 * Precondition: m->loc_fno[0 .. g->number_of_faces-1] < 0
 *
 * Returns the number of local fine-scale faces. */
/* ---------------------------------------------------------------------- */
static int
enumerate_local_dofs(size_t                  cf,
                     grid_t                 *g ,
                     struct coarse_topology *ct,
                     struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    int *b, *c, i, f, loc_no;

    loc_no = 0;

    for (b  = ct->neighbours + 2*(cf + 0);
         b != ct->neighbours + 2*(cf + 1); b++) {

        if (*b >= 0) {
            for (c  = m->b2c + m->pb2c[*b + 0];
                 c != m->b2c + m->pb2c[*b + 1]; c++) {

                for (i = g->cell_facepos[*c + 0];
                     i < g->cell_facepos[*c + 1]; i++) {

                    f = g->cell_faces[i];

                    if (m->loc_fno[f] < 0) {
                        m->loc_fno[f] = loc_no++;
                    }
                }
            }
        }
    }

    assert (loc_no > 0);

    return loc_no;
}


/* Destroy local numbering of fine-scale faces contained in a pair of
 * blocks denoted by 'cf'.
 *
 * This is the inverse of enumerate_local_dofs(), and is needed to
 * prepare assembly of the discrete local system defining the next
 * basis function. */
/* ---------------------------------------------------------------------- */
static void
unenumerate_local_dofs(size_t                  cf,
                       grid_t                 *g ,
                       struct coarse_topology *ct,
                       struct coarse_sys_meta *m)
/* ---------------------------------------------------------------------- */
{
    int *b, *c, i;

    /* Note that memset()-ing all of m->loc_fno (g->number_of_faces
     * entries) may or may not be faster than this loop.  The loop is
     * entirely random access to m->loc_fno which ruins locality, but
     * typically visits only a small subset of the actual elements.
     *
     * Profiling/measurement needed. */

    for (b  = ct->neighbours + 2*(cf + 0);
         b != ct->neighbours + 2*(cf + 1); b++) {
        if (*b >= 0) {
            for (c  = m->b2c + m->pb2c[*b + 0];
                 c != m->b2c + m->pb2c[*b + 1]; c++) {

                for (i = g->cell_facepos[*c + 0];
                     i < g->cell_facepos[*c + 1]; i++) {

                    m->loc_fno[ g->cell_faces[i] ] = -1;
                }
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
linearise_local_dof(size_t                  cf,
                    grid_t                 *g ,
                    struct coarse_topology *ct,
                    struct coarse_sys_meta *m ,
                    struct bf_asm_data     *bf_asm)
/* ---------------------------------------------------------------------- */
{
    int *b, *c, i;
    int *pdof, *dof;

    pdof = bf_asm->pdof;
    dof  = bf_asm->dof;

    pdof[0] = 0;

    for (b  = ct->neighbours + 2*(cf + 0);
         b != ct->neighbours + 2*(cf + 1); b++) {
        if (*b >= 0) {
            for (c  = m->b2c + m->pb2c[*b + 0];
                 c != m->b2c + m->pb2c[*b + 1]; c++) {

                for (i = g->cell_facepos[*c + 0];
                     i < g->cell_facepos[*c + 1]; i++) {
                    *dof++ = m->loc_fno[ g->cell_faces[i] ];
                }

                *++pdof = dof - bf_asm->dof;
            }
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
define_csr_sparsity(size_t nc, size_t m, struct bf_asm_data *bf_asm)
/* ---------------------------------------------------------------------- */
{
    int    p, *dof;
    size_t c, i;

    MAT_SIZE_T n, i1, i2, dof1, dof2;

    struct CSRMatrix *A = bf_asm->A;

    /* ------------------------------------------------------------------
     * Count connections (O(m))
     * ------------------------------------------------------------------ */
    for (i = 0; i < m; i++) { A->ia[ i + 1 ] = 1; } /* Count self */

    for (c = 0, p = 0; c < nc; c++) {
        n = bf_asm->pdof[c + 1] - bf_asm->pdof[c];

        for (; p < bf_asm->pdof[c + 1]; p++) {
            assert ((size_t) bf_asm->dof[p] < m);

            A->ia[ bf_asm->dof[p] + 1 ] += n - 1; /* Count other */
        }
    }

    /* ------------------------------------------------------------------
     * Define start pointers (O(m))
     * ------------------------------------------------------------------ */
    A->ia[0] = 0;
    for (i = 0; i < m; i++) {
        A->ia[0] += A->ia[i];
        A->ia[i]  = A->ia[0] - A->ia[i];
    }

    /* ------------------------------------------------------------------
     * Set sparsity (O(nnz))
     * ------------------------------------------------------------------ */
    A->ia[0] = 0;
    for (i = 0; i < m; i++) {
        A->ja[ A->ia[i + 1] ++ ] = (MAT_SIZE_T) i; /* Set self */
    }

    for (c = 0; c < nc; c++) {
        n = bf_asm->pdof[c + 1] - bf_asm->pdof[c];

        dof = bf_asm->dof + bf_asm->pdof[c];

        for (i1 = 0; i1 < n; i1++) {
            dof1 = dof[i1];

            for (i2 = (i1 + 1) % n; i2 != i1; i2 = (i2 + 1) % n) {
                dof2 = dof[i2];

                A->ja[ A->ia[ dof1 + 1 ] ++ ] = dof2;
            }
        }
    }

    A->m   = m;
    A->n   = m;
    A->nnz = A->ia[m];

    /* Enforce sorted connection structure per row */
    csrmatrix_sortrows(A);
}


/* ---------------------------------------------------------------------- */
static void
vector_zero(size_t n, double *v)
/* ---------------------------------------------------------------------- */
{
    size_t i;

    for (i = 0; i < n; i++) { v[i] = 0.0; }
}


/* ---------------------------------------------------------------------- */
static void
assemble_local_system(size_t                  cf   ,
                      size_t                  nlocf,
                      grid_t                 *g    ,
                      const double           *Binv ,
                      double                 *w    ,
                      struct coarse_topology *ct   ,
                      struct coarse_sys_meta *m    ,
                      struct bf_asm_data     *bf_asm)
/* ---------------------------------------------------------------------- */
{
    int    c, i, p1, p2, ndof;
    int    *b, *dof;
    size_t nc;

    double sgn;

    linearise_local_dof(cf, g, ct, m, bf_asm);

    nc = 0;
    for (b  = ct->neighbours + 2*(cf + 0);
         b != ct->neighbours + 2*(cf + 1); b++) {
        if (*b >= 0) {
            nc += m->pb2c[*b + 1] - m->pb2c[*b];
        }
    }

    define_csr_sparsity(nc, nlocf, bf_asm);

    csrmatrix_zero(       bf_asm->A);
    vector_zero   (nlocf, bf_asm->b);

    sgn = 1.0;
    dof = bf_asm->dof;
    for (b  = ct->neighbours + 2*(cf + 0);
         b != ct->neighbours + 2*(cf + 1); b++) {

        if (*b >= 0) {
            for (i = m->pb2c[*b]; i < m->pb2c[*b + 1]; i++) {
                c    = m->b2c[i];
                p1   = g->cell_facepos[c];
                p2   = m->pconn2[c];
                ndof = g->cell_facepos[c + 1] - p1;

                w[c] *= sgn;    /* Set w-sign according to source/sink */

                hybsys_cellcontrib_symm(c, ndof, p1, p2, bf_asm->gpress,
                                        w, Binv, bf_asm->fsys);

                hybsys_global_assemble_cell(ndof, dof, bf_asm->fsys->S,
                                            bf_asm->fsys->r, bf_asm->A,
                                            bf_asm->b);

                w[c] *= sgn;    /* Restore original weighting sign */

                dof += ndof;
            }

            sgn = -sgn;
        }
    }
}


/* ---------------------------------------------------------------------- */
static void
Binv_scale_mobility(int nc, struct coarse_sys_meta *m,
                    const double *totmob, double *Binv)
/* ---------------------------------------------------------------------- */
{
    int c, i;

    for (c = i = 0; c < nc; c++) {
        for (; i < m->pconn2[c]; i++) {
            Binv[i] *= totmob[c];
        }
    }
}


/* ---------------------------------------------------------------------- */
struct coarse_sys *
coarse_sys_construct(grid_t *g, const int   *p,
                     struct coarse_topology *ct,
                     const double           *perm,
                     const double           *src,
                     const double           *totmob)
/* ---------------------------------------------------------------------- */
{
    int                     cf;
    size_t                  nlocf;
    double                 *Binv, *w;
    struct coarse_sys_meta *m;
    struct bf_asm_data     *bf_asm;
    struct coarse_sys      *sys;

    sys = NULL;  bf_asm = NULL;  Binv = NULL;  w = NULL;

    m = coarse_sys_meta_construct(g, p, ct);

    if (m != NULL) {
        Binv   = compute_fs_ip(g, perm, m);
        w      = coarse_weight(g, ct->nblocks, p, m, perm, src);
        bf_asm = bf_asm_data_allocate(g, m);
        sys    = coarse_sys_allocate(ct, m);
    }

    if ((Binv != NULL) && (w != NULL) &&
        (bf_asm != NULL) && (sys != NULL)) {

        /* Prepare storage tables */
        set_csys_block_pointers(ct, m, sys);

        /* Exclude effects of gravity */
        vector_zero(g->cell_facepos[ g->number_of_cells ], bf_asm->gpress);

        /* Include mobility effects (multiple phases) */
        Binv_scale_mobility(g->number_of_cells, m, totmob, Binv);

        /* Discretise flow equation on fine scale */
        hybsys_schur_comp_symm(g->number_of_cells, g->cell_facepos,
                               Binv, bf_asm->fsys);

        for (cf = 0; cf < ct->nfaces; cf++) {
            if (m->bfno[cf] >= 0) {
                nlocf = enumerate_local_dofs(cf, g, ct, m);

                assemble_local_system(cf, nlocf, g, Binv, w,
                                      ct, m, bf_asm);
                /* solve_local_sys() */
                /* store_bf() */

                unenumerate_local_dofs(cf, g, ct, m);
            }
        }

        coarse_sys_compute_cell_ip(g->number_of_cells,
                                   m->max_ngconn,
                                   ct->nblocks,
                                   g->cell_facepos,
                                   Binv,
                                   m->pb2c, m->b2c,
                                   sys);
    }

    bf_asm_data_deallocate(bf_asm);

    free(w);    free(Binv);
    coarse_sys_meta_destroy(m);

    return sys;
}


/* Release dynamic memory resources for coarse system data structure. */
/* ---------------------------------------------------------------------- */
void
coarse_sys_destroy(struct coarse_sys *sys)
/* ---------------------------------------------------------------------- */
{
    if (sys != NULL) {
        free(sys->Binv);
        free(sys->cell_ip);
        free(sys->basis);

        free(sys->cell_ip_pos);
        free(sys->basis_pos);

        free(sys->blkdof);
        free(sys->blkdof_pos);
    }

    free(sys);
}


/* Compute \Psi'_i * B * \Psi_j for all basis function pairs (i,j) for
 * all cells.  Inverts inv(B) (i.e., Binv) in each cell.  Iterates
 * over blocks (CSR representation b2c_pos, b2c).  Result store in
 * sys->cell_ip, a packed representation of the IP pairs (one col per
 * cell per block).
 *
 * Allocates work arrays and may fail.  Does currently not report failure.*/
/* ---------------------------------------------------------------------- */
void
coarse_sys_compute_cell_ip(int                nc,
                           int                max_nconn,
                           int                nb,
                           const int         *pconn,
                           const double      *Binv,
                           const int         *b2c_pos,
                           const int         *b2c,
                           struct coarse_sys *sys)
/* ---------------------------------------------------------------------- */
{
    int i, i1, i2, b, c, n, bf, *pconn2;
    int max_nbf, nbf, loc_nc;

    size_t p, nbf_pairs, bf_off, bf_sz;

    MAT_SIZE_T mm, nn, kk, nrhs, ld1, ld2, ld3, info;

    double a1, a2;
    double *work, *BI, *Psi, *IP;

#if DEBUG_OUTPUT
    FILE *fp;
#endif


    max_nbf = max_diff(nb, sys->blkdof_pos);

    pconn2  = malloc((nc + 1) * sizeof *pconn2);
    work    = malloc(((max_nconn * max_nconn) + /* BI */
                      (max_nconn * max_nbf  ) + /* Psi */
                      (max_nbf   * max_nbf  ))  /* IP */
                     * sizeof *work);

    if ((pconn2 != NULL) && (work != NULL)) {
        BI  = work + 0                      ;
        Psi = BI   + (max_nconn * max_nconn);
        IP  = Psi  + (max_nconn * max_nbf  );

        pconn2[0] = 0;

        for (i = 1; i <= nc; i++) {
            n         = pconn[i] - pconn[i - 1];
            pconn2[i] = pconn2[i - 1] + (n * n);
        }

#if DEBUG_OUTPUT
        fp = fopen("debug_out.m", "wt");
#endif

        for (b = 0; b < nb; b++) {
            loc_nc = b2c_pos[b + 1] - b2c_pos[b];
            bf_off = 0;
            nbf    = sys->blkdof_pos[b + 1] - sys->blkdof_pos[b];

            assert ((sys->basis_pos[b + 1] -
                     sys->basis_pos[b + 0]) % nbf == 0);

            bf_sz  = (sys->basis_pos[b + 1] - sys->basis_pos[b]) / nbf;

            nbf_pairs = nbf * (nbf + 1) / 2;

            for (i = 0; i < loc_nc; i++) {
                c = b2c[b2c_pos[b] + i];
                n = pconn[c + 1] - pconn[c];

                /* Linearise (collect) BF values for cell */
                p = sys->basis_pos[b] + bf_off;
                for (bf = 0; bf < nbf; bf++, p += bf_sz) {
                    memcpy(Psi + bf*n, &sys->basis[p], n * sizeof *Psi);
                }
#if DEBUG_OUTPUT
                fprintf(fp, "Psi{%d,%d} = [\n", b+1, i+1);
                for (i2 = 0; i2 < nbf; i2++) {
                    for (i1 = 0; i1 < n; i1++) {
                        fprintf(fp, "%22.15e ", Psi[i1 + i2*n]);
                    }
                    fprintf(fp, ";\n");
                }
                fprintf(fp, "].';\n");
#endif

                /* Extract cell's inv(B) values... */
                memcpy(BI, &Binv[pconn2[c]], n * n * sizeof *BI);
#if DEBUG_OUTPUT
                fprintf(fp, "BI{%d,%d} = [\n", b+1, i+1);
                for (i2 = 0; i2 < n; i2++) {
                    for (i1 = 0; i1 < n; i1++) {
                        fprintf(fp, "%22.15e ", BI[i1 + i2*n]);
                    }
                    fprintf(fp, ";\n");
                }
                fprintf(fp, "].';\n");
#endif

                /* ...and (Cholesky) factor it... */
                nn  = n;
                ld1 = n;
                dpotrf_("Upper Triangular", &nn, BI, &ld1, &info);

                /* ...and solve BI*X = Psi (=> Psi (=X) <- B*Psi) */
                nrhs = nbf;
                ld2  = n;
                dpotrs_("Upper Triangular", &nn, &nrhs, BI, &ld1,
                        Psi, &ld2, &info);
#if DEBUG_OUTPUT
                fprintf(fp, "BPsi{%d,%d} = [\n", b+1, i+1);
                for (i2 = 0; i2 < nbf; i2++) {
                    for (i1 = 0; i1 < n; i1++) {
                        fprintf(fp, "%22.15e ", Psi[i1 + i2*n]);
                    }
                    fprintf(fp, ";\n");
                }
                fprintf(fp, "].';\n");
#endif

                /* Finally, compute IP = Psi'*X = Psi'*B*Psi... */
                mm  = nn = nbf;
                kk  = n;
                ld1 = bf_sz;   ld2 = n;    ld3 = nbf;
                a1  = 1.0;     a2  = 0.0;
                dgemm_("Transpose", "No Transpose", &mm, &nn, &kk,
                       &a1, &sys->basis[sys->basis_pos[b] + bf_off], &ld1,
                       Psi, &ld2, &a2, IP, &ld3);
#if DEBUG_OUTPUT
                fprintf(fp, "PsiTBPsi{%d,%d} = [\n", b+1, i+1);
                for (i2 = 0; i2 < nbf; i2++) {
                    for (i1 = 0; i1 < nbf; i1++) {
                        fprintf(fp, "%22.15e ", IP[i1 + i2*nbf]);
                    }
                    fprintf(fp, ";\n");
                }
                fprintf(fp, "].';\n");
#endif

                /* ...and fill results into ip-contrib for this cell... */
                p = sys->cell_ip_pos[b] + i*nbf_pairs;
                for (i2 = 0; i2 < nbf; i2++) {
                    for (i1 = 0; i1 <= i2; i1++, p++) {
                        sys->cell_ip[p] = IP[i1 + i2*nbf];
                    }
                }

                /* ...and prepare for next cell. */
                bf_off += n;
            }
        }
#if DEBUG_OUTPUT
        fclose(fp);
#endif
    }

    free(work);  free(pconn2);
}


/* Compute inv(B) on coarse scale from fine-scale contributions.
 * Specifically, this function computes the inverse of
 * B=sum(1/lambda_c * B_c, c\in Blk_j) for all blocks, 'j'.  The
 * fine-scale B contributions are computed in
 * coarse_sys_compute_cell_ip() above.
 *
 * Does not fail. */
/* ---------------------------------------------------------------------- */
void
coarse_sys_compute_Binv(int                nb,
                        int                max_bcells,
                        const double      *totmob,
                        const int         *b2c_pos,
                        const int         *b2c,
                        struct coarse_sys *sys,
                        double            *work)
/* ---------------------------------------------------------------------- */
{
    int    b, i, i1, i2, nbf_pairs, loc_nc, nbf;

    double a1, a2;
    double *Lti, *B;

    size_t p1, p2;

    MAT_SIZE_T mm, nn, ld, incx, incy, info;

#if DEBUG_OUTPUT
    FILE *fp;
#endif

    Lti = work + 0;
    B   = work + max_bcells;

    incx = incy = 1;
    p2   = 0;
    for (b = 0; b < nb; b++) {
        loc_nc = b2c_pos[b + 1] - b2c_pos[b];

        for (i = 0; i < loc_nc; i++) {
            Lti[i] = 1.0 / totmob[b2c[b2c_pos[b] + i]];
        }

        /* Form coarse inner-product matrix for block 'b' as (inverse)
         * mobility weighted sum of cell contributions */
        nbf       = sys->blkdof_pos[b + 1] - sys->blkdof_pos[b];
        nbf_pairs = nbf * (nbf + 1) / 2;

        mm = ld = nbf_pairs;
        nn = loc_nc;
        a1 = 1.0;
        a2 = 0.0;
        dgemv_("No Transpose", &mm, &nn, &a1,
               &sys->cell_ip[sys->cell_ip_pos[b]], &ld,
               Lti, &incx, &a2, B, &incy);

        /* Factor (packed) SPD inner-product matrix... */
        mm = nbf;
        dpptrf_("Upper Triangular", &mm, B, &info);
        if (info == 0) {
            /* ...and invert it... */
            dpptri_("Upper Triangular", &mm, B, &info);
        } else {
#if DEBUG_OUTPUT
            fp = fopen("debug_out.m", "at");
            mm = ld = nbf_pairs;
            nn = loc_nc;
            a1 = 1.0;
            a2 = 0.0;
            dgemv_("No Transpose", &mm, &nn, &a1,
                   &sys->cell_ip[sys->ip_pos[b]], &ld,
                   Lti, &incx, &a2, B, &incy);
            fprintf(fp, "IP{%d} = [\n", b + 1);
            for (i2 = 0; i2 < nn; i2++) {
                for (i1 = 0; i1 < mm; i1++) {
                    fprintf(fp, "%18.12e ",
                            sys->cell_ip[sys->ip_pos[b] + i1 + i2*mm]);
                }
                fprintf(fp, ";\n");
            }
            fprintf(fp, "].';\n");

            fprintf(fp, "B{%d} = [ %% info = %lu\n", b + 1,
                    (unsigned long) info);
            for (i1 = 0; i1 < nbf_pairs; i1++)
                fprintf(fp, "%22.15e ", B[i1]);
            fprintf(fp, "].';\n");
            fclose(fp);
#endif
        }

        /* ...and write result to permanent storage suitable for
         * reduction functions hybsys_schur_comp*() */
        p1 = 0;
        for (i2 = 0; i2 < nbf; i2++) {
            for (i1 = 0; i1 <= i2; i1++, p1++) {
                sys->Binv[p2 + i1 + i2*nbf] = B[p1]; /* col i2 */
                sys->Binv[p2 + i2 + i1*nbf] = B[p1]; /* row i2 */
            }
        }

        p2 += nbf * nbf;
    }
}
