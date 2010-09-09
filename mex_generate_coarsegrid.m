function varargout = mex_generate_coarsegrid(varargin)
%Build coarse grid data structure from partition of existing grid (MEX)
%
% SYNOPSIS:
%   CG = mex_generate_coarsegrid(G, p)
%   CG = mex_generate_coarsegrid(G, p, expected_nconn)
%
% PARAMETERS:
%   G - Grid data structure as described in 'grid_structure'.
%
%   p - Partition vector as defined by, e.g., functions 'partitionUI' or
%       'partitionNonUniform'.
%
%   expected_nconn -
%       Number (non-negative integer) of expected fine-scale faces
%       constituting a coarse-scale face.  If expected_nconn==0, then
%       constituent fine-scale faces will not be computed.  On the other
%       hand, if expected_nconn > 0, then constituent fine-scale faces will
%       be derived (similarly to the output of function 'subFaces').  Any
%       positive number may be used, but the implementation is most
%       efficient if 'expected_nconn' is in the same order of magnitude as
%       the typical number of constituent fine-scale faces.
%
%       OPTIONAL.  Default value: expected_nconn=0 (don't compute
%       constituent fine-scale faces (sub-faces)).
%
% RETURNS:
%   CG - Coarse grid data structure as described in 'generateCoarseGrid'.
%        There is, however, a number of subtle differences in the details
%        of this structure as compared to the pure M implementation.
%        Specifically, the coarse faces are numbered differently, and the
%        MEX implementation does not distinguish cardinal directions whence
%        the 'subFaces' function does not produce meaningful results on
%        outer faces.
%
%        If expected_nconn>0, then the 'faces' structure has two additional
%        fields 'subfacePos', and 'subfaces'.  This indirection/data array
%        pair is related such that the sub-faces for coarse face 'i' is
%        located in
%
%            subfaces(subfacePos(i) : subfacePos(i+1) - 1)
%
%        The constituent sub-faces of a particular coarse face may occur in
%        any order.
%
% SEE ALSO:
%   generateCoarseGrid, subFaces.

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
        -O -largeArrayDims ...
   ...
        mex_generate_coarsegrid.c coarse_conn.c hash_set.c mrst_api.c

   % Call MEX'ed edition.
   [varargout{1:nargout}] = mex_generate_coarsegrid(varargin{:});
end
