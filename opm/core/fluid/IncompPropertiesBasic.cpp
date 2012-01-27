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



#include <opm/core/fluid/IncompPropertiesBasic.hpp>
#include <opm/core/utility/Units.hpp>
#include <opm/core/utility/ErrorMacros.hpp>
#include <iostream>

namespace Opm
{

    IncompPropertiesBasic::IncompPropertiesBasic(const parameter::ParameterGroup& param,
						 const int dim,
						 const int num_cells)
    {
	double poro = param.getDefault("porosity", 1.0);
	using namespace Opm::unit;
	using namespace Opm::prefix;
	double perm = param.getDefault("permeability", 100.0)*milli*darcy;
        rock_.init(dim, num_cells, poro, perm);
	pvt_.init(param);
        satprops_.init(param);
	if (pvt_.numPhases() != satprops_.numPhases()) {
	    THROW("IncompPropertiesBasic::IncompPropertiesBasic() - Inconsistent number of phases in pvt data ("
		  << pvt_.numPhases() << ") and saturation-dependent function data (" << satprops_.numPhases() << ").");
	}
	viscosity_.resize(pvt_.numPhases());
	pvt_.mu(1, 0, 0, &viscosity_[0]);
    }

    IncompPropertiesBasic::~IncompPropertiesBasic()
    {
    }


    /// \return   D, the number of spatial dimensions.
    int IncompPropertiesBasic::numDimensions() const
    {
        return rock_.numDimensions();
    }

    /// \return   N, the number of cells.
    int IncompPropertiesBasic::numCells() const
    {
        return rock_.numCells();
    }

    /// \return   Array of N porosity values.
    const double* IncompPropertiesBasic::porosity() const
    {
        return rock_.porosity();
    }

    /// \return   Array of ND^2 permeability values.
    ///           The D^2 permeability values for a cell are organized as a matrix,
    ///           which is symmetric (so ordering does not matter).
    const double* IncompPropertiesBasic::permeability() const
    {
        return rock_.permeability();
    }


    // ---- Fluid interface ----

    /// \return   P, the number of phases (also the number of components).
    int IncompPropertiesBasic::numPhases() const
    {
        return pvt_.numPhases();
    }

    /// \return Array of P viscosity values.
    const double* IncompPropertiesBasic::viscosity() const
    {
	return &viscosity_[0];
    }

    /// \return Array of P density values.
    const double* IncompPropertiesBasic::density() const
    {
	return pvt_.surfaceDensities();
    }

    /// \param[in]  n      Number of data points.
    /// \param[in]  s      Array of nP saturation values.
    /// \param[in]  cells  Array of n cell indices to be associated with the s values.
    /// \param[out] kr     Array of nP relperm values, array must be valid before calling.
    /// \param[out] dkrds  If non-null: array of nP^2 relperm derivative values,
    ///                    array must be valid before calling.
    ///                    The P^2 derivative matrix is
    ///                           m_{ij} = \frac{dkr_i}{ds^j},
    ///                    and is output in Fortran order (m_00 m_10 m_20 m_01 ...)
    void IncompPropertiesBasic::relperm(const int n,
					const double* s,
					const int* /*cells*/,
					double* kr,
					double* dkrds) const
    {
        satprops_.relperm(n, s, kr, dkrds);
    }


    /// \param[in]  n      Number of data points.
    /// \param[in]  s      Array of nP saturation values.
    /// \param[in]  cells  Array of n cell indices to be associated with the s values.
    /// \param[out] pc     Array of nP capillary pressure values, array must be valid before calling.
    /// \param[out] dpcds  If non-null: array of nP^2 derivative values,
    ///                    array must be valid before calling.
    ///                    The P^2 derivative matrix is
    ///                           m_{ij} = \frac{dpc_i}{ds^j},
    ///                    and is output in Fortran order (m_00 m_10 m_20 m_01 ...)
    void IncompPropertiesBasic::capPress(const int n,
					 const double* s,
					 const int* /*cells*/,
					 double* pc,
					 double* dpcds) const
    {
        satprops_.capPress(n, s, pc, dpcds);
    }



} // namespace Opm

