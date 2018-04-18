#ifndef FELIX_HYDROLOGY_WATER_SOURCE_HPP
#define FELIX_HYDROLOGY_WATER_SOURCE_HPP

#include "Phalanx_config.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Albany_Layouts.hpp"

namespace FELIX
{

/** \brief Hydrology water source

    This evaluator computes the subglacial hydrology water source
    from measurements of the Surface Mass Balance
*/

template<typename EvalT, typename Traits, bool OnSide>
class HydrologyWaterSource: public PHX::EvaluatorWithBaseImpl<Traits>,
                            public PHX::EvaluatorDerived<EvalT, Traits>
{
public:

  typedef typename EvalT::ParamScalarT ParamScalarT;

  HydrologyWaterSource (const Teuchos::ParameterList& p,
                        const Teuchos::RCP<Albany::Layouts>& dl);

  void postRegistrationSetup (typename Traits::SetupData d,
                              PHX::FieldManager<Traits>& fm);

  void evaluateFields(typename Traits::EvalData d);

private:

  void evaluateFieldsCell(typename Traits::EvalData d);
  void evaluateFieldsSide(typename Traits::EvalData d);

  // Input:
  PHX::MDField<const ParamScalarT>  smb;

  // Output:
  PHX::MDField<ParamScalarT>        omega;

  int numNodes;

  std::string sideSetName;  // Needed only if OnSide=true
};

} // Namespace FELIX

#endif // FELIX_HYDROLOGY_WATER_SOURCE_HPP
