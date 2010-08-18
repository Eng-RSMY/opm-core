function varargout = mex_schur_comp_symm(varargin)
%Compute hybrid system component matrices using compiled C code.
%
% SYNOPSIS:
%   [S, r, F1, F2, L] = mex_schur_comp_symm(BI, connPos, conns)
%
% PARAMETERS:
%   BI      - Inner product values.
%
%   connPos - Indirection map of size [G.cells.num,1] into 'conns' table
%             (i.e., the connections or DOFs).  Specifically, the DOFs
%             connected to cell 'i' are found in the submatrix
%
%                  conns(connPos(i) : connPos(i + 1) - 1)
%
%   conns   - A (connPos(end)-1)-by-1 array of cell connections
%             (local-to-global DOF mapping in FEM parlance).
%
% NOTE:
%   The (connPos,conns) array pair is expected to be the output of function
%   'mex_ip_simple'.
%
% RETURNS:
%   S  - A SUM(nconn .^ 2)-by-1 array of unassembled system matrix values,
%        ordered by cells.
%
%   r  - A SUM(nconn)-by-1 array of unassemble system rhs values, ordered by
%        cells.
%
%   F1 - A SUM(nconn)-by-1 array of C'*inv(B) values, ordered by cells.
%
%   F2 - A SUM(nconn)-by-1 array of C'*inv(B) values, ordered by cells.
%
%   L  - A G.cells.num-by-1 array of C'*inv(B)*C values, ordered by cells.
%
% SEE ALSO:
%   mex_ip_simple, mex_compute_press_flux.

%{
#COPYRIGHT#
%}

% $Date$
% $Revision$

   buildmex CFLAGS="\$CFLAGS -Wall -Wextra -ansi -pedantic           ...
        -Wformat-nonliteral -Wcast-align -Wpointer-arith             ...
        -Wbad-function-cast -Wmissing-prototypes -Wstrict-prototypes ...
        -Wmissing-declarations -Winline -Wundef -Wnested-externs     ...
        -Wcast-qual -Wshadow -Wconversion -Wwrite-strings            ...
        -Wno-conversion -Wchar-subscripts -Wredundant-decls"         ...
   ...
        -O -largeArrayDims -DCOMPILING_FOR_MATLAB=1    ...
        -DASSEMBLE_AND_SOLVE_UMFPACK=0                 ...
        -I/work/include/SuiteSparse/                   ...
        -I/usr/include/suitesparse/                    ...
   ...
        mex_schur_comp.c hybsys.c call_umfpack.c       ...
   ...
        -lmwumfpack -lmwamd -lmwlapack -lmwblas

   % Call MEX'ed edition.
   [varargout{1:nargout}] = mex_schur_comp_symm(varargin{:});
end
