#ifndef HYBSYS_H_INCLUDED
#define HYBSYS_H_INCLUDED

#ifndef MAT_SIZE_T
#define MAT_SIZE_T int
#endif

struct hybsys {
    double *L;                  /* C' * inv(B) * C */
    double *F;                  /* C' * inv(B)     */
    double *r;                  /* system rhs per half face */
    double *S;                  /* system matrix in single cell */
    double *one;                /* ones(max_nconn, 1) */
    double *work;               /* work array (SIZE [max_nconn, 1]) */
};



struct Sparse
{
    int        m;
    int        n;
    MAT_SIZE_T *ia;
    MAT_SIZE_T *ja;
    double     *sa;
};



struct hybsys *
hybsys_allocate(int max_nconn, int nc, int nconn_tot);

void
hybsys_free(struct hybsys *sys);

void
hybsys_init(int max_nconn, int nconn_tot, struct hybsys *sys);

void
hybsys_compute_components(int nc, const int *pconn,
                          const double *gflux, const double *src,
                          const double *Binv, struct hybsys *sys);

void
hybsys_compute_cellmatrix_core(int nconn, const double *Binv,
                               double L, const double *F, double *S);

void
hybsys_compute_cellmatrix(int c, int nconn, int p1, int p2,
                          const double *Binv, struct hybsys *sys);

void
hybsys_compute_press_flux(int nc, const int *pconn, const int *conn,
                          const double *gflux, const double *src,
                          const double *Binv, const struct hybsys *sys,
                          const double *pi, double *press, double *flux,
                          double *work, const int lwork);

void
hybsys_assemble(int nc, int nf,
                const int *pconn, const int *conn,
                const double *S, const double *R,
                struct Sparse *A, double **b);

#endif  /* HYBSYS_H_INCLUDED */
