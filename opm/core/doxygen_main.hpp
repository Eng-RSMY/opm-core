/*
  Copyright 2012, 2013 SINTEF ICT, Applied Mathematics.

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

#ifndef OPM_DOXYGEN_MAIN_HEADER_INCLUDED
#define OPM_DOXYGEN_MAIN_HEADER_INCLUDED


/** \mainpage Documentation for the opm-core library.

The following are the main library features:

<h3>Grid handling</h3>

The library defines a simple grid interface through the struct
UnstructuredGrid. This can be used both from C and C++ code, and has a
structure that is similar to MRST grids.

Cells can have arbitrary shapes, and arbitrary numbers of neighbours.
This flexibility allows us to handle complex grids such as faulted
corner-point grids or PEBI grids. The structure is suited for
computation with (for example) finite volume methods or mimetic finite
differences. It is less ideal for finite element computations, as it
does not provide any reference element mappings.

There are multiple construction functions for UnstructuredGrid, for
example create_grid_cart2d(), create_grid_hexa3d(), read_grid() and
create_grid_cornerpoint(). The function destroy_grid() frees the
resources used by a grid.

For C++ users, the Opm::GridManager class can be used to encapsulate
creation and destruction of an UnstructuredGrid. The method
Opm::GridManager::c_grid() provides access to the underlying
UnstructuredGrid struct. This class also provides an easy way
to initialize a grid from an Eclipse-format input deck, via the
constructor taking an Opm::EclipseGridParser.


<h3>Well handling</h3>

A well in opm-core can have an arbitrary number of perforations
connecting it to the reservoir. Wells can be open or closed, and they
can be controlled by bottom hole pressure (BHP), volumetric
(reservoir) rates or surface rates. Multiple controls can be
specified, then one control will be the active control and the others
will be interpreted as constraints.

A small collection of structs is used to communicate well information,
the most important ones being the Wells struct and its contained
WellControls structs. A set of C functions to manipulate these are
provided, the most important ones are create_wells(),
add_well(), append_well_controls() and destroy_wells().

The C++ class Opm::WellsManager encapsulates creation and management
of well objects, making the underlying Wells struct available in the
method Opm::WellsManager::c_wells(). It goes beyond this, however, and
also provides methods such as Opm::WellsManager::conditionsMet() that
checks if all well constraints are met, and switches controls if not.


<h3>Pressure solvers</h3>

The currently recommended pressure solvers available in opm-core are
the C++ classes
- Opm::IncompTpfa (incompressible TPFA discretization)
- Opm::CompressibleTpfa (compressible TPFA discretization)

A number of other solvers and interfaces are available, but are not
recommended for use at this time.


<h3>Transport solvers</h3>

<h3>Various utilities</h3>

*/

#endif // OPM_DOXYGEN_MAIN_HEADER_INCLUDED
