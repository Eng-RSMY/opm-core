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

#ifndef OPM_WELLSTATE_HEADER_INCLUDED
#define OPM_WELLSTATE_HEADER_INCLUDED

#include <opm/core/wells.h>
#include <vector>

namespace Opm
{

    /// The state of a set of wells.
    class WellState
    {
    public:
        /// Allocate and initialize if wells is non-null.
        template <class State>
        void init(const Wells* wells, const State& state)
        {
            if (wells) {
                const int nw = wells->number_of_wells;
                bhp_.resize(nw);
                // Initialize bhp to be target pressure
                // if bhp-controlled well, otherwise set
                // to pressure in first perforation cell.
                for (int w = 0; w < nw; ++w) {
                    const WellControls* ctrl = wells->ctrls[w];

                    if ((ctrl->current < 0) || // SHUT
                        (ctrl->type[ctrl->current] != BHP)) {
                        const int cell = wells->well_cells[wells->well_connpos[w]];
                        bhp_[w] = state.pressure()[cell];
                    }
                    else {
                        bhp_[w] = ctrl->target[ctrl->current];
                    }
                }
                perfrates_.resize(wells->well_connpos[nw], 0.0);
                perfpress_.resize(wells->well_connpos[nw], -1e100);
            }
        }

        /// One bhp pressure per well.
        std::vector<double>& bhp() { return bhp_; }
        const std::vector<double>& bhp() const { return bhp_; }

        /// One rate per well connection.
        std::vector<double>& perfRates() { return perfrates_; }
        const std::vector<double>& perfRates() const { return perfrates_; }

        /// One pressure per well connection.
        std::vector<double>& perfPress() { return perfpress_; }
        const std::vector<double>& perfPress() const { return perfpress_; }

    private:
        std::vector<double> bhp_;
        std::vector<double> perfrates_;
        std::vector<double> perfpress_;
    };

} // namespace Opm

#endif // OPM_WELLSTATE_HEADER_INCLUDED
