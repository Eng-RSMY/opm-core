/*
  Copyright 2012 SINTEF ICT, Applied Mathematics.

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

#ifndef OPM_TRANSPORTMODELTRACERTOFDISCGAL_HEADER_INCLUDED
#define OPM_TRANSPORTMODELTRACERTOFDISCGAL_HEADER_INCLUDED

#include <opm/core/transport/reorder/TransportModelInterface.hpp>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <map>
#include <ostream>

struct UnstructuredGrid;

namespace Opm
{

    class IncompPropertiesInterface;
    class VelocityInterpolationInterface;

    /// Implements a discontinuous Galerkin solver for
    /// (single-phase) time-of-flight using reordering.
    /// The equation solved is:
    ///     v \cdot \grad\tau = \phi
    /// where v is the fluid velocity, \tau is time-of-flight and
    /// \phi is the porosity. This is a boundary value problem, where
    /// \tau is specified to be zero on all inflow boundaries.
    /// The user may specify the polynomial degree of the basis function space
    /// used, but only degrees 0 and 1 are supported so far.
    class TransportModelTracerTofDiscGal : public TransportModelInterface
    {
    public:
        /// Construct solver.
        /// \param[in] grid      A 2d or 3d grid.
        /// \param[in] use_cvi   If true, use corner point velocity interpolation.
        ///                      Otherwise, use the basic constant interpolation.
        TransportModelTracerTofDiscGal(const UnstructuredGrid& grid,
                                       const bool use_cvi,
                                       const bool use_limiter = false);


        /// Solve for time-of-flight.
        /// \param[in]  darcyflux         Array of signed face fluxes.
        /// \param[in]  porevolume        Array of pore volumes.
        /// \param[in]  source            Source term. Sign convention is:
        ///                                 (+) inflow flux,
        ///                                 (-) outflow flux.
        /// \param[in]  degree            Polynomial degree of DG basis functions used.
        /// \param[out] tof_coeff         Array of time-of-flight solution coefficients.
        ///                               The values are ordered by cell, meaning that
        ///                               the K coefficients corresponding to the first
        ///                               cell comes before the K coefficients corresponding
        ///                               to the second cell etc.
        ///                               K depends on degree and grid dimension.
        void solveTof(const double* darcyflux,
                      const double* porevolume,
                      const double* source,
                      const int degree,
                      std::vector<double>& tof_coeff);

    private:
        virtual void solveSingleCell(const int cell);
        virtual void solveMultiCell(const int num_cells, const int* cells);

    private:
        // Disable copying and assignment.
        TransportModelTracerTofDiscGal(const TransportModelTracerTofDiscGal&);
        TransportModelTracerTofDiscGal& operator=(const TransportModelTracerTofDiscGal&);

        // Data members
        const UnstructuredGrid& grid_;
        boost::shared_ptr<VelocityInterpolationInterface> velocity_interpolation_;
        bool use_cvi_;
        bool use_limiter_;
        const double* darcyflux_;   // one flux per grid face
        const double* porevolume_;  // one volume per cell
        const double* source_;      // one volumetric source term per cell
        int degree_;
        double* tof_coeff_;
        std::vector<double> rhs_;   // single-cell right-hand-side
        std::vector<double> jac_;   // single-cell jacobian
        std::vector<double> orig_rhs_;   // single-cell right-hand-side (copy)
        std::vector<double> orig_jac_;   // single-cell jacobian (copy)
        // Below: storage for quantities needed by solveSingleCell().
        std::vector<double> coord_;
        std::vector<double> basis_;
        std::vector<double> basis_nb_;
        std::vector<double> grad_basis_;
        std::vector<double> velocity_;

        // Private methods
        void useLimiter(const int cell);
    };

} // namespace Opm

#endif // OPM_TRANSPORTMODELTRACERTOFDISCGAL_HEADER_INCLUDED
