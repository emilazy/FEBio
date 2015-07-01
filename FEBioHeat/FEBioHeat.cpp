#include "FEBioHeat.h"
#include "FEIsotropicFourier.h"
#include "FEPlotNodeTemperature.h"
#include "FEPlotHeatFlux.h"
#include "FEHeatTransferAnalysis.h"
#include "FEHeatFlux.h"
#include "FEConvectiveHeatFlux.h"
#include "FEHeatSolver.h"
#include "FEHeatSolidDomain.h"
#include "FEHeatDomainFactory.h"
#include "FEHeatSource.h"
#include "FEBioHeatData.h"
#include "FEThermoElasticAnalysis.h"
#include "FEThermoElasticSolver.h"
#include "FEThermoElasticMaterial.h"
#include "FEThermoNeoHookean.h"
#include "FEThermalConductivity.h"

namespace FEBioHeat {

//============================================================================
void InitModule()
{
	// Domain factory
	FECoreKernel& febio = FECoreKernel::GetInstance();
	febio.RegisterDomain(new FEHeatDomainFactory);

	// Analysis
	REGISTER_FECORE_CLASS(FEHeatTransferAnalysis, FEANALYSIS_ID, "heat transfer");
	REGISTER_FECORE_CLASS(FEThermoElasticAnalysis, FEANALYSIS_ID, "thermo-elastic");

	// Solvers
	REGISTER_FECORE_CLASS(FEHeatSolver         , FESOLVER_ID, "heat transfer" );
	REGISTER_FECORE_CLASS(FEThermoElasticSolver, FESOLVER_ID, "thermo-elastic");

	// Materials
	REGISTER_FECORE_CLASS(FEIsotropicFourier     , FEMATERIAL_ID, "isotropic Fourier");
	REGISTER_FECORE_CLASS(FEThermoElasticMaterial, FEMATERIAL_ID, "thermo-elastic"   );
	REGISTER_FECORE_CLASS(FEThermoNeoHookean     , FEMATERIAL_ID, "thermo-neo-Hookean");
	REGISTER_FECORE_CLASS(FEConstReferenceThermalConductivity, FEMATERIAL_ID, "cond-ref-iso");
	REGISTER_FECORE_CLASS(FEConstThermalConductivity         , FEMATERIAL_ID, "cond-const"  );

	// Body loads
	REGISTER_FECORE_CLASS(FEHeatSource, FEBODYLOAD_ID, "heat_source");

	// Surface loads
	REGISTER_FECORE_CLASS(FEHeatFlux          , FESURFACELOAD_ID, "heatflux"           );
	REGISTER_FECORE_CLASS(FEConvectiveHeatFlux, FESURFACELOAD_ID, "convective_heatflux");

	// Plot data fields
	REGISTER_FECORE_CLASS(FEPlotNodeTemperature	, FEPLOTDATA_ID, "temperature"	);
	REGISTER_FECORE_CLASS(FEPlotHeatFlux		, FEPLOTDATA_ID, "heat flux"	);

	// log data fields
	REGISTER_FECORE_CLASS(FENodeTemp, FENODELOGDATA_ID, "T");
}

}
