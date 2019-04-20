/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2019 University of Utah, Columbia University, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/

#include "stdafx.h"
#include "FENewtonSolver.h"
#include "FEModel.h"
#include "FEGlobalMatrix.h"
#include "LinearSolver.h"
#include "FELinearConstraintManager.h"
#include "FEAnalysis.h"
#include "FEPrescribedDOF.h"
#include "log.h"
#include "sys.h"
#include "FEDomain.h"
#include "DumpStream.h"

//-----------------------------------------------------------------------------
// define the parameter list
BEGIN_FECORE_CLASS(FENewtonSolver, FESolver)
	ADD_PARAMETER(m_lineSearch->m_LStol , FE_RANGE_GREATER_OR_EQUAL(0.0), "lstol"   );
	ADD_PARAMETER(m_lineSearch->m_LSmin , FE_RANGE_GREATER_OR_EQUAL(0.0), "lsmin"   );
	ADD_PARAMETER(m_lineSearch->m_LSiter, FE_RANGE_GREATER_OR_EQUAL(0), "lsiter"  );
	ADD_PARAMETER(m_maxref              , FE_RANGE_GREATER_OR_EQUAL(0.0), "max_refs");
	ADD_PARAMETER(m_bzero_diagonal      , "check_zero_diagonal");
	ADD_PARAMETER(m_zero_tol            , "zero_diagonal_tol"  );
	ADD_PARAMETER(m_force_partition     , "force_partition");
	ADD_PARAMETER(m_breformtimestep     , "reform_each_time_step");
	ADD_PARAMETER(m_breformAugment      , "reform_augment");
	ADD_PARAMETER(m_bdivreform          , "diverge_reform");
	ADD_PARAMETER(m_bdoreforms          , "do_reforms"  );

	// obsolete parameters (Should be set via the qn_method)
	ADD_PARAMETER(m_qndefault           , "qnmethod", 0, "BFGS\0BROYDEN\0JFNK\0");
	ADD_PARAMETER(m_maxups              , FE_RANGE_GREATER_OR_EQUAL(0.0), "max_ups" );
	ADD_PARAMETER(m_max_buf_size        , FE_RANGE_GREATER_OR_EQUAL(0), "qn_max_buffer_size");
	ADD_PARAMETER(m_cycle_buffer        , "qn_cycle_buffer");
	ADD_PARAMETER(m_cmax                , FE_RANGE_GREATER_OR_EQUAL(0.0), "cmax"    );

	ADD_PROPERTY(m_qnstrategy, "qn_method", FEProperty::Optional);
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
FENewtonSolver::FENewtonSolver(FEModel* pfem) : FESolver(pfem)
{
	m_lineSearch = new FELineSearch(this);
	m_ls = 0.0;

	// default parameters
	m_maxref = 15;

	m_nref = 0;

    m_neq = 0;
    m_plinsolve = 0;
	m_pK = 0;

	m_cmax   = 1e5;
	m_maxups = 10;
	m_max_buf_size = 0;
	m_cycle_buffer = true;

	m_qndefault = QN_BFGS;
	m_qnstrategy = nullptr;

	m_bforceReform = true;
	m_bdivreform = true;
	m_bdoreforms = true;

	m_bzero_diagonal = true;
	m_zero_tol = 0.0;

	m_force_partition = 0;
	m_breformtimestep = true;
	m_breformAugment = false;
}

//-----------------------------------------------------------------------------
//! Set the default solution strategy
void FENewtonSolver::SetDefaultStrategy(QN_STRATEGY qn)
{
	m_qndefault = qn;
}

//-----------------------------------------------------------------------------
void FENewtonSolver::SetSolutionStrategy(FENewtonStrategy* pstrategy)
{
	if (m_qnstrategy) delete m_qnstrategy;
	m_qnstrategy = pstrategy;
	m_qnstrategy->SetNewtonSolver(this);
}

//-----------------------------------------------------------------------------
FENewtonSolver::~FENewtonSolver()
{
	Clean();
}

//-----------------------------------------------------------------------------
// return line search
FELineSearch* FENewtonSolver::GetLineSearch()
{
	return m_lineSearch;
}

//-----------------------------------------------------------------------------
FEGlobalMatrix* FENewtonSolver::GetStiffnessMatrix()
{
	return m_pK;
}

//-----------------------------------------------------------------------------
//! Check the zero diagonal
void FENewtonSolver::CheckZeroDiagonal(bool bcheck, double ztol)
{
	m_bzero_diagonal = bcheck;
	m_zero_tol = fabs(ztol);
}

//-----------------------------------------------------------------------------
void FENewtonSolver::AssembleStiffness(vector<int>& en, vector<int>& lmi, vector<int>& lmj, matrix& ke)
{
	if (lmi.size() == 0) return;

	// assemble into the global stiffness
	m_pK->Assemble(ke, lmi, lmj);

	// adjust for linear constraints
	FEModel& fem = *GetFEModel();
	FELinearConstraintManager& LCM = fem.GetLinearConstraintManager();
	if (LCM.LinearConstraints() > 0)
	{
		LCM.AssembleStiffness(*m_pK, m_Fd, m_ui, en, lmi, lmj, ke);
	}

	// if there are prescribed bc's we need to adjust the residual
	SparseMatrix& K = *m_pK;
	int cols = ke.columns();
	int rows = ke.rows();
	for (int j = 0; j<cols; ++j)
	{
		int J = -lmj[j] - 2;
		if ((J >= 0) && (J<m_neq))
		{
			// dof j is a prescribed degree of freedom

			// loop over rows
			for (int i = 0; i<rows; ++i)
			{
				int I = lmi[i];
				if (I >= 0)
				{
					// dof i is not a prescribed degree of freedom
					m_Fd[I] -= ke[i][j] * m_ui[J];
				}
			}
			// set the diagonal element of K to 1
			K.set(J, J, 1);
		}
	}
}

//-----------------------------------------------------------------------------
//! Reforms a stiffness matrix and factorizes it
bool FENewtonSolver::ReformStiffness()
{
	feLog("Reforming stiffness matrix: reformation #%d\n\n", m_nref + 1);

    // first, let's make sure we have not reached the max nr of reformations allowed
    if (m_nref >= m_maxref) throw MaxStiffnessReformations();

	FEModel& fem = *GetFEModel();

    // recalculate the shape of the stiffness matrix if necessary
    if (m_breshape)
    {
        // reshape the stiffness matrix
        if (!CreateStiffness(m_niter == 0)) return false;
        
        // reset reshape flag, except for contact
		m_breshape = (((fem.SurfacePairConstraints() > 0) || (fem.NonlinearConstraints() > 0)) ? true : false);
    }
    
    // calculate the global stiffness matrix
	bool bret = false;
	{
		TRACK_TIME(TimerID::Timer_Stiffness);

		// zero the stiffness matrix
		m_pK->Zero();

		// Zero the rhs adjustment vector
		zero(m_Fd);

		// calculate the global stiffness matrix
	    bret = StiffnessMatrix();

		// check for zero diagonals
		if (m_bzero_diagonal)
		{
			// get the stiffness matrix
			SparseMatrix& K = *m_pK;
			vector<int> zd;
			int neq = K.Rows();
			for (int i=0; i<neq; ++i)
			{
				double di = fabs(K.diag(i));
				if (di <= m_zero_tol)
				{
					zd.push_back(i);
				}
			}

			if (zd.empty() == false) throw ZeroDiagonal(-1, -1);
		}
	}

	// if the stiffness matrix was evaluated successfully,
	// we factor it.
    if (bret)
    {
        {
			TRACK_TIME(TimerID::Timer_Solve);
			// factorize the stiffness matrix
            if (m_plinsolve->Factor() == false) return false;
        }

        // increase total nr of reformations
        m_nref++;
        m_ntotref++;
        
        // reset bfgs update counter
		m_qnstrategy->m_nups = 0;
    }
    
    return bret;
}

//-----------------------------------------------------------------------------
//! get the RHS
std::vector<double> FENewtonSolver::GetLoadVector()
{
	return m_R0;
}

//-----------------------------------------------------------------------------
//!  Creates the global stiffness matrix
//! \todo Can we move this to the FEGlobalMatrix::Create function?
bool FENewtonSolver::CreateStiffness(bool breset)
{
	{
		TRACK_TIME(TimerID::Timer_Reform);
		// clean up the solver
		if (m_pK->NonZeroes()) m_plinsolve->Destroy();

		// clean up the stiffness matrix
		m_pK->Clear();

		// create the stiffness matrix
		feLog("===== reforming stiffness matrix:\n");
		if (m_pK->Create(GetFEModel(), m_neq, breset) == false)
		{
			feLogError("An error occured while building the stiffness matrix\n\n");
			return false;
		}
		else
		{
			// output some information about the direct linear solver
			int neq = m_pK->Rows();
			int nnz = m_pK->NonZeroes();
			feLog("\tNr of equations ........................... : %d\n", neq);
			feLog("\tNr of nonzeroes in stiffness matrix ....... : %d\n", nnz);
		}
	}

	// Do the preprocessing of the solver
	{
		TRACK_TIME(TimerID::Timer_Solve);
		if (!m_plinsolve->PreProcess())
		{
			// TODO: get rid of throwing this exception. We should just return false.
			throw FatalError();
		}
	}

	// done!
	return true;
}

//-----------------------------------------------------------------------------
//! return the linear solver
LinearSolver* FENewtonSolver::GetLinearSolver()
{
	return m_plinsolve;
}

//-----------------------------------------------------------------------------
bool FENewtonSolver::AllocateLinearSystem()
{
	// Now that we have determined the equation numbers we can continue
	// with creating the stiffness matrix. First we select the linear solver
	// The stiffness matrix is created in CreateStiffness
	// Note that if a particular solver was requested in the input file
	// then the solver might already be allocated. That's way we need to check it.
	if (m_plinsolve == 0)
	{
		FEModel* fem = GetFEModel();
		FECoreKernel& fecore = FECoreKernel::GetInstance();
		m_plinsolve = fecore.CreateLinearSolver(fem);
		if (m_plinsolve == 0)
		{
			feLogError("Unknown solver type selected\n");
			return false;
		}

		if (m_part.empty() == false)
		{
			m_plinsolve->SetPartitions(m_part);
		}
	}

	Matrix_Type mtype = MatrixType();
	SparseMatrix* pS = m_qnstrategy->CreateSparseMatrix(mtype);
	if ((pS == 0) && (m_msymm == REAL_SYMMETRIC))
	{
		// oh, oh, something went wrong. It's probably because the user requested a symmetric matrix for a 
		// solver that wants a non-symmetric. If so, let's force a non-symmetric format.
		pS = m_qnstrategy->CreateSparseMatrix(REAL_UNSYMMETRIC);

		if (pS)
		{
			// Problem solved! Let's inform the user.
			m_msymm = REAL_UNSYMMETRIC;
			feLogWarning("The matrix format was changed to non-symmetric since the selected linear solver does not support a symmetric format.");
		}
	}

	// if the sparse matrix is still zero, we have a problem
	if (pS == 0)
	{
		feLogError("The selected linear solver does not support the requested matrix format.\nPlease select a different linear solver.");
		return false;
	}

	// clean up the stiffness matrix if we have one
	if (m_pK) delete m_pK; m_pK = 0;

	// Create the stiffness matrix.
	// Note that this does not construct the stiffness matrix. This
	// is done later in the CreateStiffness routine.
	m_pK = new FEGlobalMatrix(pS);
	if (m_pK == 0)
	{
		feLogError("Failed allocating stiffness matrix.");
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
bool FENewtonSolver::Init()
{
	// choose a solution strategy
	if (m_qnstrategy == nullptr)
	{
		switch (m_qndefault)
		{
		case QN_BFGS   : SetSolutionStrategy(fecore_new<FENewtonStrategy>("BFGS"   , GetFEModel())); break;
		case QN_BROYDEN: SetSolutionStrategy(fecore_new<FENewtonStrategy>("Broyden", GetFEModel())); break;
		case QN_JFNK   : SetSolutionStrategy(fecore_new<FENewtonStrategy>("JFNK"   , GetFEModel())); break;
		default:
			return false;
		}

		// copy some solution parameters
		m_qnstrategy->m_maxups = m_maxups;
		m_qnstrategy->m_max_buf_size = m_max_buf_size;
		m_qnstrategy->m_cycle_buffer = m_cycle_buffer;
		m_qnstrategy->m_cmax = m_cmax;
	}
	else
	{
		// make sure the QN strategy knows what solver it belongs to
		m_qnstrategy->SetNewtonSolver(this);
	}

	// allocate data vectors
	m_R0.assign(m_neq, 0);
	m_R1.assign(m_neq, 0);
	m_ui.assign(m_neq, 0);
	m_Fd.assign(m_neq, 0);

	// allocate storage for the sparse matrix that will hold the stiffness matrix data
	// we let the linear solver allocate the correct type of matrix format
	if (AllocateLinearSystem() == false) return false;

	// Base class initialization and validation
	if (FESolver::Init() == false) return false;

	// set the create stiffness matrix flag
	m_breshape = true;

	// Set the partitioning of the global matrix
	// This is only used for debugging block solvers for problems that
	// usually don't generate a block structure
	if (m_force_partition > 0) m_plinsolve->SetPartitions(m_force_partition, m_neq - m_force_partition);

	return true;
}

//-----------------------------------------------------------------------------
//! Clean
void FENewtonSolver::Clean()
{
	if (m_plinsolve) delete m_plinsolve; m_plinsolve = nullptr;
	if (m_pK) delete m_pK; m_pK = nullptr;
	if (m_qnstrategy) delete m_qnstrategy; m_qnstrategy = nullptr;
}

//-----------------------------------------------------------------------------
void FENewtonSolver::Serialize(DumpStream& ar)
{
	FESolver::Serialize(ar);
	if (m_lineSearch) m_lineSearch->Serialize(ar);

	if (ar.IsShallow()) return;

	if (ar.IsSaving())
	{
		ar << m_neq;
		ar << m_maxref;
		ar << m_qndefault;
		ar << m_qnstrategy;
	}
	else
	{
		ar >> m_neq;
		ar >> m_maxref;
		ar >> m_qndefault;
		ar >> m_qnstrategy;

		// realloc data
		if (m_neq > 0)
		{
			m_R0.assign(m_neq, 0);
			m_R1.assign(m_neq, 0);
			m_ui.assign(m_neq, 0);
			m_Fd.assign(m_neq, 0);

			// reinitialize the linear system
			if (AllocateLinearSystem() == false) throw DumpStream::ReadError();
		}
	}
}

//-----------------------------------------------------------------------------
//!  This function mainly calls the Quasin routine 
//!  and deals with exceptions that require the immediate termination of
//!	quasi-Newton iterations.
bool FENewtonSolver::SolveStep()
{
	bool bret;

	// initialize counters
	m_niter = 0;	// nr of iterations
	m_nrhs = 0;		// nr of RHS evaluations
	m_nref = 0;		// nr of stiffness reformations
	m_ntotref = 0;
	m_naug = 0;		// nr of augmentations

	try
	{
		// let's try to call Quasin
		bret = Quasin();
	}
	catch (NegativeJacobian e)
	{
		// A negative jacobian was detected
		feLogError("Negative jacobian was detected at element %d at gauss point %d\njacobian = %lg\n", e.m_iel, e.m_ng+1, e.m_vol);
		return false;
	}
	catch (MaxStiffnessReformations)
	{
		// max nr of reformations is reached
		feLogError("Max nr of reformations reached.");
		return false;
	}
	catch (ForceConversion)
	{
		// user forced conversion of problem
		feLogWarning("User forced conversion.\nSolution might not be stable.");
		return true;
	}
	catch (IterationFailure)
	{
		// user caused a forced iteration failure
		feLogWarning("User forced iteration failure.");
		return false;
	}
	catch (MaxResidualError)
	{
		// user caused a forced iteration failure
		feLogWarning("Maximum residual exceeded.");
		return false;
	}
	catch (ZeroLinestepSize)
	{
		// a zero line step size was detected
		feLogError("Zero line step size.");
		return false;
	}
	catch (EnergyDiverging)
	{
		// problem was diverging after stiffness reformation
		feLogError("ERROR", "Problem diverging uncontrollably.");
		return false;
	}
	catch (FEMultiScaleException e)
	{
		// the RVE problem didn't solve
		// logging was turned off during multi-scale runs
		// so we need to turn it back on
		GetFEModel()->UnBlockLog();
		feLogError("The RVE problem has failed at element %d, gauss point %d.\nAborting macro run.", e.elemId, e.gptIndex+1);

		return false;
	}
	catch (DoRunningRestart)
	{
		// a request to fail the iteration and restart the time step
		return false;
	}

	if (bret)
	{
		// print a convergence summary to the felog file
		feLog("\nconvergence summary\n");
		feLog("    number of iterations   : %d\n", m_niter);
		feLog("    number of reformations : %d\n", m_nref);
	}

	return bret;
}

//-----------------------------------------------------------------------------
bool FENewtonSolver::Quasin()
{
	// initialize counters
	m_niter = 0;		// nr of iterations
	m_nrhs = 0;			// nr of RHS evaluations
	m_nref = 0;			// nr of stiffness reformations
	m_ntotref = 0;
	m_qnstrategy->m_nups = 0;	// nr of stiffness updates between reformations

	FEModel& fem = *GetFEModel();
	FETimeInfo& tp = fem.GetTime();

	// Do the pre-solve domain update
	FEMesh& mesh = fem.GetMesh();
	for (int i = 0; i<mesh.Domains(); ++i) mesh.Domain(i).PreSolveUpdate(tp);

	// set-up the boundary conditions
	zero(m_ui);
	int nbc = fem.BoundaryConditions();
	for (int i = 0; i<nbc; ++i)
	{
		FEBoundaryCondition& bc = *fem.BoundaryCondition(i);
		if (bc.IsActive()) bc.PrepStep(m_ui);
	}

	// Initialize QN method
	QNInit();

	// Start the quasi-Newton loop
	bool bconv = false;
	do
	{
		feLog(" %d\n", m_niter + 1);

		// solve the equations (returns line search; solution stored in m_ui)
		double ls = QNSolve();

		feLog(" Nonlinear solution status: time= %lg\n", tp.currentTime);
		feLog("\tstiffness updates             = %d\n", m_qnstrategy->m_nups);
		feLog("\tright hand side evaluations   = %d\n", m_nrhs);
		feLog("\tstiffness matrix reformations = %d\n", m_nref);

		// check convergence
		bconv = CheckConvergence(m_niter, m_ui, ls);

		// if we did not converge, do QN update
		if (bconv == false)
		{
			// do the QN update (this may also do a stiffness reformation if necessary)
			bool bret = QNUpdate();

			// Oh, oh, something went wrong
			if (bret == false) break;
		}

		// increase iteration number
		m_niter++;

		// do minor iterations callbacks
		fem.DoCallback(CB_MINOR_ITERS);
	}
	while (!bconv);

	return bconv;
}

//-----------------------------------------------------------------------------
//! Solve the linear system of equations.
//! x is the solution vector
//! R is the right-hand-side vector
void FENewtonSolver::SolveLinearSystem(vector<double>& x, vector<double>& R)
{
	// solve the equations
	if (m_plinsolve->BackSolve(x, R) == false)
		throw LinearSolverFailed();
}

//-----------------------------------------------------------------------------
//! rewind solver
//! This is called when the time step failed.
void FENewtonSolver::Rewind()
{
	// reset the forceReform flag so that we reform the stiffness matrix
	m_bforceReform = true;
}

//-----------------------------------------------------------------------------
//! call this at the start of the quasi-newton loop (after PrepStep)
bool FENewtonSolver::QNInit()
{
	// see if we reform at the start of every time step
	bool breform = m_breformtimestep;

	// if the force reform flag was set, we force a reform
	// (This will be the case for the first time this is called, or when the previous time step failed)
	if (m_bforceReform)
	{
		breform = true;

		m_bforceReform = false;
	}

	m_qnstrategy->PreSolveUpdate();

	// do the reform
	if (breform)
	{
		// do the first stiffness formation
		if (m_qnstrategy->ReformStiffness() == false) return false;
	}

	// calculate initial residual
	{
		TRACK_TIME(TimerID::Timer_Residual);
		if (m_qnstrategy->Residual(m_R0, true) == false) return false;
	}

	// add the contribution from prescribed dofs
	m_R0 += m_Fd;

	// TODO: I can check here if the residual is zero.
	// If it is than there is probably no force acting on the system
	// if (m_R0*m_R0 < eps) bconv = true;

	//	double r0 = m_R0*m_R0;

	// do callback (we do it here since we want the RHS to be formed as well)
	GetFEModel()->DoCallback(CB_MATRIX_REFORM);

	return true;
}

//-----------------------------------------------------------------------------
double FENewtonSolver::QNSolve()
{
	{ // call the strategy to solve the linear equations
		TRACK_TIME(TimerID::Timer_Solve);
		m_qnstrategy->SolveEquations(m_ui, m_R0);

		// check for nans
		double du = m_ui*m_ui;
		if (ISNAN(du)) throw NANDetected();
	}

	// perform a linesearch
	// the geometry is also updated in the line search
	m_ls = 1.0;
	if (m_lineSearch && (m_lineSearch->m_LStol > 0.0)) m_ls = m_lineSearch->DoLineSearch();
	else
	{
		// Update geometry
		{
			TRACK_TIME(TimerID::Timer_Update);
			Update(m_ui);
		}

		// calculate residual at this point
		{
			TRACK_TIME(TimerID::Timer_Residual);
			m_qnstrategy->Residual(m_R1, false);
		}
	}

	// return line search
	return m_ls;
}

//-----------------------------------------------------------------------------
void FENewtonSolver::QNForceReform(bool b)
{
	m_bforceReform = b;
}

//-----------------------------------------------------------------------------
//! Do a QN update
bool FENewtonSolver::QNUpdate()
{
	// see if the force reform flag was set
	bool breform = m_bforceReform; m_bforceReform = false;

	// for full-Newton, we skip QN update
	if (m_maxups == 0) breform = true;

	// if not, do a QN update
	if (breform == false)
	{
		TRACK_TIME(TimerID::Timer_QNUpdate);

		// make sure we didn't reach max updates
		if (m_qnstrategy->m_nups >= m_qnstrategy->m_maxups - 1)
		{
			// print a warning only if the user did not intent full-Newton
			if (m_qnstrategy->m_maxups > 0)
				feLogWarning("Max nr of iterations reached.\nStiffness matrix will now be reformed.");
			breform = true;
		}

		// try to do an update
		bool bret = m_qnstrategy->Update(m_ls, m_ui, m_R0, m_R1);
		if (bret == false)
		{
			// Stiffness update has failed.
			// this might be due a too large condition number
			// or the update was no longer positive definite.
			feLogWarning("The QN update has failed.\nStiffness matrix will now be reformed.");
			breform = true;
		}
	}

	// zero displacement increments
	// we must set this to zero before the reformation
	// because we assume that the prescribed displacements are stored 
	// in the m_ui vector.
	zero(m_ui);

	// reform stiffness matrices if necessary
	if (breform && m_bdoreforms)
	{
		// reform the matrix
		if (m_qnstrategy->ReformStiffness() == false) return false;
	}

	// copy last calculated residual
	m_R0 = m_R1;

	return true;
}

//-----------------------------------------------------------------------------
bool FENewtonSolver::DoAugmentations()
{
	FEModel& fem = *GetFEModel();
	FEAnalysis* pstep = fem.GetCurrentStep();

	// we have converged, so let's see if the augmentations have converged as well
	feLog("\n........................ augmentation # %d\n", m_naug + 1);

	// do callback
	fem.DoCallback(CB_AUGMENT);

	// do the augmentations
	bool bconv = Augment();

	// update counter
	++m_naug;

	// we reset the reformations counter
	m_nref = 0;

	// If we havn't converged we prepare for the next iteration
	if (!bconv)
	{
		// Since the Lagrange multipliers have changed, we can't just copy
		// the last residual but have to recalculate the residual
		// we also recalculate the stresses in case we are doing augmentations
		// for incompressible materials
		fem.Update();
		{
			TRACK_TIME(TimerID::Timer_Residual);
			Residual(m_R0);
		}

		m_qnstrategy->PreSolveUpdate();

		// reform the matrix if we are using full-Newton or
		// force reform after augmentations
		if ((m_qnstrategy->m_maxups == 0) || (m_breformAugment))
		{
			// TODO: Note sure how to handle a false return from ReformStiffness. 
			//       I think this is pretty rare so I'm ignoring it for now.
//			if (ReformStiffness() == false) break;
			m_qnstrategy->ReformStiffness();
		}
	}

	return bconv;
}
