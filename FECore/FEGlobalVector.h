#pragma once
#include <vector>
#include "fecore_export.h"
using namespace std;

class FEModel;

//-----------------------------------------------------------------------------
//! This class represents a global system array. It provides functions to assemble
//! local (element) vectors into this array
class FECOREDLL_EXPORT FEGlobalVector
{
public:
	//! constructor
	FEGlobalVector(FEModel& fem, vector<double>& R, vector<double>& Fr);

	//! destructor
	virtual ~FEGlobalVector();

	//! Assemble the element vector into this global vector
	virtual void Assemble(vector<int>& en, vector<int>& elm, vector<double>& fe, bool bdom = false);

	//! Assemble into this global vector
	virtual void Assemble(vector<int>& lm, vector<double>& fe);
    
	//! access operator
	double& operator [] (int i) { return m_R[i]; }

	//! Get the FE model
	FEModel& GetFEModel() { return m_fem; }

	//! get the size of the vector
	int Size() const { return (int) m_R.size(); }

protected:
	FEModel&			m_fem;	//!< model
	vector<double>&		m_R;	//!< residual
	vector<double>&		m_Fr;	//!< nodal reaction forces \todo I want to remove this
};
