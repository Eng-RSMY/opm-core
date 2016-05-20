#ifndef OPM_PRODUCTIONSPECIFICATION_HPP
#define	OPM_PRODUCTIONSPECIFICATION_HPP

#include <opm/core/wells.h>
#include <string>

namespace Opm
{

    struct ProductionSpecification
    {

        enum ControlMode
        {
            NONE = 0, ORAT = 1, WRAT=2, GRAT=3, LRAT=4, CRAT=5, RESV=6, PRBL=7, BHP=8, THP=9, GRUP=10, FLD=11
        };

        enum Procedure
        {
            NONE_P, RATE, WELL
        };

        enum GuideRateType
        {
            OIL, GAS, WATER, NONE_GRT
        };

        ProductionSpecification();
        const std::string ControlMode2String(const ControlMode& mode);
        const std::string Procedure2String(const Procedure& type);
        const std::string GuideRateType2String(const GuideRateType& type);

        ControlMode control_mode_;
        Procedure procedure_;

        double oil_max_rate_;
        double water_max_rate_;
        double gas_max_rate_;
        double liquid_max_rate_;
        double reservoir_flow_max_rate_;
        double BHP_limit_;
        double guide_rate_;
        GuideRateType guide_rate_type_;

    };
}

#endif	/* OPM_PRODUCTIONSPECIFICATION_HPP */

