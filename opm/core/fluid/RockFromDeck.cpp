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


#include <opm/core/fluid/RockFromDeck.hpp>
#include <opm/core/eclipse/EclipseGridInspector.hpp>

namespace Opm
{

    // Helper functions
    namespace
    {
        enum PermeabilityKind { ScalarPerm, DiagonalPerm, TensorPerm, None, Invalid };

        PermeabilityKind classifyPermeability(const Dune::EclipseGridParser& parser);
        void setScalarPermIfNeeded(std::tr1::array<int,9>& kmap,
                                   int i, int j, int k);
        PermeabilityKind fillTensor(const Dune::EclipseGridParser&                 parser,
                                    std::vector<const std::vector<double>*>& tensor,
                                    std::tr1::array<int,9>&                     kmap);
    } // anonymous namespace



    // ---- ReadFromDeck methods ----


    /// Default constructor.
    RockFromDeck::RockFromDeck()
    {
    }


    /// Initialize from deck and cell mapping.
    /// \param  deck         Deck input parser
    /// \param  global_cell  mapping from cell indices (typically from a processed grid)
    ///                      to logical cartesian indices consistent with the deck.
    void RockFromDeck::init(const Dune::EclipseGridParser& deck,
                            const std::vector<int>& global_cell)
    {
        assignPorosity(deck, global_cell);
        permfield_valid_.assign(global_cell.size(), false);
        const double perm_threshold = 0.0; // Maybe turn into parameter?
        assignPermeability(deck, global_cell, perm_threshold);
    }


    void RockFromDeck::assignPorosity(const Dune::EclipseGridParser& parser,
                                      const std::vector<int>& global_cell)
    {
        porosity_.assign(global_cell.size(), 1.0);

        if (parser.hasField("PORO")) {
            const std::vector<double>& poro = parser.getFloatingPointValue("PORO");

            for (int c = 0; c < int(porosity_.size()); ++c) {
                porosity_[c] = poro[global_cell[c]];
            }
        }
    }



    void RockFromDeck::assignPermeability(const Dune::EclipseGridParser& parser,
                                          const std::vector<int>& global_cell,
                                          double perm_threshold)
    {
        const int dim = 3;
        Dune::EclipseGridInspector insp(parser);
        std::tr1::array<int, 3> dims = insp.gridSize();
        int num_global_cells = dims[0]*dims[1]*dims[2];
        ASSERT (num_global_cells > 0);

        permeability_.assign(dim * dim * global_cell.size(), 0.0);

        std::vector<const std::vector<double>*> tensor;
        tensor.reserve(10);

        const std::vector<double> zero(num_global_cells, 0.0);
        tensor.push_back(&zero);

        std::tr1::array<int,9> kmap;
        PermeabilityKind pkind = fillTensor(parser, tensor, kmap);
        if (pkind == Invalid) {
            THROW("Invalid permeability field.");
        }

        // Assign permeability values only if such values are
        // given in the input deck represented by 'parser'.  In
        // other words: Don't set any (arbitrary) default values.
        // It is infinitely better to experience a reproducible
        // crash than subtle errors resulting from a (poorly
        // chosen) default value...
        //
        if (tensor.size() > 1) {
            const int nc  = global_cell.size();
            int       off = 0;

            for (int c = 0; c < nc; ++c, off += dim*dim) {
                // SharedPermTensor K(dim, dim, &permeability_[off]);
                int       kix  = 0;
                const int glob = global_cell[c];

                for (int i = 0; i < dim; ++i) {
                    for (int j = 0; j < dim; ++j, ++kix) {
                        // K(i,j) = (*tensor[kmap[kix]])[glob];
                        permeability_[off + kix] = (*tensor[kmap[kix]])[glob];
                    }
                    // K(i,i) = std::max(K(i,i), perm_threshold);
                    permeability_[off + 3*i + i] = std::max(permeability_[off + 3*i + i], perm_threshold);
                }

                permfield_valid_[c] = std::vector<unsigned char>::value_type(1);
            }
        }
    }


    namespace {

        /// @brief
        ///    Classify and verify a given permeability specification
        ///    from a structural point of view.  In particular, we
        ///    verify that there are no off-diagonal permeability
        ///    components such as @f$k_{xy}@f$ unless the
        ///    corresponding diagonal components are known as well.
        ///
        /// @param parser [in]
        ///    An Eclipse data parser capable of answering which
        ///    permeability components are present in a given input
        ///    deck.
        ///
        /// @return
        ///    An enum value with the following possible values:
        ///        ScalarPerm     only one component was given.
        ///        DiagonalPerm   more than one component given.
        ///        TensorPerm     at least one cross-component given.
        ///        None           no components given.
        ///        Invalid        invalid set of components given.
        PermeabilityKind classifyPermeability(const Dune::EclipseGridParser& parser)
        {
            const bool xx = parser.hasField("PERMX" );
            const bool xy = parser.hasField("PERMXY");
            const bool xz = parser.hasField("PERMXZ");

            const bool yx = parser.hasField("PERMYX");
            const bool yy = parser.hasField("PERMY" );
            const bool yz = parser.hasField("PERMYZ");

            const bool zx = parser.hasField("PERMZX");
            const bool zy = parser.hasField("PERMZY");
            const bool zz = parser.hasField("PERMZ" );

            int num_cross_comp = xy + xz + yx + yz + zx + zy;
            int num_comp       = xx + yy + zz + num_cross_comp;
            PermeabilityKind retval = None;
            if (num_cross_comp > 0) {
                retval = TensorPerm;
            } else {
                if (num_comp == 1) {
                    retval = ScalarPerm;
                } else if (num_comp >= 2) {
                    retval = DiagonalPerm;
                }
            }

            bool ok = true;
            if (num_comp > 0) {
                // At least one tensor component specified on input.
                // Verify that any remaining components are OK from a
                // structural point of view.  In particular, there
                // must not be any cross-components (e.g., k_{xy})
                // unless the corresponding diagonal component (e.g.,
                // k_{xx}) is present as well...
                //
                ok =        xx || !(xy || xz || yx || zx) ;
                ok = ok && (yy || !(yx || yz || xy || zy));
                ok = ok && (zz || !(zx || zy || xz || yz));
            }
            if (!ok) {
                retval = Invalid;
            }

            return retval;
        }


        /// @brief
        ///    Copy isotropic (scalar) permeability to other diagonal
        ///    components if the latter have not (yet) been assigned a
        ///    separate value.  Specifically, this function assigns
        ///    copies of the @f$i@f$ permeability component (e.g.,
        ///    'PERMX') to the @f$j@f$ and @f$k@f$ permeability (e.g.,
        ///    'PERMY' and 'PERMZ') components if these have not
        ///    previously been assigned.
        ///
        /// @param kmap
        ///    Permeability indirection map.  In particular @code
        ///    kmap[i] @endcode is the index (an integral number in
        ///    the set [1..9]) into the permeability tensor
        ///    representation of function @code fillTensor @endcode
        ///    which represents permeability component @code i
        ///    @endcode.
        ///
        /// @param [in] i
        /// @param [in] j
        /// @param [in] k
        void setScalarPermIfNeeded(std::tr1::array<int,9>& kmap,
                                   int i, int j, int k)
        {
            if (kmap[j] == 0) { kmap[j] = kmap[i]; }
            if (kmap[k] == 0) { kmap[k] = kmap[i]; }
        }


        /// @brief
        ///   Extract pointers to appropriate tensor components from
        ///   input deck.  The permeability tensor is, generally,
        ///   @code
        ///        [ kxx  kxy  kxz ]
        ///    K = [ kyx  kyy  kyz ]
        ///        [ kzx  kzy  kzz ]
        ///   @endcode
        ///   We store these values in a linear array using natural
        ///   ordering with the column index cycling the most rapidly.
        ///   In particular we use the representation
        ///   @code
        ///        [  0    1    2    3    4    5    6    7    8  ]
        ///    K = [ kxx, kxy, kxz, kyx, kyy, kyz, kzx, kzy, kzz ]
        ///   @endcode
        ///   Moreover, we explicitly enforce symmetric tensors by
        ///   assigning
        ///   @code
        ///     3     1       6     2       7     5
        ///    kyx = kxy,    kzx = kxz,    kzy = kyz
        ///   @endcode
        ///   However, we make no attempt at enforcing positive
        ///   definite tensors.
        ///
        /// @param [in]  parser
        ///    An Eclipse data parser capable of answering which
        ///    permeability components are present in a given input
        ///    deck as well as retrieving the numerical value of each
        ///    permeability component in each grid cell.
        ///
        /// @param [out] tensor
        /// @param [out] kmap
        PermeabilityKind fillTensor(const Dune::EclipseGridParser&                 parser,
                                    std::vector<const std::vector<double>*>& tensor,
                                    std::tr1::array<int,9>&                     kmap)
        {
            PermeabilityKind kind = classifyPermeability(parser);
            if (kind == Invalid) {
                THROW("Invalid set of permeability fields given.");
            }
            ASSERT (tensor.size() == 1);
            for (int i = 0; i < 9; ++i) { kmap[i] = 0; }

            enum { xx, xy, xz,    // 0, 1, 2
                   yx, yy, yz,    // 3, 4, 5
                   zx, zy, zz };  // 6, 7, 8

            // -----------------------------------------------------------
            // 1st row: [kxx, kxy, kxz]
            if (parser.hasField("PERMX" )) {
                kmap[xx] = tensor.size();
                tensor.push_back(&parser.getFloatingPointValue("PERMX" ));

                setScalarPermIfNeeded(kmap, xx, yy, zz);
            }
            if (parser.hasField("PERMXY")) {
                kmap[xy] = kmap[yx] = tensor.size();  // Enforce symmetry.
                tensor.push_back(&parser.getFloatingPointValue("PERMXY"));
            }
            if (parser.hasField("PERMXZ")) {
                kmap[xz] = kmap[zx] = tensor.size();  // Enforce symmetry.
                tensor.push_back(&parser.getFloatingPointValue("PERMXZ"));
            }

            // -----------------------------------------------------------
            // 2nd row: [kyx, kyy, kyz]
            if (parser.hasField("PERMYX")) {
                kmap[yx] = kmap[xy] = tensor.size();  // Enforce symmetry.
                tensor.push_back(&parser.getFloatingPointValue("PERMYX"));
            }
            if (parser.hasField("PERMY" )) {
                kmap[yy] = tensor.size();
                tensor.push_back(&parser.getFloatingPointValue("PERMY" ));

                setScalarPermIfNeeded(kmap, yy, zz, xx);
            }
            if (parser.hasField("PERMYZ")) {
                kmap[yz] = kmap[zy] = tensor.size();  // Enforce symmetry.
                tensor.push_back(&parser.getFloatingPointValue("PERMYZ"));
            }

            // -----------------------------------------------------------
            // 3rd row: [kzx, kzy, kzz]
            if (parser.hasField("PERMZX")) {
                kmap[zx] = kmap[xz] = tensor.size();  // Enforce symmetry.
                tensor.push_back(&parser.getFloatingPointValue("PERMZX"));
            }
            if (parser.hasField("PERMZY")) {
                kmap[zy] = kmap[yz] = tensor.size();  // Enforce symmetry.
                tensor.push_back(&parser.getFloatingPointValue("PERMZY"));
            }
            if (parser.hasField("PERMZ" )) {
                kmap[zz] = tensor.size();
                tensor.push_back(&parser.getFloatingPointValue("PERMZ" ));

                setScalarPermIfNeeded(kmap, zz, xx, yy);
            }
            return kind;
        }

    } // anonymous namespace

} // namespace Opm
