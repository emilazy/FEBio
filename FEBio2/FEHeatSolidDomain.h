#pragma once
#include "FECore/FESolidDomain.h"
#include "FECore/FENLSolver.h"

//-----------------------------------------------------------------------------
//! domain class for 3D heat elements
class FEHeatSolidDomain : public FESolidDomain
{
public:
	FEHeatSolidDomain(FEMesh* pm, FEMaterial* pmat) : FESolidDomain(FE_HEAT_SOLID_DOMAIN, pm, pmat) {}

	FEDomain* Clone()
	{
		FEHeatSolidDomain* pd = new FEHeatSolidDomain(m_pMesh, m_pMat);
		pd->m_Elem = m_Elem; pd->m_pMesh = m_pMesh; pd->m_Node = m_Node;
		return pd;
	}

	//! Unpack solid element data
	void UnpackLM(FEElement& el, vector<int>& lm);

	void HeatStiffnessMatrix(FENLSolver* psolver);

protected:
	//! calculate the conductive element stiffness matrix
	void ConductionStiffness(FESolidElement& el, matrix& ke);

	//! calculate the capacitance element stiffness matrix
	void CapacitanceStiffness(FESolidElement& el, matrix& ke, double dt);
};
