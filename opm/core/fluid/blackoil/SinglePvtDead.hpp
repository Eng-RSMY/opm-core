/*
  Copyright 2010, 2011, 2012 SINTEF ICT, Applied Mathematics.

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

#ifndef OPM_SINGLEPVTDEAD_HEADER_INCLUDED
#define OPM_SINGLEPVTDEAD_HEADER_INCLUDED


#include <opm/core/fluid/blackoil/SinglePvtInterface.hpp>
#include <opm/core/utility/UniformTableLinear.hpp>
#include <vector>

namespace Opm
{

    /// Class for immiscible dead oil and dry gas.
    class SinglePvtDead : public SinglePvtInterface
    {
    public:
	typedef std::vector<std::vector<std::vector<double> > > table_t;

	SinglePvtDead(const table_t& pvd_table);
	virtual ~SinglePvtDead();

        /// Viscosity as a function of p and z.
        virtual void mu(const int n,
                        const double* p,
                        const double* z,
                        double* output_mu) const;

        /// Formation volume factor as a function of p and z.
        virtual void B(const int n,
                       const double* p,
                       const double* z,
                       double* output_B) const;

        /// Formation volume factor and p-derivative as functions of p and z.
        virtual void dBdp(const int n,
                          const double* p,
                          const double* z,
                          double* output_B,
                          double* output_dBdp) const;

        /// Solution factor as a function of p and z.
        virtual void R(const int n,
                       const double* p,
                       const double* z,
                       double* output_R) const;

        /// Solution factor and p-derivative as functions of p and z.
        virtual void dRdp(const int n,
                          const double* p,
                          const double* z,
                          double* output_R,
                          double* output_dRdp) const;
    private:
	// PVT properties of dry gas or dead oil
        Dune::utils::UniformTableLinear<double> one_over_B_;
        Dune::utils::UniformTableLinear<double> viscosity_;
    };

}

#endif // OPM_SINGLEPVTDEAD_HEADER_INCLUDED

