#ifndef OPM_WELLSGROUP_HPP
#define	OPM_WELLSGROUP_HPP

#include <opm/core/InjectionSpecification.hpp>
#include <opm/core/ProductionSpecification.hpp>
#include <opm/core/eclipse/EclipseGridParser.hpp>
#include <opm/core/grid.h>
#include <string>


namespace Opm
{

    class WellsGroupInterface
    {
    public:
        WellsGroupInterface(const std::string& name,
                            ProductionSpecification prod_spec,
                            InjectionSpecification inj_spec);
        virtual ~WellsGroupInterface();

        /// The unique identifier for the well or well group.
        const std::string& name();
        
        /// Production specifications for the well or well group.
        const ProductionSpecification& prodSpec() const;
        
        /// Injection specifications for the well or well group.
        const InjectionSpecification& injSpec() const;
        
        /// \returns true if the object is a leaf node (WellNode), false otherwise.
        virtual bool isLeafNode() const;
        
        /// \returns the pointer to the WellsGroupInterface with the given name. NULL if 
        ///          the name is not found.a
        virtual WellsGroupInterface* findGroup(std::string name_of_node) = 0;
        
        void setParent(WellsGroupInterface* parent);
    protected:
           WellsGroupInterface* parent_;

    private:
        std::string name_;
        ProductionSpecification production_specification_;
        InjectionSpecification injection_specification_;
    };



    class WellsGroup : public WellsGroupInterface
    {
    public:
        WellsGroup(const std::string& name,
                   ProductionSpecification prod_spec,
                   InjectionSpecification inj_spec);

        virtual WellsGroupInterface* findGroup(std::string name_of_node);

        void addChild(std::tr1::shared_ptr<WellsGroupInterface> child);
        
        bool conditionsMet(const std::vector<double>& well_bhp, const std::vector<double>& well_rate, const struct Wells* wells,
                                   int index_of_well);
    private:
        std::vector<std::tr1::shared_ptr<WellsGroupInterface> > children_;
    };



    class WellNode : public WellsGroupInterface
    {
    public:
        WellNode(const std::string& name,
                 ProductionSpecification prod_spec,
                InjectionSpecification inj_spec);

        virtual WellsGroupInterface* findGroup(std::string name_of_node);
        virtual bool conditionsMet(const std::vector<double>& well_bhp, const std::vector<double>& well_rate);
        virtual bool isLeafNode() const;
        
        void setWellsPointer(const struct Wells* wells, int self_index);
        
    private:
        const struct Wells* wells_;
        int self_index_;
    };

    /// Doc me!
    std::tr1::shared_ptr<WellsGroupInterface> createWellsGroup(std::string name, 
                                                               const EclipseGridParser& deck);


}
#endif	/* OPM_WELLSGROUP_HPP */

