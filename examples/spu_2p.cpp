/*===========================================================================
//
// File: spu_2p.cpp
//
// Created: 2011-10-05 10:29:01+0200
//
// Authors: Ingeborg S. Ligaarden <Ingeborg.Ligaarden@sintef.no>
//          Jostein R. Natvig     <Jostein.R.Natvig@sintef.no>
//          Halvor M. Nilsen      <HalvorMoll.Nilsen@sintef.no>
//          Atgeirr F. Rasmussen  <atgeirr@sintef.no>
//          Bård Skaflestad       <Bard.Skaflestad@sintef.no>
//
//==========================================================================*/


/*
  Copyright 2011, 2012 SINTEF ICT, Applied Mathematics.
  Copyright 2011, 2012 Statoil ASA.

  This file is part of the Open Porous Media Project (OPM).

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

#if HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include <opm/core/pressure/IncompTpfa.hpp>
#include <opm/core/pressure/FlowBCManager.hpp>

#include <opm/core/grid.h>
#include <opm/core/GridManager.hpp>
#include <opm/core/newwells.h>
#include <opm/core/wells/WellsManager.hpp>
#include <opm/core/utility/ErrorMacros.hpp>
#include <opm/core/utility/initState.hpp>
#include <opm/core/simulator/SimulatorTimer.hpp>
#include <opm/core/utility/StopWatch.hpp>
#include <opm/core/utility/Units.hpp>
#include <opm/core/utility/writeVtkData.hpp>
#include <opm/core/utility/miscUtilities.hpp>
#include <opm/core/utility/parameters/ParameterGroup.hpp>

#include <opm/core/fluid/SimpleFluid2p.hpp>
#include <opm/core/fluid/IncompPropertiesBasic.hpp>
#include <opm/core/fluid/IncompPropertiesFromDeck.hpp>
#include <opm/core/fluid/RockCompressibility.hpp>

#include <opm/core/linalg/LinearSolverFactory.hpp>

#include <opm/core/transport/transport_source.h>
#include <opm/core/transport/CSRMatrixUmfpackSolver.hpp>
#include <opm/core/transport/NormSupport.hpp>
#include <opm/core/transport/ImplicitAssembly.hpp>
#include <opm/core/transport/ImplicitTransport.hpp>
#include <opm/core/transport/JacobianSystem.hpp>
#include <opm/core/transport/CSRMatrixBlockAssembler.hpp>
#include <opm/core/transport/SinglePointUpwindTwoPhase.hpp>

#include <opm/core/utility/ColumnExtract.hpp>
#include <opm/core/simulator/TwophaseState.hpp>
#include <opm/core/simulator/WellState.hpp>
#include <opm/core/transport/GravityColumnSolver.hpp>

#include <opm/core/transport/reorder/TransportSolverTwophaseReorder.hpp>

#include <boost/filesystem/convenience.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include <cassert>
#include <cstddef>

#include <algorithm>
#include <tr1/array>
#include <functional>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <iterator>
#include <vector>
#include <numeric>


#ifdef HAVE_ERT
#include <opm/core/utility/writeECLData.hpp>
#endif



static void outputState(const UnstructuredGrid& grid,
                        const Opm::TwophaseState& state,
                        const Opm::SimulatorTimer& simtimer,
                        const std::string& output_dir)
{
    // Write data in VTK format.
  int step = simtimer.currentStepNum();
    std::ostringstream vtkfilename;
    vtkfilename << output_dir << "/output-" << std::setw(3) << std::setfill('0') << step << ".vtu";
    std::ofstream vtkfile(vtkfilename.str().c_str());
    if (!vtkfile) {
        THROW("Failed to open " << vtkfilename.str());
    }
    Opm::DataMap dm;
    dm["saturation"] = &state.saturation();
    dm["pressure"] = &state.pressure();
    std::vector<double> cell_velocity;
    Opm::estimateCellVelocity(grid, state.faceflux(), cell_velocity);
    dm["velocity"] = &cell_velocity;
    Opm::writeVtkData(grid, dm, vtkfile);
#ifdef HAVE_ERT
    Opm::writeECLData(grid , dm , simtimer , output_dir , "OPM" );
#endif

    // Write data (not grid) in Matlab format
    for (Opm::DataMap::const_iterator it = dm.begin(); it != dm.end(); ++it) {
        std::ostringstream fname;
        fname << output_dir << "/" << it->first << "-" << std::setw(3) << std::setfill('0') << step << ".dat";
        std::ofstream file(fname.str().c_str());
        if (!file) {
            THROW("Failed to open " << fname.str());
        }
        const std::vector<double>& d = *(it->second);
        std::copy(d.begin(), d.end(), std::ostream_iterator<double>(file, "\n"));
    }
}


static void outputWaterCut(const Opm::Watercut& watercut,
                           const std::string& output_dir)
{
    // Write water cut curve.
    std::string fname = output_dir  + "/watercut.txt";
    std::ofstream os(fname.c_str());
    if (!os) {
        THROW("Failed to open " << fname);
    }
    watercut.write(os);
}


static void outputWellReport(const Opm::WellReport& wellreport,
                             const std::string& output_dir)
{
    // Write well report.
    std::string fname = output_dir  + "/wellreport.txt";
    std::ofstream os(fname.c_str());
    if (!os) {
        THROW("Failed to open " << fname);
    }
    wellreport.write(os);
}



// --------------- Types needed to define transport solver ---------------

class SimpleFluid2pWrappingProps
{
public:
    SimpleFluid2pWrappingProps(const Opm::IncompPropertiesInterface& props)
  : props_(props),
    smin_(props.numCells()*props.numPhases()),
    smax_(props.numCells()*props.numPhases())
    {
        if (props.numPhases() != 2) {
            THROW("SimpleFluid2pWrapper requires 2 phases.");
        }
        const int num_cells = props.numCells();
        std::vector<int> cells(num_cells);
        for (int c = 0; c < num_cells; ++c) {
            cells[c] = c;
        }
        props.satRange(num_cells, &cells[0], &smin_[0], &smax_[0]);
    }

    double density(int phase) const
    {
        return props_.density()[phase];
    }

    template <class Sat,
              class Mob,
              class DMob>
    void mobility(int c, const Sat& s, Mob& mob, DMob& dmob) const
    {
        props_.relperm(1, &s[0], &c, &mob[0], &dmob[0]);
        const double* mu = props_.viscosity();
        mob[0] /= mu[0];
        mob[1] /= mu[1];
        // Recall that we use Fortran ordering for kr derivatives,
        // therefore dmob[i*2 + j] is row j and column i of the
        // matrix.
        // Each row corresponds to a kr function, so which mu to
        // divide by also depends on the row, j.
        dmob[0*2 + 0] /= mu[0];
        dmob[0*2 + 1] /= mu[1];
        dmob[1*2 + 0] /= mu[0];
        dmob[1*2 + 1] /= mu[1];
    }

    template <class Sat,
              class Pcap,
              class DPcap>
    void pc(int c, const Sat& s, Pcap& pcap, DPcap& dpcap) const
    {
        double pcow[2];
        double dpcow[4];
        props_.capPress(1, &s[0], &c, pcow, dpcow);
        pcap = pcow[0];
        ASSERT(pcow[1] == 0.0);
        dpcap = dpcow[0];
        ASSERT(dpcow[1] == 0.0);
        ASSERT(dpcow[2] == 0.0);
        ASSERT(dpcow[3] == 0.0);
    }

    double s_min(int c) const
    {
        return smin_[2*c + 0];
    }

    double s_max(int c) const
    {
        return smax_[2*c + 0];
    }

private:
    const Opm::IncompPropertiesInterface& props_;
    std::vector<double> smin_;
    std::vector<double> smax_;
};

typedef SimpleFluid2pWrappingProps TwophaseFluid;
typedef Opm::SinglePointUpwindTwoPhase<TwophaseFluid> TransportModel;

using namespace Opm::ImplicitTransportDefault;

typedef NewtonVectorCollection< ::std::vector<double> >      NVecColl;
typedef JacobianSystem        < struct CSRMatrix, NVecColl > JacSys;

template <class Vector>
class MaxNorm {
public:
    static double
    norm(const Vector& v) {
        return AccumulationNorm <Vector, MaxAbs>::norm(v);
    }
};

typedef Opm::ImplicitTransport<TransportModel,
                               JacSys        ,
                               MaxNorm       ,
                               VectorNegater ,
                               VectorZero    ,
                               MatrixZero    ,
                               VectorAssign  > TransportSolver;



// ----------------- Main program -----------------
int
main(int argc, char** argv)
{
    using namespace Opm;

    std::cout << "\n================    Test program for incompressible two-phase flow     ===============\n\n";
    Opm::parameter::ParameterGroup param(argc, argv, false);
    std::cout << "---------------    Reading parameters     ---------------" << std::endl;

    // Reading various control parameters.
    const bool guess_old_solution = param.getDefault("guess_old_solution", false);
    const bool use_reorder = param.getDefault("use_reorder", true);
    const bool output = param.getDefault("output", true);
    std::string output_dir;
    int output_interval = 1;
    if (output) {
        output_dir = param.getDefault("output_dir", std::string("output"));
        // Ensure that output dir exists
        boost::filesystem::path fpath(output_dir);
        try {
            create_directories(fpath);
        }
        catch (...) {
            THROW("Creating directories failed: " << fpath);
        }
        output_interval = param.getDefault("output_interval", output_interval);
    }
    const int num_transport_substeps = param.getDefault("num_transport_substeps", 1);

    // If we have a "deck_filename", grid and props will be read from that.
    bool use_deck = param.has("deck_filename");
    boost::scoped_ptr<Opm::GridManager> grid;
    boost::scoped_ptr<Opm::IncompPropertiesInterface> props;
    boost::scoped_ptr<Opm::WellsManager> wells;
    boost::scoped_ptr<Opm::RockCompressibility> rock_comp;
    Opm::SimulatorTimer simtimer;
    Opm::TwophaseState state;
    bool check_well_controls = false;
    int max_well_control_iterations = 0;
    double gravity[3] = { 0.0 };
    if (use_deck) {
        std::string deck_filename = param.get<std::string>("deck_filename");
        Opm::EclipseGridParser deck(deck_filename);
        // Grid init
        grid.reset(new Opm::GridManager(deck));
        // Rock and fluid init
        props.reset(new Opm::IncompPropertiesFromDeck(deck, *grid->c_grid()));
        // Wells init.
        wells.reset(new Opm::WellsManager(deck, *grid->c_grid(), props->permeability()));
        check_well_controls = param.getDefault("check_well_controls", false);
        max_well_control_iterations = param.getDefault("max_well_control_iterations", 10);
        // Timer init.
        if (deck.hasField("TSTEP")) {
            simtimer.init(deck);
        } else {
            simtimer.init(param);
        }
        // Rock compressibility.
        rock_comp.reset(new Opm::RockCompressibility(deck));
        // Gravity.
        gravity[2] = deck.hasField("NOGRAV") ? 0.0 : Opm::unit::gravity;
        // Init state variables (saturation and pressure).
        if (param.has("init_saturation")) {
            initStateBasic(*grid->c_grid(), *props, param, gravity[2], state);
        } else {
            initStateFromDeck(*grid->c_grid(), *props, deck, gravity[2], state);
        }
    } else {
        // Grid init.
        const int nx = param.getDefault("nx", 100);
        const int ny = param.getDefault("ny", 100);
        const int nz = param.getDefault("nz", 1);
        const double dx = param.getDefault("dx", 1.0);
        const double dy = param.getDefault("dy", 1.0);
        const double dz = param.getDefault("dz", 1.0);
        grid.reset(new Opm::GridManager(nx, ny, nz, dx, dy, dz));
        // Rock and fluid init.
        props.reset(new Opm::IncompPropertiesBasic(param, grid->c_grid()->dimensions, grid->c_grid()->number_of_cells));
        // Wells init.
        wells.reset(new Opm::WellsManager());
        // Timer init.
        simtimer.init(param);
        // Rock compressibility.
        rock_comp.reset(new Opm::RockCompressibility(param));
        // Gravity.
        gravity[2] = param.getDefault("gravity", 0.0);
        // Init state variables (saturation and pressure).
        initStateBasic(*grid->c_grid(), *props, param, gravity[2], state);
    }

    // Extra fluid init for transport solver.
    TwophaseFluid fluid(*props);

    // Warn if gravity but no density difference.
    bool use_gravity = (gravity[0] != 0.0 || gravity[1] != 0.0 || gravity[2] != 0.0);
    if (use_gravity) {
        if (props->density()[0] == props->density()[1]) {
            std::cout << "**** Warning: nonzero gravity, but zero density difference." << std::endl;
        }
    }
    bool use_segregation_split = false;
    bool use_column_solver = false;
    bool use_gauss_seidel_gravity = false;
    if (use_gravity && use_reorder) {
        use_segregation_split = param.getDefault("use_segregation_split", use_segregation_split);
        if (use_segregation_split) {
            use_column_solver = param.getDefault("use_column_solver", use_column_solver);
            if (use_column_solver) {
                use_gauss_seidel_gravity = param.getDefault("use_gauss_seidel_gravity", use_gauss_seidel_gravity);
            }
        }
    }

    // Check that rock compressibility is not used with solvers that do not handle it.
    int nl_pressure_maxiter = 0;
    double nl_pressure_residual_tolerance = 0.0;
    double nl_pressure_change_tolerance = 0.0;
    if (rock_comp->isActive()) {
        if (!use_reorder) {
            THROW("Cannot run implicit (non-reordering) transport solver with rock compressibility yet.");
        }
        nl_pressure_residual_tolerance = param.getDefault("nl_pressure_residual_tolerance", 0.0);
        nl_pressure_change_tolerance = param.getDefault("nl_pressure_change_tolerance", 1.0); // In Pascal.
        nl_pressure_maxiter = param.getDefault("nl_pressure_maxiter", 10);
    }

    // Source-related variables init.
    int num_cells = grid->c_grid()->number_of_cells;

    // Extra rock init.
    std::vector<double> porevol;
    if (rock_comp->isActive()) {
        computePorevolume(*grid->c_grid(), props->porosity(), *rock_comp, state.pressure(), porevol);
    } else {
        computePorevolume(*grid->c_grid(), props->porosity(), porevol);
    }
    double tot_porevol_init = std::accumulate(porevol.begin(), porevol.end(), 0.0);

    // Initialising src
    std::vector<double> src(num_cells, 0.0);
    if (wells->c_wells()) {
        // Do nothing, wells will be the driving force, not source terms.
        // Opm::wellsToSrc(*wells->c_wells(), num_cells, src);
    } else {
        const double default_injection = use_gravity ? 0.0 : 0.1;
        const double flow_per_sec = param.getDefault<double>("injected_porevolumes_per_day", default_injection)
            *tot_porevol_init/Opm::unit::day;
        src[0] = flow_per_sec;
        src[num_cells - 1] = -flow_per_sec;
    }

    TransportSource* tsrc = create_transport_source(2, 2);
    double ssrc[]   = { 1.0, 0.0 };
    double ssink[]  = { 0.0, 1.0 };
    double zdummy[] = { 0.0, 0.0 };
    for (int cell = 0; cell < num_cells; ++cell) {
        if (src[cell] > 0.0) {
            append_transport_source(cell, 2, 0, src[cell], ssrc, zdummy, tsrc);
        } else if (src[cell] < 0.0) {
            append_transport_source(cell, 2, 0, src[cell], ssink, zdummy, tsrc);
        }
    }
    std::vector<double> reorder_src = src;

    // Boundary conditions.
    Opm::FlowBCManager bcs;
    if (param.getDefault("use_pside", false)) {
        int pside = param.get<int>("pside");
        double pside_pressure = param.get<double>("pside_pressure");
        bcs.pressureSide(*grid->c_grid(), Opm::FlowBCManager::Side(pside), pside_pressure);
    }

    // Solvers init.
    // Linear solver.
    Opm::LinearSolverFactory linsolver(param);
    // Pressure solver.
    const double *grav = use_gravity ? &gravity[0] : 0;
    Opm::IncompTpfa psolver(*grid->c_grid(), *props, rock_comp.get(), linsolver,
                            nl_pressure_residual_tolerance, nl_pressure_change_tolerance,
                            nl_pressure_maxiter,
                            grav, wells->c_wells(), src, bcs.c_bcs());
    // Reordering solver.
    const double nl_tolerance = param.getDefault("nl_tolerance", 1e-9);
    const int nl_maxiter = param.getDefault("nl_maxiter", 30);
    Opm::TransportSolverTwophaseReorder reorder_model(*grid->c_grid(), *props, nl_tolerance, nl_maxiter);
    if (use_gauss_seidel_gravity) {
        reorder_model.initGravity(grav);
    }
    // Non-reordering solver.
    TransportModel  model  (fluid, *grid->c_grid(), porevol, grav, guess_old_solution);
    if (use_gravity) {
        model.initGravityTrans(*grid->c_grid(), psolver.getHalfTrans());
    }
    TransportSolver tsolver(model);
    // Column-based gravity segregation solver.
    std::vector<std::vector<int> > columns;
    if (use_column_solver) {
        Opm::extractColumn(*grid->c_grid(), columns);
    }
    Opm::GravityColumnSolver<TransportModel> colsolver(model, *grid->c_grid(), nl_tolerance, nl_maxiter);

    // Control init.
    Opm::ImplicitTransportDetails::NRReport  rpt;
    Opm::ImplicitTransportDetails::NRControl ctrl;
    if (!use_reorder || use_segregation_split) {
        ctrl.max_it = param.getDefault("max_it", 20);
        ctrl.verbosity = param.getDefault("verbosity", 0);
        ctrl.max_it_ls = param.getDefault("max_it_ls", 5);
    }

    // Linear solver init.
    using Opm::ImplicitTransportLinAlgSupport::CSRMatrixUmfpackSolver;
    CSRMatrixUmfpackSolver linsolve;

    // The allcells vector is used in calls to computeTotalMobility()
    // and computeTotalMobilityOmega().
    std::vector<int> allcells(num_cells);
    for (int cell = 0; cell < num_cells; ++cell) {
        allcells[cell] = cell;
    }

    // Warn if any parameters are unused.
    if (param.anyUnused()) {
        std::cout << "--------------------   Unused parameters:   --------------------\n";
        param.displayUsage();
        std::cout << "----------------------------------------------------------------" << std::endl;
    }

    // Write parameters used for later reference.
    if (output) {
        param.writeParam(output_dir + "/simulation.param");
    }

    // Main simulation loop.
    Opm::time::StopWatch pressure_timer;
    double ptime = 0.0;
    Opm::time::StopWatch transport_timer;
    double ttime = 0.0;
    Opm::time::StopWatch total_timer;
    total_timer.start();
    std::cout << "\n\n================    Starting main simulation loop     ===============" << std::endl;
    double init_satvol[2] = { 0.0 };
    double satvol[2] = { 0.0 };
    double injected[2] = { 0.0 };
    double produced[2] = { 0.0 };
    double tot_injected[2] = { 0.0 };
    double tot_produced[2] = { 0.0 };
    Opm::computeSaturatedVol(porevol, state.saturation(), init_satvol);
    std::cout << "\nInitial saturations are    " << init_satvol[0]/tot_porevol_init
              << "    " << init_satvol[1]/tot_porevol_init << std::endl;
    Opm::Watercut watercut;
    watercut.push(0.0, 0.0, 0.0);
    Opm::WellReport wellreport;
    Opm::WellState well_state;
    well_state.init(wells->c_wells(), state);
    std::vector<double> fractional_flows;
    std::vector<double> well_resflows_phase;
    int num_wells = 0;
    if (wells->c_wells()) {
        num_wells = wells->c_wells()->number_of_wells;
        well_resflows_phase.resize((wells->c_wells()->number_of_phases)*(wells->c_wells()->number_of_wells), 0.0);
        wellreport.push(*props, *wells->c_wells(), state.saturation(), 0.0, well_state.bhp(), well_state.perfRates());
    }
    for (; !simtimer.done(); ++simtimer) {
        // Report timestep and (optionally) write state to disk.
        simtimer.report(std::cout);
        if (output && (simtimer.currentStepNum() % output_interval == 0)) {
            outputState(*grid->c_grid(), state, simtimer , output_dir);
        }

        // Solve pressure.
        if (check_well_controls) {
            computeFractionalFlow(*props, allcells, state.saturation(), fractional_flows);
        }
        if (check_well_controls) {
            wells->applyExplicitReinjectionControls(well_resflows_phase, well_resflows_phase);
        }
        bool well_control_passed = !check_well_controls;
        int well_control_iteration = 0;
        do {
            pressure_timer.start();
            std::vector<double> initial_pressure = state.pressure();
            psolver.solve(simtimer.currentStepLength(), state, well_state);
            if (!rock_comp->isActive()) {
                // Compute average pressures of previous and last
                // step, and total volume.
                double av_prev_press = 0.;
                double av_press = 0.;
                double tot_vol = 0.;
                for (int cell = 0; cell < num_cells; ++cell) {
                    av_prev_press += initial_pressure[cell]*grid->c_grid()->cell_volumes[cell];
                    av_press      += state.pressure()[cell]*grid->c_grid()->cell_volumes[cell];
                    tot_vol       += grid->c_grid()->cell_volumes[cell];
                }
                // Renormalization constant
                const double ren_const = (av_prev_press - av_press)/tot_vol;
                for (int cell = 0; cell < num_cells; ++cell) {
                    state.pressure()[cell] += ren_const;
                }
                for (int well = 0; well < num_wells; ++well) {
                    well_state.bhp()[well] += ren_const;
                }
            }
            pressure_timer.stop();
            double pt = pressure_timer.secsSinceStart();
            std::cout << "Pressure solver took:  " << pt << " seconds." << std::endl;
            ptime += pt;


            if (check_well_controls) {
                Opm::computePhaseFlowRatesPerWell(*wells->c_wells(),
                                                  fractional_flows,
                                                  well_state.perfRates(),
                                                  well_resflows_phase);
                std::cout << "Checking well conditions." << std::endl;
                // For testing we set surface := reservoir
                well_control_passed = wells->conditionsMet(well_state.bhp(), well_resflows_phase, well_resflows_phase);
                ++well_control_iteration;
                if (!well_control_passed && well_control_iteration > max_well_control_iterations) {
                    THROW("Could not satisfy well conditions in " << max_well_control_iterations << " tries.");
                }
                if (!well_control_passed) {
                    std::cout << "Well controls not passed, solving again." << std::endl;
                } else {
                    std::cout << "Well conditions met." << std::endl;
                }
            }
        } while (!well_control_passed);

        // Update pore volumes if rock is compressible.
        if (rock_comp->isActive()) {
            computePorevolume(*grid->c_grid(), props->porosity(), *rock_comp, state.pressure(), porevol);
        }

        // Process transport sources (to include bdy terms and well flows).
        Opm::computeTransportSource(*grid->c_grid(), src, state.faceflux(), 1.0,
                                    wells->c_wells(), well_state.perfRates(), reorder_src);
        if (!use_reorder) {
            clear_transport_source(tsrc);
            for (int cell = 0; cell < num_cells; ++cell) {
                if (reorder_src[cell] > 0.0) {
                    append_transport_source(cell, 2, 0, reorder_src[cell], ssrc, zdummy, tsrc);
                } else if (reorder_src[cell] < 0.0) {
                    append_transport_source(cell, 2, 0, reorder_src[cell], ssink, zdummy, tsrc);
                }
            }
        }

        // Solve transport.
        transport_timer.start();
        double stepsize = simtimer.currentStepLength();
        if (num_transport_substeps != 1) {
            stepsize /= double(num_transport_substeps);
            std::cout << "Making " << num_transport_substeps << " transport substeps." << std::endl;
        }
        for (int tr_substep = 0; tr_substep < num_transport_substeps; ++tr_substep) {
            if (use_reorder) {
                reorder_model.solve(&state.faceflux()[0], &porevol[0], &reorder_src[0],
                                    stepsize, state.saturation());
                Opm::computeInjectedProduced(*props, state.saturation(), reorder_src, stepsize, injected, produced);
                if (use_segregation_split) {
                    if (use_column_solver) {
                        if (use_gauss_seidel_gravity) {
                            reorder_model.solveGravity(columns, &porevol[0], stepsize, state.saturation());
                        } else {
                            colsolver.solve(columns, stepsize, state.saturation());
                        }
                    } else {
                        std::vector<double> fluxes = state.faceflux();
                        std::fill(state.faceflux().begin(), state.faceflux().end(), 0.0);
                        tsolver.solve(*grid->c_grid(), tsrc, stepsize, ctrl, state, linsolve, rpt);
                        std::cout << rpt;
                        state.faceflux() = fluxes;
                    }
                }
            } else {
                tsolver.solve(*grid->c_grid(), tsrc, stepsize, ctrl, state, linsolve, rpt);
                std::cout << rpt;
                Opm::computeInjectedProduced(*props, state.saturation(), reorder_src, stepsize, injected, produced);
            }
        }
        transport_timer.stop();
        double tt = transport_timer.secsSinceStart();
        std::cout << "Transport solver took: " << tt << " seconds." << std::endl;
        ttime += tt;

        // Report volume balances.
        Opm::computeSaturatedVol(porevol, state.saturation(), satvol);
        tot_injected[0] += injected[0];
        tot_injected[1] += injected[1];
        tot_produced[0] += produced[0];
        tot_produced[1] += produced[1];
        std::cout.precision(5);
        const int width = 18;
        std::cout << "\nVolume balance report (all numbers relative to total pore volume).\n";
        std::cout << "    Saturated volumes:     "
                  << std::setw(width) << satvol[0]/tot_porevol_init
                  << std::setw(width) << satvol[1]/tot_porevol_init << std::endl;
        std::cout << "    Injected volumes:      "
                  << std::setw(width) << injected[0]/tot_porevol_init
                  << std::setw(width) << injected[1]/tot_porevol_init << std::endl;
        std::cout << "    Produced volumes:      "
                  << std::setw(width) << produced[0]/tot_porevol_init
                  << std::setw(width) << produced[1]/tot_porevol_init << std::endl;
        std::cout << "    Total inj volumes:     "
                  << std::setw(width) << tot_injected[0]/tot_porevol_init
                  << std::setw(width) << tot_injected[1]/tot_porevol_init << std::endl;
        std::cout << "    Total prod volumes:    "
                  << std::setw(width) << tot_produced[0]/tot_porevol_init
                  << std::setw(width) << tot_produced[1]/tot_porevol_init << std::endl;
        std::cout << "    In-place + prod - inj: "
                  << std::setw(width) << (satvol[0] + tot_produced[0] - tot_injected[0])/tot_porevol_init
                  << std::setw(width) << (satvol[1] + tot_produced[1] - tot_injected[1])/tot_porevol_init << std::endl;
        std::cout << "    Init - now - pr + inj: "
                  << std::setw(width) << (init_satvol[0] - satvol[0] - tot_produced[0] + tot_injected[0])/tot_porevol_init
                  << std::setw(width) << (init_satvol[1] - satvol[1] - tot_produced[1] + tot_injected[1])/tot_porevol_init
                  << std::endl;
        std::cout.precision(8);

        watercut.push(simtimer.currentTime() + simtimer.currentStepLength(),
                      produced[0]/(produced[0] + produced[1]),
                      tot_produced[0]/tot_porevol_init);
        if (wells->c_wells()) {
            wellreport.push(*props, *wells->c_wells(), state.saturation(),
                            simtimer.currentTime() + simtimer.currentStepLength(),
                            well_state.bhp(), well_state.perfRates());
        }
    }
    total_timer.stop();

    std::cout << "\n\n================    End of simulation     ===============\n"
              << "Total time taken: " << total_timer.secsSinceStart()
              << "\n  Pressure time:  " << ptime
              << "\n  Transport time: " << ttime << std::endl;

    if (output) {
        outputState(*grid->c_grid(), state, simtimer, output_dir);
        outputWaterCut(watercut, output_dir);
        if (wells->c_wells()) {
            outputWellReport(wellreport, output_dir);
        }
    }

    destroy_transport_source(tsrc);
}
