#include "stdafx.h"
#include "FEDiscreteContact.h"
#include <FECore/FEModel.h>
#include <FECore/FEGlobalMatrix.h>
#include "FEContactInterface.h"
#include <FECore/FEClosestPointProjection.h>
#include <FECore/log.h>

FEDiscreteContactSurface::FEDiscreteContactSurface(FEModel* fem) : FEContactSurface(fem)
{

}

bool FEDiscreteContactSurface::Init()
{
	return FEContactSurface::Init();
}

BEGIN_PARAMETER_LIST(FEDiscreteContact, FENLConstraint)
	ADD_PARAMETER(m_blaugon , FE_PARAM_BOOL, "laugon");
	ADD_PARAMETER2(m_altol  , FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "altol");
	ADD_PARAMETER2(m_gaptol , FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "gaptol");
	ADD_PARAMETER2(m_penalty, FE_PARAM_DOUBLE, FE_RANGE_GREATER_OR_EQUAL(0.0), "penalty");
	ADD_PARAMETER(m_naugmin, FE_PARAM_INT   , "minaug");
	ADD_PARAMETER(m_naugmax, FE_PARAM_INT   , "maxaug");
	ADD_PARAMETER(m_nsegup , FE_PARAM_INT   , "segup");
END_PARAMETER_LIST();

FEDiscreteContact::FEDiscreteContact(FEModel* pfem) : FENLConstraint(pfem), m_surf(pfem)
{
	m_blaugon = false;
	m_altol = 0.01;
	m_penalty = 1.0;
	m_gaptol = 0.0;
	m_naugmin = 0;
	m_naugmax = 100;
	m_bfirst = true;
	m_nsegup = 0;
}

bool FEDiscreteContact::Init()
{
	return m_surf.Init();
}

void FEDiscreteContact::Activate()
{
	FENLConstraint::Activate();
	ProjectSurface(true);
}

void FEDiscreteContact::Update(const FETimePoint& tp)
{
	bool bupdate = (m_bfirst || (m_nsegup == 0)? true : (tp.niter <= m_nsegup));
	ProjectSurface(true);
	m_bfirst = false;
}

void FEDiscreteContact::SetDiscreteSet(FEDiscreteSet* pset)
{
	FEMesh& mesh = GetFEModel()->GetMesh();
	vector<int> tag(mesh.Nodes(), 0);
	m_Node.clear();
	int nsize = pset->size();
	for (int i=0; i<nsize; ++i)
	{
		const FEDiscreteSet::NodePair& delem = pset->Element(i);
		tag[delem.n0] = 1;
		tag[delem.n1] = 1;
	}
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		if (tag[i])
		{
			NODE node;
			node.nid = i;
			node.pe = 0;
			node.Lm = 0;
			node.gap = 0;
			node.nu = vec3d(0,0,0);
			node.q = vec3d(0,0,0);
			node.proj[0] = node.proj[1] = 0.0;

			m_Node.push_back(node);
		}
	}
}

void FEDiscreteContact::ProjectSurface(bool bsegup)
{
	FEClosestPointProjection cpp(m_surf);
	cpp.SetTolerance(0.01);
	cpp.SetSearchRadius(0.0);
	cpp.HandleSpecialCases(true);
	cpp.Init();

	// loop over all slave nodes
	FEMesh& mesh = *m_surf.GetMesh();
	for (int i=0; i<(int)m_Node.size(); ++i)
	{
		NODE& nodeData = m_Node[i];

		// get the node
		FENode& node = mesh.Node(nodeData.nid);

		// get the nodal position
		vec3d x = node.m_rt;

		// If the node is in contact, let's see if the node still is 
		// on the same master element
		if (nodeData.pe != 0)
		{
			FESurfaceElement& mel = *nodeData.pe;

			double r = nodeData.proj[0];
			double s = nodeData.proj[1];

			vec3d q = m_surf.ProjectToSurface(mel, x, r, s);
			nodeData.proj[0] = r;
			nodeData.proj[1] = s;
			nodeData.q = q;

			if (bsegup && (!m_surf.IsInsideElement(mel, r, s, 0.01)))
			{
				// see if the node might have moved to another master element
				vec2d rs(0,0);
				nodeData.pe = cpp.Project(x, q, rs);
				nodeData.proj[0] = rs.x();
				nodeData.proj[1] = rs.y();
				nodeData.q = q;
			}
		}
		else if (bsegup)
		{
			vec2d rs(0,0); vec3d q;
			nodeData.pe = cpp.Project(x, q, rs);
			nodeData.proj[0] = rs.x();
			nodeData.proj[1] = rs.y();
			nodeData.q = q;
		}

		// if we found a master element, update the gap and normal data
		if (nodeData.pe != 0)
		{
			FESurfaceElement& mel =  *nodeData.pe;

			double r = nodeData.proj[0];
			double s = nodeData.proj[1];

			// the slave normal is set to the master element normal
			nodeData.nu = m_surf.SurfaceNormal(mel, r, s);

			// calculate gap
			nodeData.gap = -(nodeData.nu*(x - nodeData.q));
		}
		else
		{
			// TODO: Is this a good criteria for out-of-contact?
			//		 perhaps this is not even necessary.
			// since the node is not in contact, we set the gap function 
			// and Lagrangian multiplier to zero
			nodeData.gap = 0;
			nodeData.Lm = 0;
		}
	}
}

void FEDiscreteContact::Residual(FEGlobalVector& R, const FETimePoint& tp)
{
	// element contact force vector
	vector<double> fe;

	// the lm array for this force vector
	vector<int> lm;

	// the en array
	vector<int> en;

	// the elements LM vectors
	vector<int> mLM;

	// loop over all slave nodes
	FEMesh& mesh = *m_surf.GetMesh();
	int nodes = (int) m_Node.size();
	for (int i=0; i<nodes; ++i)
	{
		NODE& nodeData = m_Node[i];

		FENode& node = mesh.Node(nodeData.nid);
		vector<int>& sLM = node.m_ID;

		FESurfaceElement* pe = nodeData.pe;

		// see if this node's constraint is active
		// that is, if it has a master element associated with it
		// TODO: is this a good way to test for an active constraint
		// The rigid wall criteria seems to work much better.
		if (pe != 0)
		{
			// This node is active and could lead to a non-zero
			// contact force.
			// get the master element
			FESurfaceElement& mel = *pe;
			m_surf.UnpackLM(mel, mLM);

			// calculate the degrees of freedom
			int nmeln = mel.Nodes();
			int ndof = 3*(nmeln+1);
			fe.resize(ndof);

			// calculate the nodal force
			ContactNodalForce(nodeData, mel, fe);

			// fill the lm array
			lm.resize(3*(nmeln+1));
			lm[0] = sLM[0];
			lm[1] = sLM[1];
			lm[2] = sLM[2];

			for (int l=0; l<nmeln; ++l)
			{
				lm[3*(l+1)  ] = mLM[l*3  ];
				lm[3*(l+1)+1] = mLM[l*3+1];
				lm[3*(l+1)+2] = mLM[l*3+2];
			}

			// fill the en array
			en.resize(nmeln+1);
			en[0] = nodeData.nid;
			for (int l=0; l<nmeln; ++l) en[l+1] = mel.m_node[l];

			// assemble into global force vector
			R.Assemble(en, lm, fe);
		}
	}
}

void FEDiscreteContact::ContactNodalForce(FEDiscreteContact::NODE& nodeData, FESurfaceElement& mel, vector<double>& fe)
{
	// max nr of master element nodes
	const int MAXMN = FEElement::MAX_NODES;

	// master element nodes
	vec3d rtm[MAXMN];

	// master shape function values at projection point
	double H[MAXMN];

	// contact forces
	double N[3*(MAXMN+1)];

	// get the mesh
	FEMesh& mesh = *m_surf.GetMesh();

	// gap function
	double gap = nodeData.gap;

	// penalty
	double eps = m_penalty;

	// get slave node normal force
	double Ln = nodeData.Lm;
	double tn = Ln + eps*gap;
	tn = MBRACKET(tn);

	// get the slave node normal
	vec3d nu = nodeData.nu;

	int nmeln = mel.Nodes();
	int ndof = 3*(1 + nmeln);

	// get the master element node positions
	for (int k=0; k<nmeln; ++k) rtm[k] = mesh.Node(mel.m_node[k]).m_rt;

	// isoparametric coordinates of the projected slave node
	// onto the master element
	double r = nodeData.proj[0];
	double s = nodeData.proj[1];

	// get the master shape function values at this slave node
	mel.shape_fnc(H, r, s);

	// calculate contact vectors for normal traction
	N[0] = nu.x;
	N[1] = nu.y;
	N[2] = nu.z;
	for (int l=0; l<nmeln; ++l)
	{
		N[3*(l+1)  ] = -H[l]*nu.x;
		N[3*(l+1)+1] = -H[l]*nu.y;
		N[3*(l+1)+2] = -H[l]*nu.z;
	}

	// calculate force vector
	for (int l=0; l<ndof; ++l) fe[l] = tn*N[l];
}

void FEDiscreteContact::StiffnessMatrix(FESolver* psolver, const FETimePoint& tp)
{
	matrix ke;

	const int MAXMN = FEElement::MAX_NODES;
	vector<int> lm(3*(MAXMN + 1));
	vector<int> en(MAXMN+1);

	vector<int> sLM;
	vector<int> mLM;

	// loop over all integration points (that is nodes)
	FEMesh& mesh = *m_surf.GetMesh();
	int nodes = (int) m_Node.size();
	for (int i=0; i<nodes; ++i)
	{
		NODE& nodeData = m_Node[i];

		vector<int>& sLM = mesh.Node(nodeData.nid).m_ID;

		// see if this node's constraint is active
		// that is, if it has a master element associated with it
		if (nodeData.pe != 0)
		{
			// get the master element
			FESurfaceElement& me = *nodeData.pe;

			// get the masters element's LM array
			m_surf.UnpackLM(me, mLM);

			int nmeln = me.Nodes();
			int ndof = 3*(nmeln+1);

			// calculate the stiffness matrix
			ke.resize(ndof, ndof);
			ContactNodalStiffness(nodeData, me, ke);

			// fill the lm array
			lm[0] = sLM[0];
			lm[1] = sLM[1];
			lm[2] = sLM[2];
			for (int k=0; k<nmeln; ++k)
			{
				lm[3*(k+1)  ] = mLM[k*3  ];
				lm[3*(k+1)+1] = mLM[k*3+1];
				lm[3*(k+1)+2] = mLM[k*3+2];
			}

			// create the en array
			en.resize(nmeln+1);
			en[0] = nodeData.nid;
			for (int k=0; k<nmeln; ++k) en[k+1] = me.m_node[k];
						
			// assemble stiffness matrix
			psolver->AssembleStiffness(en, lm, ke);
		}
	}
}

void FEDiscreteContact::ContactNodalStiffness(FEDiscreteContact::NODE& nodeData, FESurfaceElement& mel, matrix& ke)
{
	const int MAXMN = FEElement::MAX_NODES;

	vector<int> lm(3*(MAXMN+1));
	vector<int> en(MAXMN + 1);

	double H[MAXMN], Hr[MAXMN], Hs[MAXMN];
	double N[3*(MAXMN+1)], T1[3*(MAXMN+1)], T2[3*(MAXMN+1)];
	double N1[3*(MAXMN+1)], N2[3*(MAXMN+1)], D1[3*(MAXMN+1)], D2[3*(MAXMN+1)];
	double Nb1[3*(MAXMN+1)], Nb2[3*(MAXMN+1)];

	// get the mesh
	FEMesh& mesh = *m_surf.GetMesh();

	// nr of element nodes and degrees of freedom 
	int nmeln = mel.Nodes();
	int ndof = 3*(1 + nmeln);

	// penalty factor
	double eps = m_penalty;

	// nodal coordinates
	vec3d rt[MAXMN];
	for (int j=0; j<nmeln; ++j) rt[j] = mesh.Node(mel.m_node[j]).m_rt;

	// slave node natural coordinates in master element
	double r = nodeData.proj[0];
	double s = nodeData.proj[1];

	// slave gap
	double gap = nodeData.gap;

	// lagrange multiplier
	double Lm = nodeData.Lm;

	// get slave node normal force
	double tn = Lm + eps*gap;
	tn = MBRACKET(tn);

	// get the slave node normal
	vec3d nu = nodeData.nu;

	// get the master shape function values and the derivatives at this slave node
	mel.shape_fnc(H, r, s);
	mel.shape_deriv(Hr, Hs, r, s);

	// get the tangent vectors
	vec3d tau[2];
	m_surf.CoBaseVectors(mel, r, s, tau);

	// set up the N vector
	N[0] = nu.x;
	N[1] = nu.y;
	N[2] = nu.z;

	for (int k=0; k<nmeln; ++k) 
	{
		N[(k+1)*3  ] = -H[k]*nu.x;
		N[(k+1)*3+1] = -H[k]*nu.y;
		N[(k+1)*3+2] = -H[k]*nu.z;
	}
	
	// set up the Ti vectors
	T1[0] = tau[0].x; T2[0] = tau[1].x;
	T1[1] = tau[0].y; T2[1] = tau[1].y;
	T1[2] = tau[0].z; T2[2] = tau[1].z;

	for (int k=0; k<nmeln; ++k) 
	{
		T1[(k+1)*3  ] = -H[k]*tau[0].x;
		T1[(k+1)*3+1] = -H[k]*tau[0].y;
		T1[(k+1)*3+2] = -H[k]*tau[0].z;

		T2[(k+1)*3  ] = -H[k]*tau[1].x;
		T2[(k+1)*3+1] = -H[k]*tau[1].y;
		T2[(k+1)*3+2] = -H[k]*tau[1].z;
	}

	// set up the Ni vectors
	N1[0] = N2[0] = 0;
	N1[1] = N2[1] = 0;
	N1[2] = N2[2] = 0;

	for (int k=0; k<nmeln; ++k) 
	{
		N1[(k+1)*3  ] = -Hr[k]*nu.x;
		N1[(k+1)*3+1] = -Hr[k]*nu.y;
		N1[(k+1)*3+2] = -Hr[k]*nu.z;

		N2[(k+1)*3  ] = -Hs[k]*nu.x;
		N2[(k+1)*3+1] = -Hs[k]*nu.y;
		N2[(k+1)*3+2] = -Hs[k]*nu.z;
	}

	// calculate metric tensor
	mat2d M;
	M[0][0] = tau[0]*tau[0]; M[0][1] = tau[0]*tau[1]; 
	M[1][0] = tau[1]*tau[0]; M[1][1] = tau[1]*tau[1]; 

	// calculate reciprocal metric tensor
	mat2d Mi = M.inverse();

	// calculate curvature tensor
	double K[2][2] = {0};
	double Grr[FEElement::MAX_NODES];
	double Grs[FEElement::MAX_NODES];
	double Gss[FEElement::MAX_NODES];
	mel.shape_deriv2(Grr, Grs, Gss, r, s);
	for (int k=0; k<nmeln; ++k)
	{
		K[0][0] += (nu*rt[k])*Grr[k];
		K[0][1] += (nu*rt[k])*Grs[k];
		K[1][0] += (nu*rt[k])*Grs[k];
		K[1][1] += (nu*rt[k])*Gss[k];
	}

	// setup A matrix A = M + gK
	double A[2][2];
	A[0][0] = M[0][0] + gap*K[0][0];
	A[0][1] = M[0][1] + gap*K[0][1];
	A[1][0] = M[1][0] + gap*K[1][0];
	A[1][1] = M[1][1] + gap*K[1][1];

	// calculate determinant of A
	double detA = A[0][0]*A[1][1] - A[0][1]*A[1][0];

	// setup Di vectors
	for (int k=0; k<ndof; ++k)
	{
		D1[k] = (1/detA)*(A[1][1]*(T1[k]+gap*N1[k]) - A[0][1]*(T2[k] + gap*N2[k]));
		D2[k] = (1/detA)*(A[0][0]*(T2[k]+gap*N2[k]) - A[0][1]*(T1[k] + gap*N1[k]));
	}

	// setup Nbi vectors
	for (int k=0; k<ndof; ++k)
	{
		Nb1[k] = N1[k] - K[0][1]*D2[k];
		Nb2[k] = N2[k] - K[0][1]*D1[k];
	}

	// --- N O R M A L   S T I F F N E S S ---
	double sum;
	for (int k=0; k<ndof; ++k)
		for (int l=0; l<ndof; ++l)
			{
				sum = 0;

				sum = Mi[0][0]*Nb1[k]*Nb1[l]+Mi[0][1]*(Nb1[k]*Nb2[l]+Nb2[k]*Nb1[l])+Mi[1][1]*Nb2[k]*Nb2[l];
				sum *= gap;
				sum -= D1[k]*N1[l]+D2[k]*N2[l]+N1[k]*D1[l]+N2[k]*D2[l];
				sum += K[0][1]*(D1[k]*D2[l]+D2[k]*D1[l]);
				sum *= tn;

				sum += eps*HEAVYSIDE(Lm+eps*gap)*N[k]*N[l];
	
				ke[k][l] = sum;
			}		
}


bool FEDiscreteContact::Augment(int naug, const FETimePoint& tp)
{
	// make sure we need to augment
	if (!m_blaugon) return true;

	bool bconv = true;
	mat2d Mi;

	// penalty factor
	double eps = m_penalty;

	// --- c a l c u l a t e   i n i t i a l   n o r m s ---
	double normL0 = 0;
	for (int i=0; i<(int) m_Node.size(); ++i)	normL0 += m_Node[i].Lm * m_Node[i].Lm;
	normL0 = sqrt(normL0);

	// --- c a l c u l a t e   c u r r e n t   n o r m s ---
	double normL1 = 0;	// force norm
	double normg1 = 0;	// gap norm
	int N = 0;
	for (int i=0; i<(int)m_Node.size(); ++i)
	{
		// update Lagrange multipliers
		double Ln = m_Node[i].Lm + eps*m_Node[i].gap;
		Ln = MBRACKET(Ln);

		normL1 += Ln*Ln;

		if (m_Node[i].gap > 0)
		{
			normg1 += m_Node[i].gap*m_Node[i].gap;
			++N;
		}
	}	
	if (N == 0) N=1;

	normL1 = sqrt(normL1);
	normg1 = sqrt(normg1 / N);

	if (naug == 0) m_normg0 = 0;

	// calculate and print convergence norms
	double lnorm = 0, gnorm = 0;
	if (normL1 != 0) lnorm = fabs(normL1 - normL0)/normL1; else lnorm = fabs(normL1 - normL0);
	if (normg1 != 0) gnorm = fabs(normg1 - m_normg0)/normg1; else gnorm = fabs(normg1 - m_normg0);

	felog.printf(" discrete contact # %d\n", GetID());
	felog.printf("                        CURRENT        REQUIRED\n");
	felog.printf("    normal force : %15le", lnorm);
	if (m_altol > 0) felog.printf("%15le\n", m_altol); else felog.printf("       ***\n");
	felog.printf("    gap function : %15le", gnorm);
	if (m_gaptol > 0) felog.printf("%15le\n", m_gaptol); else felog.printf("       ***\n");

	// check convergence
	bconv = true;
	if ((m_altol > 0) && (lnorm > m_altol)) bconv = false;
	if ((m_gaptol > 0) && (gnorm > m_gaptol)) bconv = false;
	if (m_naugmin > naug) bconv = false;
	if (m_naugmax <= naug) bconv = true;
		
	if (bconv == false)
	{
		// we did not converge so update multipliers
		for (int i=0; i<(int) m_Node.size(); ++i)
		{
			NODE& node = m_Node[i];
			// update Lagrange multipliers
			double Ln = node.Lm + eps*node.gap;
			node.Lm = MBRACKET(Ln);
		}	
	}

	// store the last gap norm
	m_normg0 = normg1;

	return bconv;
}

void FEDiscreteContact::BuildMatrixProfile(FEGlobalMatrix& K)
{
	// TODO: this is currently for max 6 nodes (hence 7=6+1)
	vector<int> lm(6*7);

	FEModel& fem = *GetFEModel();
	FEMesh& mesh = fem.GetMesh();

	// get the DOFS
	const int dof_X = fem.GetDOFIndex("x");
	const int dof_Y = fem.GetDOFIndex("y");
	const int dof_Z = fem.GetDOFIndex("z");
	const int dof_RU = fem.GetDOFIndex("Ru");
	const int dof_RV = fem.GetDOFIndex("Rv");
	const int dof_RW = fem.GetDOFIndex("Rw");

	const int nodes = (int) m_Node.size();
	for (int i=0; i<nodes; ++i)
	{
		NODE& nodeData = m_Node[i];

		// get the FE node
		FENode& node = mesh.Node(nodeData.nid);

		// get the master surface element
		FESurfaceElement* pe = nodeData.pe;

		if (pe != 0)
		{
			FESurfaceElement& me = *pe;
			int* en = &me.m_node[0];

			// Note that we need to grab the rigid degrees of freedom as well
			// this is in case one of the nodes belongs to a rigid body.
			int n = me.Nodes();
			if (n == 3)
			{
				lm[6*(3+1)  ] = -1;lm[6*(3+2)  ] = -1;lm[6*(3+3)  ] = -1;
				lm[6*(3+1)+1] = -1;lm[6*(3+2)+1] = -1;lm[6*(3+3)+1] = -1;
				lm[6*(3+1)+2] = -1;lm[6*(3+2)+2] = -1;lm[6*(3+3)+2] = -1;
				lm[6*(3+1)+3] = -1;lm[6*(3+2)+3] = -1;lm[6*(3+3)+3] = -1;
				lm[6*(3+1)+4] = -1;lm[6*(3+2)+4] = -1;lm[6*(3+3)+4] = -1;
				lm[6*(3+1)+5] = -1;lm[6*(3+2)+5] = -1;lm[6*(3+3)+5] = -1;
			}
			if (n == 4)
			{
				lm[6*(4+1)  ] = -1;lm[6*(4+2)  ] = -1;
				lm[6*(4+1)+1] = -1;lm[6*(4+2)+1] = -1;
				lm[6*(4+1)+2] = -1;lm[6*(4+2)+2] = -1;
				lm[6*(4+1)+3] = -1;lm[6*(4+2)+3] = -1;
				lm[6*(4+1)+4] = -1;lm[6*(4+2)+4] = -1;
				lm[6*(4+1)+5] = -1;lm[6*(4+2)+5] = -1;
			}

			lm[0] = node.m_ID[dof_X];
			lm[1] = node.m_ID[dof_Y];
			lm[2] = node.m_ID[dof_Z];
			lm[3] = node.m_ID[dof_RU];
			lm[4] = node.m_ID[dof_RV];
			lm[5] = node.m_ID[dof_RW];

			for (int k=0; k<n; ++k)
			{
				vector<int>& id = mesh.Node(en[k]).m_ID;
				lm[6*(k+1)  ] = id[dof_X];
				lm[6*(k+1)+1] = id[dof_Y];
				lm[6*(k+1)+2] = id[dof_Z];
				lm[6*(k+1)+3] = id[dof_RU];
				lm[6*(k+1)+4] = id[dof_RV];
				lm[6*(k+1)+5] = id[dof_RW];
			}

			K.build_add(lm);
		}
	}
}