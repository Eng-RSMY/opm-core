#ifndef BLAS_LAPACK_H_INCLUDED
#define BLAS_LAPACK_H_INCLUDED

#if defined(MATLAB_MEX_FILE) && MATLAB_MEX_FILE
#include <mex.h>
#undef  MAT_SIZE_T
#define MAT_SIZE_T mwSignedIndex
#endif

#ifndef MAT_SIZE_T
#define MAT_SIZE_T int
#endif

/* C <- a1*op(A)*op(B) + a2*C  where  op(X) in {X, X.'} */
void dgemm_(const char *transA  , const char *transB   ,
            const MAT_SIZE_T*  m, const MAT_SIZE_T* n  , const MAT_SIZE_T* k  ,
            const double*     a1, const double*     A  , const MAT_SIZE_T* ldA,
            const double*      B, const MAT_SIZE_T* ldB,
            const double*     a2,       double*     C  , const MAT_SIZE_T* ldC);


/* C <- a1*A*A' + a2*C   *or*   C <- a1*A'*A + a2*C */
void dsyrk_(const char       *uplo, const char       *trans,
            const MAT_SIZE_T *n   , const MAT_SIZE_T *k    ,
            const double     *a1  , const double     *A    , const MAT_SIZE_T *ldA,
            const double     *a2  ,       double     *C    , const MAT_SIZE_T *ldC);


void dgeqrf_(const MAT_SIZE_T *m    , const MAT_SIZE_T *n   ,
                   double     *A    , const MAT_SIZE_T *ld  ,
                   double     *tau  ,       double     *work,
             const MAT_SIZE_T *lwork,       MAT_SIZE_T *info);


void dorgqr_(const MAT_SIZE_T *m   , const MAT_SIZE_T *n    , const MAT_SIZE_T *k  ,
                   double     *A   , const MAT_SIZE_T *ld   , const double     *tau,
                   double     *work, const MAT_SIZE_T *lwork,       MAT_SIZE_T *info);


/* A <- chol(A) */
void dpotrf_(const char *uplo, const MAT_SIZE_T *n,
             double     *A   , const MAT_SIZE_T *lda,
             MAT_SIZE_T *info);

/* B <- (A \ (A' \ B)), when A=DPOTRF(A_orig) */
void dpotrs_(const char *uplo, const MAT_SIZE_T *n  , const MAT_SIZE_T *nrhs,
             double     *A   , const MAT_SIZE_T *lda,
             double     *B   , const MAT_SIZE_T *ldb,       MAT_SIZE_T *info);

/* A <- chol(A), packed format. */
void dpptrf_(const char *uplo, const MAT_SIZE_T *n,
             double     *Ap  ,       MAT_SIZE_T *info);

/* A <- (A \ (A' \ eye(n)) when A=DPPTRF(A_orig) (packed format). */
void dpptri_(const char *uplo, const MAT_SIZE_T *n,
             double     *Ap  ,       MAT_SIZE_T *info);

/* y <- a1*op(A)*x + a2*y */
void dgemv_(const char       *trans,
            const MAT_SIZE_T *m    , const MAT_SIZE_T *n,
            const double     *a1   , const double     *A, const MAT_SIZE_T *ldA ,
                                     const double     *x, const MAT_SIZE_T *incX,
            const double     *a2   ,       double     *y, const MAT_SIZE_T *incY);


/* y <- a*x + y */
void daxpy_(const MAT_SIZE_T *n, const double *a,
            const double *x, const MAT_SIZE_T *incx,
                  double *y, const MAT_SIZE_T *incy);

/* s <- x' * y */
double ddot_(const MAT_SIZE_T *n, const double *x, const MAT_SIZE_T *incx,
                                  const double *y, const MAT_SIZE_T *incy);


#endif /* BLAS_LAPACK_H_INCLUDED */
