//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//
#include "LameProblem.hpp"
#include "Albany_Utils.hpp"
#include "Albany_ProblemUtils.hpp"

Albany::LameProblem::
LameProblem(const Teuchos::RCP<Teuchos::ParameterList>& params_,
            const Teuchos::RCP<ParamLib>& paramLib_,
            const int numDim_,
            const Teuchos::RCP<const Epetra_Comm>& comm) :
  Albany::AbstractProblem(params_, paramLib_, numDim_),
  haveSource(false), haveMatDB(false)
{
 
  std::string& method = params->get("Name", "Library of Advanced Materials for Engineering (LAME) ");
  *out << "Problem Name = " << method << std::endl;
  
  haveSource =  params->isSublist("Source Functions");

  if(params->isType<string>("MaterialDB Filename")){
        haveMatDB = true;
    mtrlDbFilename = params->get<string>("MaterialDB Filename");
    materialDB = Teuchos::rcp(new QCAD::MaterialDatabase(mtrlDbFilename, comm));
  }

  // currently only support 3D analyses
  TEUCHOS_TEST_FOR_EXCEPTION(neq != 3,
                     Teuchos::Exceptions::InvalidParameter,
                     "\nOnly three-dimensional analyses are suppored when using the Library of Advanced Materials for Engineering (LAME)\n");
}

Albany::LameProblem::
~LameProblem()
{
}

//the following function returns the problem information required for setting the rigid body modes (RBMs) for elasticity problems (in src/Albany_SolverFactory.cpp)
//written by IK, Feb. 2012 
void
Albany::LameProblem::getRBMInfoForML(
   int& numPDEs, int& numElasticityDim, int& numScalar, int& nullSpaceDim)
{
  //number of PDEs and number of elastic equations is the number of spatial dimensions
  numPDEs = numDim;
  numElasticityDim = numDim;
  numScalar = 0;
  if (numDim == 1) {nullSpaceDim = 0; }
  else {
    if (numDim == 2) {nullSpaceDim = 3; }
    if (numDim == 3) {nullSpaceDim = 6; }
  }
}


void
Albany::LameProblem::
buildProblem(
  Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >  meshSpecs,
  Albany::StateManager& stateMgr)
{
  /* Construct All Phalanx Evaluators */
  int physSets = meshSpecs.size();
  cout << "Lame Num MeshSpecs: " << physSets << endl;
  fm.resize(physSets);

  for (int ps=0; ps<physSets; ps++) {
    fm[ps]  = Teuchos::rcp(new PHX::FieldManager<PHAL::AlbanyTraits>);
    buildEvaluators(*fm[ps], *meshSpecs[ps], stateMgr, BUILD_RESID_FM, 
		    Teuchos::null);
  }
  constructDirichletEvaluators(*meshSpecs[0]);
}

Teuchos::Array< Teuchos::RCP<const PHX::FieldTag> >
Albany::LameProblem::
buildEvaluators(
  PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
  const Albany::MeshSpecsStruct& meshSpecs,
  Albany::StateManager& stateMgr,
  Albany::FieldManagerChoice fmchoice,
  const Teuchos::RCP<Teuchos::ParameterList>& responseList)
{
  // Call constructeEvaluators<EvalT>(*rfm[0], meshSpecs, stateMgr);
  // for each EvalT in PHAL::AlbanyTraits::BEvalTypes
  ConstructEvaluatorsOp<LameProblem> op(
    *this, fm0, meshSpecs, stateMgr, fmchoice, responseList);
  boost::mpl::for_each<PHAL::AlbanyTraits::BEvalTypes>(op);
  return *op.tags;
}

void
Albany::LameProblem::constructDirichletEvaluators(
        const Albany::MeshSpecsStruct& meshSpecs)
{
   // Construct Dirichlet evaluators for all nodesets and names
   std::vector<std::string> dirichletNames(neq);
   dirichletNames[0] = "X";
   if (neq>1) dirichletNames[1] = "Y";
   if (neq>2) dirichletNames[2] = "Z";
   Albany::BCUtils<Albany::DirichletTraits> dirUtils;
   dfm = dirUtils.constructBCEvaluators(meshSpecs.nsNames, dirichletNames,
                                          this->params, this->paramLib);
}

Teuchos::RCP<const Teuchos::ParameterList>
Albany::LameProblem::getValidProblemParameters() const
{
  Teuchos::RCP<Teuchos::ParameterList> validPL =
    this->getGenericProblemParams("ValidLameProblemParams");

  validPL->set<string>("Lame Material Model", "", "The name of the LAME material model.");
  validPL->sublist("Lame Material Parameters", false, "");
  validPL->sublist("aveJ", false, "If true, the determinate of the deformation gradient for each integration point is replaced with the average value over all integration points in the element (produces constant volumetric response).");
  validPL->sublist("volaveJ", false, "If true, the determinate of the deformation gradient for each integration point is replaced with the volume-averaged value over all integration points in the element (produces constant volumetric response).");
  validPL->set<string>("MaterialDB Filename","materials.xml","Filename of material database xml file");

  return validPL;
}

