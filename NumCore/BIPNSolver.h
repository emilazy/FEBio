#pragma once
#include <FECore/LinearSolver.h>
#include <FECore/SparseMatrix.h>
#include "CompactMatrix.h"

//-----------------------------------------------------------------------------
// This class implements the bi-partitioned iterative solver, by:
// Esmaily-Moghadam, Bazilevs, Marsden, Comput. Methods Appl. Mech. Engrg. 286(2015) 40-62
//
class BIPNSolver : public LinearSolver
{
public:
	// constructor
	BIPNSolver();

	// set the output level
	void SetPrintLevel(int n);

	// set the max nr of BIPN iterations
	void SetMaxIterations(int n);

	// Set the BIPN convergence tolerance
	void SetTolerance(double eps);

	// Set split row/column
	void SetSplit(int n);

public:
	// allocate storage
	bool PreProcess();

	//! Factor the matrix (for iterative solvers, this can be used for creating pre-conditioner)
	bool Factor();

	//! Calculate the solution of RHS b and store solution in x
	bool BackSolve(vector<double>& x, vector<double>& b);

	//! Return a sparse matrix compatible with this solver
	SparseMatrix* CreateSparseMatrix(Matrix_Type ntype);

private:
	bool cgsolve(vector<double>& x, vector<double>& b, int maxiter, double tol);
	bool gmressolve(vector<double>& x, vector<double>& b, int maxiter, double tol);

private:
	CompactUnSymmMatrix*	m_A;
	std::vector<double>		m_W;

	std::vector<double>		Wm, Wc;
	std::vector<double>		Rm, Rc;
	std::vector<double>		Rm_n, Rc_n;
	std::vector<double>		yu, yp;
	std::vector<double>		yu_n, yp_n;

	std::vector< std::vector<double> >	Yu, Yp;
	std::vector<double>	au, ap;

	vector< vector<double> > RM;
	vector< vector<double> > RC;

	vector< vector<double> > Rmu;
	vector< vector<double> > Rmp;
	vector< vector<double> > Rcu;
	vector< vector<double> > Rcp;

	int		m_print_level;	//!< level of output (0 is no output)
	int		m_split;		//!< set the split row index
	int		m_maxiter;		//!< max nr of BIPN iterations
	double	m_tol;			//!< BPIN convergence tolerance

	int		m_cg_maxiter;	//!< max CG iterations
	double	m_cg_tol;		//!< CG tolerance

	int		m_gmres_maxiter;	//!< max GMRES iterations
	double	m_gmres_tol;		//!< GMRES tolerance
};