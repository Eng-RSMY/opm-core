#ifndef OPM_WELLSGROUP_HPP
#define	OPM_WELLSGROUP_HPP

#include <opm/core/InjectionSpecification.hpp>
#include <opm/core/ProductionSpecification.hpp>
#include <opm/core/eclipse/EclipseGridParser.hpp>
#include <string>


namespace Opm
{

    class WellsGroupInterface
    {
    public:
        WellsGroupInterface(const std::string& name, ProductionSpecification prod_spec,
                InjectionSpecification inj_spec);
        virtual ~WellsGroupInterface();

        const std::string& name();
        const ProductionSpecification& prodSpec() const;
        const InjectionSpecification& injSpec() const;

        /// \returns the pointer to the WellsGroupInterface with the given name. NULL if 
        ///          the name is not found.a
        virtual WellsGroupInterface* findGroup(std::string name_of_node) = 0;
    private:
        std::string name_;
        ProductionSpecification production_specification_;
        InjectionSpecification injection_specification_;
    };

    class WellsGroup : public WellsGroupInterface
    {
    public:
        WellsGroup(const std::string& name, ProductionSpecification prod_spec,
                InjectionSpecification inj_spec);

        virtual WellsGroupInterface* findGroup(std::string name_of_node);

        void addChild(std::tr1::shared_ptr<WellsGroupInterface> child);
    private:
        std::vector<std::tr1::shared_ptr<WellsGroupInterface> > children_;

    };

    class WellNode : public WellsGroupInterface
    {
    public:
        WellNode(const std::string& name, ProductionSpecification prod_spec,
                InjectionSpecification inj_spec);

        virtual WellsGroupInterface* findGroup(std::string name_of_node);

    };

    
    std::tr1::shared_ptr<WellsGroupInterface> createWellsGroup(std::string name, 
                                const EclipseGridParser& deck);


}
#endif	/* OPM_WELLSGROUP_HPP */

