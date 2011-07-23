#include "stdafx.h"
#include "FECore/FEMaterial.h"
#include "FEBiphasicSoluteDomain.h"
#include "FESolidSolver.h"
#include "FEMicroMaterial.h"
#include "FEBiphasic.h"
#include "log.h"

//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::Residual(FESolidSolver* psolver, vector<double>& R)
{
	int i, j;
	
	FEM& fem = psolver->m_fem;
	
	// make sure we are in poro-solute mode
	assert(fem.m_pStep->m_nModule == FE_POROSOLUTE);
	
	// element force vector
	vector<double> fe;

	vector<int> elm;
	
	int NE = m_Elem.size();
	if (fem.m_pStep->m_nanalysis == FE_STEADY_STATE) {
		for (i=0; i<NE; ++i)
		{
			// get the element
			FESolidElement& el = m_Elem[i];
			
			// this element should not be rigid
			assert(!el.IsRigid());
			
			//! this element should not be UDG
			assert(el.Type() != FE_UDGHEX);
			
			// unpack the element
			UnpackLM(el, elm);
			
			// get the element force vector and initialize it to zero
			int ndof = 3*el.Nodes();
			fe.assign(ndof, 0);
			
			// calculate internal force vector
			// (This function is inherited from FEElasticSolidDomain)
			InternalForces(el, fe);
			
			// apply body forces
			// TODO: can we calculate body-forces with our formulation
			//       of biphasic theory
			/*
			 if (fem.UseBodyForces())
			 {
			 BodyForces(fem, el, fe);
			 }
			 */
			
			// assemble element 'fe'-vector into global R vector
			psolver->AssembleResidual(el.m_node, elm, fe, R);
			
			FEMaterial* pm = fem.GetMaterial(el.GetMatID());
			assert(dynamic_cast<FEBiphasicSolute*>(pm) != 0);
			
			// calculate fluid internal work
			InternalFluidWorkSS(fem, el, fe);
			
			// add fluid work to global residual
			int neln = el.Nodes();
			int J;
			for (j=0; j<neln; ++j)
			{
				J = elm[3*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
			
			// calculate solute internal work
			InternalSoluteWorkSS(fem, el, fe);
			
			// add solute work to global residual
			for (j=0; j<neln; ++j)
			{
				J = elm[11*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
		}
	} else {
		for (i=0; i<NE; ++i)
		{
			// get the element
			FESolidElement& el = m_Elem[i];
			
			// this element should not be rigid
			assert(!el.IsRigid());
			
			//! this element should not be UDG
			assert(el.Type() != FE_UDGHEX);
			
			// unpack the element
			UnpackLM(el, elm);
			
			// get the element force vector and initialize it to zero
			int ndof = 3*el.Nodes();
			fe.assign(ndof, 0);
			
			// calculate internal force vector
			// (This function is inherited from FEElasticSolidDomain)
			InternalForces(el, fe);
			
			// apply body forces
			// TODO: can we calculate body-forces with our formulation
			//       of biphasic theory
			/*
			 if (fem.UseBodyForces())
			 {
			 BodyForces(fem, el, fe);
			 }
			 */
			
			// assemble element 'fe'-vector into global R vector
			psolver->AssembleResidual(el.m_node, elm, fe, R);
			
			FEMaterial* pm = fem.GetMaterial(el.GetMatID());
			assert(dynamic_cast<FEBiphasicSolute*>(pm) != 0);
			
			// calculate fluid internal work
			InternalFluidWork(fem, el, fe);
			
			// add fluid work to global residual
			int neln = el.Nodes();
			int J;
			for (j=0; j<neln; ++j)
			{
				J = elm[3*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
			
			// calculate solute internal work
			InternalSoluteWork(fem, el, fe);
			
			// add solute work to global residual
			for (j=0; j<neln; ++j)
			{
				J = elm[11*neln+j];
				if (J >= 0) R[J] += fe[j];
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::InternalFluidWork(FEM& fem, FESolidElement& el, vector<double>& fe)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ, J0i[3][3];
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz, GX, GY, GZ;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
	
	FEMesh& mesh = fem.m_mesh;
	
	vec3d rp[8];
	for (i=0; i<neln; ++i) 
	{
		rp[i] = mesh.Node(el.m_node[i]).m_rp;
	}
	
	// get the element's material
	FEBiphasicSolute* pm = dynamic_cast<FEBiphasicSolute*> (fem.GetMaterial(el.GetMatID()));
	if (pm == 0)
	{
		clog.printbox("FATAL ERROR", "Incorrect material type\n");
		return false;
	}
	
	zero(fe);
	
	// get the time step value
	double dt = fem.m_pStep->m_dt;
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.m_State[n];
		FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
		FESolutePoroElasticMaterialPoint& pt = *(el.m_State[n]->ExtractData<FESolutePoroElasticMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
		
		// we need to calculate the divergence of v. To do this we use
		// the formula div(v) = 1/J*dJdt, where J = det(F)
		invjac0(el, J0i, n);
		
		// next we calculate the deformation gradient
		mat3d Fp;
		Fp.zero();
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			GX = J0i[0][0]*Gr[i]+J0i[1][0]*Gs[i]+J0i[2][0]*Gt[i];
			GY = J0i[0][1]*Gr[i]+J0i[1][1]*Gs[i]+J0i[2][1]*Gt[i];
			GZ = J0i[0][2]*Gr[i]+J0i[1][2]*Gs[i]+J0i[2][2]*Gt[i];
			
			Fp[0][0] += rp[i].x*GX; Fp[1][0] += rp[i].y*GX; Fp[2][0] += rp[i].z*GX;
			Fp[0][1] += rp[i].x*GY; Fp[1][1] += rp[i].y*GY; Fp[2][1] += rp[i].z*GY;
			Fp[0][2] += rp[i].x*GZ; Fp[1][2] += rp[i].y*GZ; Fp[2][2] += rp[i].z*GZ;
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
		}
		
		// next we get the determinant
		double Jp = Fp.det();
		double J = ept.J;
		
		// and then finally
		double divv = ((J-Jp)/dt)/J;
		
		// get the flux
		vec3d& w = pt.m_w;
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*w.x+B2[i]*w.y+B3[i]*w.z - divv*H[i])*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! for a steady-state analysis (zero solid velocity)
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::InternalFluidWorkSS(FEM& fem, FESolidElement& el, vector<double>& fe)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ;
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
	
	// get the element's material
	FEBiphasicSolute* pm = dynamic_cast<FEBiphasicSolute*> (fem.GetMaterial(el.GetMatID()));
	if (pm == 0)
	{
		clog.printbox("FATAL ERROR", "Incorrect material type\n");
		return false;
	}
	
	zero(fe);
	
	// get the time step value
	double dt = fem.m_pStep->m_dt;
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FESolutePoroElasticMaterialPoint& pt = *(el.m_State[n]->ExtractData<FESolutePoroElasticMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
		}
		
		// get the flux
		vec3d& w = pt.m_w;
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*w.x+B2[i]*w.y+B3[i]*w.z)*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::InternalSoluteWork(FEM& fem, FESolidElement& el, vector<double>& fe)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ, J0i[3][3];
	
	double *Gr, *Gs, *Gt, *H;
	double *Grr, *Gsr, *Gtr, *Grs, *Gss, *Gts, *Grt, *Gst, *Gtt;
	double Gx, Gy, Gz, GX, GY, GZ;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
	
	FEMesh& mesh = fem.m_mesh;
	
	vec3d r0[8], rt[8], rp[8], vt[8];
	double cp[8];
	for (i=0; i<neln; ++i) 
	{
		r0[i] = mesh.Node(el.m_node[i]).m_r0;
		rt[i] = mesh.Node(el.m_node[i]).m_rt;
		rp[i] = mesh.Node(el.m_node[i]).m_rp;
		cp[i] = mesh.Node(el.m_node[i]).m_cp;
		vt[i] = mesh.Node(el.m_node[i]).m_vt;
	}
	
	// get the element's material
	FEBiphasicSolute* pm = dynamic_cast<FEBiphasicSolute*> (fem.GetMaterial(el.GetMatID()));
	if (pm == 0)
	{
		clog.printbox("FATAL ERROR", "Incorrect material type\n");
		return false;
	}
	
	zero(fe);
	
	// get the time step value
	double dt = fem.m_pStep->m_dt;
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.m_State[n];
		FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
		FESolutePoroElasticMaterialPoint& pt = *(el.m_State[n]->ExtractData<FESolutePoroElasticMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);

		vec3d g1(Ji[0][0],Ji[0][1],Ji[0][2]);
		vec3d g2(Ji[1][0],Ji[1][1],Ji[1][2]);
		vec3d g3(Ji[2][0],Ji[2][1],Ji[2][2]);
		
		// we need to calculate the divergence of v. To do this we use
		// the formula div(v) = 1/J*dJdt, where J = det(F)
		invjac0(el, J0i, n);
		vec3d G1(J0i[0][0],J0i[0][1],J0i[0][2]);
		vec3d G2(J0i[1][0],J0i[1][1],J0i[1][2]);
		vec3d G3(J0i[2][0],J0i[2][1],J0i[2][2]);
		
		// next we calculate the deformation gradient and the solid velocity
		mat3d Fp;
		Fp.zero();
		vec3d vs(0);
		vec3d gradJ(0);
		double cprev = 0;
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		Grr = el.Grr(n); Grs = el.Grs(n); Grt = el.Grt(n);
		Gsr = el.Gsr(n); Gss = el.Gss(n); Gst = el.Gst(n);
		Gtr = el.Gtr(n); Gts = el.Gts(n); Gtt = el.Gtt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			GX = J0i[0][0]*Gr[i]+J0i[1][0]*Gs[i]+J0i[2][0]*Gt[i];
			GY = J0i[0][1]*Gr[i]+J0i[1][1]*Gs[i]+J0i[2][1]*Gt[i];
			GZ = J0i[0][2]*Gr[i]+J0i[1][2]*Gs[i]+J0i[2][2]*Gt[i];
			
			Fp[0][0] += rp[i].x*GX; Fp[1][0] += rp[i].y*GX; Fp[2][0] += rp[i].z*GX;
			Fp[0][1] += rp[i].x*GY; Fp[1][1] += rp[i].y*GY; Fp[2][1] += rp[i].z*GY;
			Fp[0][2] += rp[i].x*GZ; Fp[1][2] += rp[i].y*GZ; Fp[2][2] += rp[i].z*GZ;
			
			// calculate solid velocity
			vs += vt[i]*H[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
			
			// calculate gradJ
			gradJ += (g1*Grr[i] + g2*Grs[i] + g3*Grt[i])*(rt[i]*g1-r0[i]*G1)
			+ (g1*Gsr[i] + g2*Gss[i] + g3*Gst[i])*(rt[i]*g2-r0[i]*G2)
			+ (g1*Gtr[i] + g2*Gts[i] + g3*Gtt[i])*(rt[i]*g3-r0[i]*G3);
			
			// calculate effective concentration at previous time step
			cprev += cp[i]*H[i];
		}
		
		// next we get the determinant
		double Jp = Fp.det();
		double J = ept.J;
		double dJdt = (J-Jp)/dt;
		gradJ *= J;
		
		// and then finally
		double divv = dJdt/J;
		
		// get the solute flux
		vec3d& j = pt.m_j;
		// get the effective concentration and its gradient
		double c = pt.m_c;
		vec3d gradc = pt.m_gradc;
		
		// evaluate the solubility and its derivatives w.r.t. J and c, and its gradient
		double kappa = pm->m_pSolub->Solubility(mp);
		double dkdJ = pm->m_pSolub->Tangent_Solubility_Strain(mp);
		double dkdc = pm->m_pSolub->Tangent_Solubility_Concentration(mp);
		vec3d gradk = gradJ*dkdJ;
		// evaluate the porosity, its derivative w.r.t. J, and its gradient
		double phiw = pm->Porosity(mp);
		double dpdJ = (1. - phiw)/J;
		vec3d gradp = gradJ*dpdJ;
		// evaluate time derivatives of concentration, solubility and porosity
		double dcdt = (c - cprev)/dt;
		double dkdt = dkdJ*dJdt + dkdc*dcdt;
		double dpdt = dpdJ*dJdt;
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*j.x+B2[i]*j.y+B3[i]*j.z 
						 - H[i]*(dpdt*kappa*c+phiw*dkdt*c+phiw*kappa*dcdt
								 +phiw*kappa*c*divv)
						 )*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------
//! calculates the internal equivalent nodal forces due to the fluid work
//! for steady-state response (zero solid velocity, zero time derivative of
//! solute concentration)
//! Note that we only use the first n entries in fe, where n is the number
//! of nodes

bool FEBiphasicSoluteDomain::InternalSoluteWorkSS(FEM& fem, FESolidElement& el, vector<double>& fe)
{
	int i, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	// jacobian
	double Ji[3][3], detJ;
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	
	// gauss-weights
	double* wg = el.GaussWeights();
		
	// get the element's material
	FEBiphasicSolute* pm = dynamic_cast<FEBiphasicSolute*> (fem.GetMaterial(el.GetMatID()));
	if (pm == 0)
	{
		clog.printbox("FATAL ERROR", "Incorrect material type\n");
		return false;
	}
	
	zero(fe);
	
	// get the time step value
	double dt = fem.m_pStep->m_dt;
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FESolutePoroElasticMaterialPoint& pt = *(el.m_State[n]->ExtractData<FESolutePoroElasticMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
				
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
		}
		
		// get the solute flux
		vec3d& j = pt.m_j;
		
		// update force vector
		for (i=0; i<neln; ++i)
		{
			fe[i] -= dt*(B1[i]*j.x+B2[i]*j.y+B3[i]*j.z)*detJ*wg[n];
		}
	}
	
	return true;
}


//-----------------------------------------------------------------------------

void FEBiphasicSoluteDomain::StiffnessMatrix(FESolidSolver* psolver)
{
	FEM& fem = psolver->m_fem;
	
	// element stiffness matrix
	matrix ke;

	vector<int> elm;
	
	// repeat over all solid elements
	int NE = m_Elem.size();
	if (fem.m_pStep->m_nanalysis == FE_STEADY_STATE) {
		for (int iel=0; iel<NE; ++iel)
		{
			FESolidElement& el = m_Elem[iel];
			
			// this element should not be rigid
			assert(!el.IsRigid());
			
			UnpackLM(el, elm);
			
			// get the elements material
			FEMaterial* pmat = fem.GetMaterial(el.GetMatID());
			assert(dynamic_cast<FEBiphasicSolute*>(pmat) != 0);
			
			// allocate stiffness matrix
			int neln = el.Nodes();
			int ndof = neln*5;
			ke.Create(ndof, ndof);
			
			// calculate the element stiffness matrix
			ElementBiphasicSoluteStiffnessSS(fem, el, ke);
			
			// TODO: the problem here is that the LM array that is returned by the UnpackLM
			// function does not give the equation numbers in the right order. For this reason we
			// have to create a new lm array and place the equation numbers in the right order.
			// What we really ought to do is fix the UnpackLM function so that it returns
			// the LM vector in the right order for solute-solid elements.
			vector<int> lm(ndof);
			for (int i=0; i<neln; ++i)
			{
				lm[5*i  ] = elm[3*i];
				lm[5*i+1] = elm[3*i+1];
				lm[5*i+2] = elm[3*i+2];
				lm[5*i+3] = elm[3*neln+i];
				lm[5*i+4] = elm[11*neln+i];
			}
			
			// assemble element matrix in global stiffness matrix
			psolver->AssembleStiffness(el.m_node, lm, ke);
		}
	} else {
		for (int iel=0; iel<NE; ++iel)
		{
			FESolidElement& el = m_Elem[iel];
			
			// this element should not be rigid
			assert(!el.IsRigid());
			
			UnpackLM(el, elm);
			
			// get the elements material
			FEMaterial* pmat = fem.GetMaterial(el.GetMatID());
			assert(dynamic_cast<FEBiphasicSolute*>(pmat) != 0);
			
			// allocate stiffness matrix
			int neln = el.Nodes();
			int ndof = neln*5;
			ke.Create(ndof, ndof);
			
			// calculate the element stiffness matrix
			ElementBiphasicSoluteStiffness(fem, el, ke);
			
			// TODO: the problem here is that the LM array that is returned by the UnpackLM
			// function does not give the equation numbers in the right order. For this reason we
			// have to create a new lm array and place the equation numbers in the right order.
			// What we really ought to do is fix the UnpackLM function so that it returns
			// the LM vector in the right order for solute-solid elements.
			vector<int> lm(ndof);
			for (int i=0; i<neln; ++i)
			{
				lm[5*i  ] = elm[3*i];
				lm[5*i+1] = elm[3*i+1];
				lm[5*i+2] = elm[3*i+2];
				lm[5*i+3] = elm[3*neln+i];
				lm[5*i+4] = elm[11*neln+i];
			}
			
			// assemble element matrix in global stiffness matrix
			psolver->AssembleStiffness(el.m_node, lm, ke);
		}
	}
}

//-----------------------------------------------------------------------------
//! calculates element stiffness matrix for element iel
//!
bool FEBiphasicSoluteDomain::ElementBiphasicSoluteStiffness(FEM& fem, FESolidElement& el, matrix& ke)
{
	int i, j, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz, GX, GY, GZ;
	
	// jacobian
	double Ji[3][3], detJ, J0i[3][3];
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	vector<vec3d> gradN(neln);
	double tmp;
	
	// gauss-weights
	double* gw = el.GaussWeights();
	
	FEMesh& mesh = fem.m_mesh;
	
	vec3d r0[8], rt[8], rp[8], v[8];
	double cp[8];
	for (i=0; i<neln; ++i) 
	{
		r0[i] = mesh.Node(el.m_node[i]).m_r0;
		rt[i] = mesh.Node(el.m_node[i]).m_rt;
		rp[i] = mesh.Node(el.m_node[i]).m_rp;
		cp[i] = mesh.Node(el.m_node[i]).m_cp;
		v[i]  = mesh.Node(el.m_node[i]).m_vt;
	}
	
	// zero stiffness matrix
	ke.zero();
	
	// calculate solid stiffness matrix
	int ndof = 3*el.Nodes();
	matrix ks(ndof, ndof); ks.zero();
	SolidElementStiffness(fem, el, ks);
	
	// copy solid stiffness matrix into ke
	for (i=0; i<neln; ++i)
		for (j=0; j<neln; ++j)
		{
			ke[5*i  ][5*j] = ks[3*i  ][3*j  ]; ke[5*i  ][5*j+1] = ks[3*i  ][3*j+1]; ke[5*i  ][5*j+2] = ks[3*i  ][3*j+2];
			ke[5*i+1][5*j] = ks[3*i+1][3*j  ]; ke[5*i+1][5*j+1] = ks[3*i+1][3*j+1]; ke[5*i+1][5*j+2] = ks[3*i+1][3*j+2];
			ke[5*i+2][5*j] = ks[3*i+2][3*j  ]; ke[5*i+2][5*j+1] = ks[3*i+2][3*j+1]; ke[5*i+2][5*j+2] = ks[3*i+2][3*j+2];
		}
	
	// get the element's material
	FEBiphasicSolute* pm = dynamic_cast<FEBiphasicSolute*> (fem.GetMaterial(el.GetMatID()));
	if (pm == 0)
	{
		clog.printbox("FATAL ERROR", "Incorrect material type\n");
		return false;
	}
	
	// check if we use the symmetric version of the poro-implementation
	bool bsymm = fem.m_bsym_poro;
	double dt = fem.m_pStep->m_dt;
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.m_State[n];
		FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
		FESolutePoroElasticMaterialPoint& pt = *(el.m_State[n]->ExtractData<FESolutePoroElasticMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);

		vec3d g1(Ji[0][0],Ji[0][1],Ji[0][2]);
		vec3d g2(Ji[1][0],Ji[1][1],Ji[1][2]);
		vec3d g3(Ji[2][0],Ji[2][1],Ji[2][2]);
		
		// we need to calculate the divergence of v. To do this we use
		// the formula div(v) = 1/J*dJdt, where J = det(F)
		invjac0(el, J0i, n);
		vec3d G1(J0i[0][0],J0i[0][1],J0i[0][2]);
		vec3d G2(J0i[1][0],J0i[1][1],J0i[1][2]);
		vec3d G3(J0i[2][0],J0i[2][1],J0i[2][2]);
		
		// next we calculate the deformation gradient and the solid velocity
		mat3d Fp, gradv;
		Fp.zero();
		gradv.zero();
		vec3d vs(0);
		double cprev = 0;
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			GX = J0i[0][0]*Gr[i]+J0i[1][0]*Gs[i]+J0i[2][0]*Gt[i];
			GY = J0i[0][1]*Gr[i]+J0i[1][1]*Gs[i]+J0i[2][1]*Gt[i];
			GZ = J0i[0][2]*Gr[i]+J0i[1][2]*Gs[i]+J0i[2][2]*Gt[i];
			
			Fp[0][0] += rp[i].x*GX; Fp[1][0] += rp[i].y*GX; Fp[2][0] += rp[i].z*GX;
			Fp[0][1] += rp[i].x*GY; Fp[1][1] += rp[i].y*GY; Fp[2][1] += rp[i].z*GY;
			Fp[0][2] += rp[i].x*GZ; Fp[1][2] += rp[i].y*GZ; Fp[2][2] += rp[i].z*GZ;
			
			// calculate solid velocity and its gradient
			vs += v[i]*H[i];
			gradv[0][0] += v[i].x*Gx; gradv[1][0] += v[i].y*Gx; gradv[2][0] += v[i].z*Gx;
			gradv[0][1] += v[i].x*Gy; gradv[1][1] += v[i].y*Gy; gradv[2][1] += v[i].z*Gy;
			gradv[0][2] += v[i].x*Gz; gradv[1][2] += v[i].y*Gz; gradv[2][2] += v[i].z*Gz;
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
			gradN[i] = vec3d(Gx,Gy,Gz);
			
			// calculate effective concentration at previous time step
			cprev += cp[i]*H[i];
		}
		
		// next we get the determinant
		double Jp = Fp.det();
		double J = ept.J;
		
		// and then finally
		double dJdt = (J-Jp)/dt;
		double divv = dJdt/J;
		
		// get the fluid flux and pressure gradient
		vec3d w = pt.m_w;
		vec3d gradp = pt.m_gradp;
		
		// get the effective concentration, its gradient and its time derivative
		double c = pt.m_c;
		vec3d gradc = pt.m_gradc;
		double dcdt = (c - cprev)/dt;
		
		// evaluate the permeability and its derivatives
		mat3ds K = pm->m_pPerm->Permeability(mp);
		tens4ds dKdE = pm->m_pPerm->Tangent_Permeability_Strain(mp);
		mat3ds dKdc = pm->m_pPerm->Tangent_Permeability_Concentration(mp); 
		
		// evaluate the porosity and its derivative
		double phiw = pm->Porosity(mp);
		double phis = 1. - phiw;
		double dpdJ = phis/J;
		double dpdJJ = -2*phis/(J*J);
		
		// evaluate the solubility and its derivatives
		double kappa = pm->m_pSolub->Solubility(mp);
		double dkdJ = pm->m_pSolub->Tangent_Solubility_Strain(mp);
		double dkdJJ = pm->m_pSolub->Tangent_Solubility_Strain_Strain(mp);
		double dkdc = pm->m_pSolub->Tangent_Solubility_Concentration(mp);
		double dkdcc = 0;	// TODO: temporary, until I implent it in FESoluteSolubility
		double dkdJc = pm->m_pSolub->Tangent_Solubility_Strain_Concentration(mp);
		
		// evaluate the diffusivity tensor and its derivatives
		mat3ds D = pm->m_pDiff->Diffusivity(mp);
		mat3ds dDdc = pm->m_pDiff->Tangent_Diffusivity_Concentration(mp);
		tens4ds dDdE = pm->m_pDiff->Tangent_Diffusivity_Strain(mp);
		
		// evaluate the solute free diffusivity
		double D0 = pm->m_pDiff->Free_Diffusivity(mp);
		double dD0dc = 0;	// TODO: temporary, until I implement it in FESoluteDiffusivity
		
		// evaluate the osmotic coefficient and its derivatives
		double osmc = pm->m_pOsmC->OsmoticCoefficient(mp);
		double dodc = pm->m_pOsmC->Tangent_OsmoticCoefficient_Concentration(mp);
		
		// evaluate the stress tangent with concentration
		mat3ds dTdc = pm->m_pSolid->Tangent_Concentration(mp);
		
		// Miscellaneous constants
		mat3dd I(1);
		double R = pm->m_Rgas;
		double T = pm->m_Tabs;
		
		// evaluate the effective permeability and its derivatives
		mat3ds Ki = K.inverse();
		mat3ds ImD = I-D/D0;
		mat3ds Ke = (Ki + ImD*(R*T*kappa*c/phiw/D0)).inverse();
		tens4ds G = dyad1s(Ki,I) - dyad4s(Ki,I)*2 - ddots(dyad2s(Ki),dKdE)*0.5
		+dyad1s(ImD,I)*(R*T*c*J/D0/2/phiw*(dkdJ-kappa/phiw*dpdJ))
		+(dyad1s(I) - dyad4s(I)*2 - dDdE/D0)*(R*T*kappa*c/phiw/D0);
		tens4ds dKedE = dyad1s(Ke,I) - 2*dyad4s(Ke,I) - ddots(dyad2s(Ke),G)*0.5;
		mat3ds Gc = -Ki*dKdc*Ki + ImD*(R*T/phiw/D0*(dkdc*c+kappa-kappa*c/D0*dD0dc))
		+R*T*kappa*c/phiw/D0/D0*(D*dD0dc/D0 - dDdc);
		mat3ds dKedc = -Ke*Gc*Ke;
		
		// calculate all the matrices
		vec3d vtmp,gp,gc,qpu,qcu,wc,jc;
		mat3d wu,ju;
		double qcc;
		tmp = detJ*gw[n];
		for (i=0; i<neln; ++i)
		{
			for (j=0; j<neln; ++j)
			{
				// calculate the kpu matrix
				gp = gradp+(D*gradc)*R*T*kappa/D0;
				wu = vdotTdotv(-gp, dKedE, gradN[j])
				-(((Ke*(D*gradc)) & gradN[j])*(J*dkdJ - kappa)
				  +Ke*(2*kappa*(gradN[j]*(D*gradc))))*R*T/D0
				- Ke*vdotTdotv(gradc, dDdE, gradN[j])*(kappa*R*T/D0);
				qpu = -gradN[j]*(divv+1/dt)-gradv.transpose()*gradN[j];
				vtmp = (wu.transpose()*gradN[i] + qpu*H[i])*(tmp*dt);
				ke[5*i+3][5*j  ] += vtmp.x;
				ke[5*i+3][5*j+1] += vtmp.y;
				ke[5*i+3][5*j+2] += vtmp.z;
				
				// calculate the kcu matrix
				gc = -gradc*phiw + w*c/D0;
				ju = ((D*gc) & gradN[j])*(J*dkdJ) 
				+ vdotTdotv(gc, dDdE, gradN[j])*kappa
				+ (((D*gradc) & gradN[j])*(-phis)
				   +(D*((gradN[j]*w)*2) - ((D*w) & gradN[j]))*c/D0
				   )*kappa
				+D*wu*(kappa*c/D0);
				qcu = -gradN[j]*(c*dJdt*(2*(dpdJ*kappa+phiw*dkdJ+J*dpdJ*dkdJ)
										 +J*(dpdJJ*kappa+phiw*dkdJJ))
								 +dcdt*((phiw+J*dpdJ)*(kappa+dkdc*c)
										+J*phiw*(dkdJ+dkdJc*c)))
				+qpu*(c*(phiw*kappa+J*dpdJ*kappa+J*phiw*dkdJ));
				vtmp = (ju.transpose()*gradN[i] + qcu*H[i])*(tmp*dt);
				ke[5*i+4][5*j  ] += vtmp.x;
				ke[5*i+4][5*j+1] += vtmp.y;
				ke[5*i+4][5*j+2] += vtmp.z;
				
				// calculate the kup matrix
				vtmp = -gradN[i]*H[j]*tmp;
				ke[5*i  ][5*j+3] += vtmp.x;
				ke[5*i+1][5*j+3] += vtmp.y;
				ke[5*i+2][5*j+3] += vtmp.z;
				
				// calculate the kpp matrix
				ke[5*i+3][5*j+3] -= gradN[i]*(Ke*gradN[j])*(tmp*dt);
				
				// calculate the kcp matrix
				ke[5*i+4][5*j+3] -= (gradN[i]*((D*Ke)*gradN[j]))*(kappa*c/D0)*(tmp*dt);

				// calculate the kuc matrix
				vtmp = (dTdc*gradN[i] - gradN[i]*(R*T*(dodc*kappa*c+osmc*dkdc*c+osmc*kappa)))*H[j]*tmp;
				ke[5*i  ][5*j+4] += vtmp.x;
				ke[5*i+1][5*j+4] += vtmp.y;
				ke[5*i+2][5*j+4] += vtmp.z;
				
				// calculate the kpc matrix
				wc = (dKedc*gp)*(-H[j])
				-Ke*((((D*(dkdc-kappa*dD0dc/D0)+dDdc*(kappa/D0))*gradc)*H[j]
					  +(D*gradN[j])*kappa)*(R*T/D0));
				ke[5*i+3][5*j+4] += (gradN[i]*wc)*(tmp*dt);
				
				// calculate the kcc matrix
				jc = ((D*dkdc+dDdc*kappa)*gc)*H[j]
				+D*((-gradN[j]*phiw+wc*(c/D0))*kappa);
				qcc = -H[j]*((phiw+J*dpdJ)*divv*(kappa+c*dkdc)
				+phiw*((2*dkdc+c*dkdcc)*dcdt+(kappa+c*dkdc)/dt+dJdt*(dkdJ+c*dkdJc)));
				ke[5*i+4][5*j+4] += (gradN[i]*jc + H[i]*qcc)*(tmp*dt);
				
			}
		}
	}
	
	// Enforce symmetry by averaging top-right and bottom-left corners of stiffness matrix
	if (bsymm) {
		for (i=0; i<5*neln; ++i)
			for (j=i+1; j<5*neln; ++j) {
				tmp = 0.5*(ke[i][j]+ke[j][i]);
				ke[i][j] = ke[j][i] = tmp;
			}
	}
	
	return true;
}

//-----------------------------------------------------------------------------
//! calculates element stiffness matrix for element iel
//!
bool FEBiphasicSoluteDomain::ElementBiphasicSoluteStiffnessSS(FEM& fem, FESolidElement& el, matrix& ke)
{
	int i, j, n;
	
	int nint = el.GaussPoints();
	int neln = el.Nodes();
	
	double *Gr, *Gs, *Gt, *H;
	double Gx, Gy, Gz;
	
	// jacobian
	double Ji[3][3], detJ;
	
	// Bp-matrix
	vector<double> B1(neln), B2(neln), B3(neln);
	vector<vec3d> gradN(neln);
	double tmp;
	
	// gauss-weights
	double* gw = el.GaussWeights();
		
	// zero stiffness matrix
	ke.zero();
	
	// calculate solid stiffness matrix
	int ndof = 3*el.Nodes();
	matrix ks(ndof, ndof); ks.zero();
	SolidElementStiffness(fem, el, ks);
	
	// copy solid stiffness matrix into ke
	for (i=0; i<neln; ++i)
		for (j=0; j<neln; ++j)
		{
			ke[5*i  ][5*j] = ks[3*i  ][3*j  ]; ke[5*i  ][5*j+1] = ks[3*i  ][3*j+1]; ke[5*i  ][5*j+2] = ks[3*i  ][3*j+2];
			ke[5*i+1][5*j] = ks[3*i+1][3*j  ]; ke[5*i+1][5*j+1] = ks[3*i+1][3*j+1]; ke[5*i+1][5*j+2] = ks[3*i+1][3*j+2];
			ke[5*i+2][5*j] = ks[3*i+2][3*j  ]; ke[5*i+2][5*j+1] = ks[3*i+2][3*j+1]; ke[5*i+2][5*j+2] = ks[3*i+2][3*j+2];
		}
	
	// get the element's material
	FEBiphasicSolute* pm = dynamic_cast<FEBiphasicSolute*> (fem.GetMaterial(el.GetMatID()));
	if (pm == 0)
	{
		clog.printbox("FATAL ERROR", "Incorrect material type\n");
		return false;
	}
	
	// check if we use the symmetric version of the poro-implementation
	bool bsymm = fem.m_bsym_poro;
	double dt = fem.m_pStep->m_dt;
	
	// loop over gauss-points
	for (n=0; n<nint; ++n)
	{
		FEMaterialPoint& mp = *el.m_State[n];
		FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
		FESolutePoroElasticMaterialPoint& pt = *(el.m_State[n]->ExtractData<FESolutePoroElasticMaterialPoint>());
		
		// calculate jacobian
		detJ = invjact(el, Ji, n);
		
		Gr = el.Gr(n);
		Gs = el.Gs(n);
		Gt = el.Gt(n);
		
		H = el.H(n);
		
		for (i=0; i<neln; ++i)
		{
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx = Ji[0][0]*Gr[i]+Ji[1][0]*Gs[i]+Ji[2][0]*Gt[i];
			Gy = Ji[0][1]*Gr[i]+Ji[1][1]*Gs[i]+Ji[2][1]*Gt[i];
			Gz = Ji[0][2]*Gr[i]+Ji[1][2]*Gs[i]+Ji[2][2]*Gt[i];
			
			// calculate Bp matrix
			B1[i] = Gx;
			B2[i] = Gy;
			B3[i] = Gz;
			gradN[i] = vec3d(Gx,Gy,Gz);
		}
		
		// next we get the determinant
		double J = ept.J;
		
		// get the fluid flux and pressure gradient
		vec3d w = pt.m_w;
		vec3d gradp = pt.m_gradp;
		
		// get the effective concentration, its gradient and its time derivative
		double c = pt.m_c;
		vec3d gradc = pt.m_gradc;
		
		// evaluate the permeability and its derivatives
		mat3ds K = pm->m_pPerm->Permeability(mp);
		tens4ds dKdE = pm->m_pPerm->Tangent_Permeability_Strain(mp);
		mat3ds dKdc = pm->m_pPerm->Tangent_Permeability_Concentration(mp); 
		
		// evaluate the porosity and its derivative
		double phiw = pm->Porosity(mp);
		double phis = 1. - phiw;
		double dpdJ = phis/J;
		
		// evaluate the solubility and its derivatives
		double kappa = pm->m_pSolub->Solubility(mp);
		double dkdJ = pm->m_pSolub->Tangent_Solubility_Strain(mp);
		double dkdc = pm->m_pSolub->Tangent_Solubility_Concentration(mp);
		
		// evaluate the diffusivity tensor and its derivatives
		mat3ds D = pm->m_pDiff->Diffusivity(mp);
		mat3ds dDdc = pm->m_pDiff->Tangent_Diffusivity_Concentration(mp);
		tens4ds dDdE = pm->m_pDiff->Tangent_Diffusivity_Strain(mp);
		
		// evaluate the solute free diffusivity
		double D0 = pm->m_pDiff->Free_Diffusivity(mp);
		double dD0dc = 0;	// TODO: temporary, until I implement it in FESoluteDiffusivity
		
		// evaluate the osmotic coefficient and its derivatives
		double osmc = pm->m_pOsmC->OsmoticCoefficient(mp);
		double dodc = pm->m_pOsmC->Tangent_OsmoticCoefficient_Concentration(mp);
		
		// evaluate the stress tangent with concentration
		mat3ds dTdc = pm->m_pSolid->Tangent_Concentration(mp);
		
		// Miscellaneous constants
		mat3dd I(1);
		double R = pm->m_Rgas;
		double T = pm->m_Tabs;
		
		// evaluate the effective permeability and its derivatives
		mat3ds Ki = K.inverse();
		mat3ds ImD = I-D/D0;
		mat3ds Ke = (Ki + ImD*(R*T*kappa*c/phiw/D0)).inverse();
		tens4ds G = dyad1s(Ki,I) - dyad4s(Ki,I)*2 - ddots(dyad2s(Ki),dKdE)*0.5
		+dyad1s(ImD,I)*(R*T*c*J/D0/2/phiw*(dkdJ-kappa/phiw*dpdJ))
		+(dyad1s(I) - dyad4s(I)*2 - dDdE/D0)*(R*T*kappa*c/phiw/D0);
		tens4ds dKedE = dyad1s(Ke,I) - 2*dyad4s(Ke,I) - ddots(dyad2s(Ke),G)*0.5;
		mat3ds Gc = -Ki*dKdc*Ki + ImD*(R*T/phiw/D0*(dkdc*c+kappa-kappa*c/D0*dD0dc))
		+R*T*kappa*c/phiw/D0/D0*(D*dD0dc/D0 - dDdc);
		mat3ds dKedc = -Ke*Gc*Ke;
		
		// calculate all the matrices
		vec3d vtmp,gp,gc,wc,jc;
		mat3d wu,ju;
		tmp = detJ*gw[n];
		for (i=0; i<neln; ++i)
		{
			for (j=0; j<neln; ++j)
			{
				// calculate the kpu matrix
				gp = gradp+(D*gradc)*R*T*kappa/D0;
				wu = vdotTdotv(-gp, dKedE, gradN[j])
				-(((Ke*(D*gradc)) & gradN[j])*(J*dkdJ - kappa)
				  +Ke*(2*kappa*(gradN[j]*(D*gradc))))*R*T/D0
				- Ke*vdotTdotv(gradc, dDdE, gradN[j])*(kappa*R*T/D0);
				vtmp = (wu.transpose()*gradN[i])*(tmp*dt);
				ke[5*i+3][5*j  ] += vtmp.x;
				ke[5*i+3][5*j+1] += vtmp.y;
				ke[5*i+3][5*j+2] += vtmp.z;
				
				// calculate the kcu matrix
				gc = -gradc*phiw + w*c/D0;
				ju = ((D*gc) & gradN[j])*(J*dkdJ) 
				+ vdotTdotv(gc, dDdE, gradN[j])*kappa
				+ (((D*gradc) & gradN[j])*(-phis)
				   +(D*((gradN[j]*w)*2) - ((D*w) & gradN[j]))*c/D0
				   )*kappa
				+D*wu*(kappa*c/D0);
				vtmp = (ju.transpose()*gradN[i])*(tmp*dt);
				ke[5*i+4][5*j  ] += vtmp.x;
				ke[5*i+4][5*j+1] += vtmp.y;
				ke[5*i+4][5*j+2] += vtmp.z;
				
				// calculate the kup matrix
				vtmp = -gradN[i]*H[j]*tmp;
				ke[5*i  ][5*j+3] += vtmp.x;
				ke[5*i+1][5*j+3] += vtmp.y;
				ke[5*i+2][5*j+3] += vtmp.z;
				
				// calculate the kpp matrix
				ke[5*i+3][5*j+3] -= gradN[i]*(Ke*gradN[j])*(tmp*dt);
				
				// calculate the kcp matrix
				ke[5*i+4][5*j+3] -= (gradN[i]*((D*Ke)*gradN[j]))*(kappa*c/D0)*(tmp*dt);
				
				// calculate the kuc matrix
				vtmp = (dTdc*gradN[i] - gradN[i]*(R*T*(dodc*kappa*c+osmc*dkdc*c+osmc*kappa)))*H[j]*tmp;
				ke[5*i  ][5*j+4] += vtmp.x;
				ke[5*i+1][5*j+4] += vtmp.y;
				ke[5*i+2][5*j+4] += vtmp.z;
				
				// calculate the kpc matrix
				wc = (dKedc*gp)*(-H[j])
				-Ke*((((D*(dkdc-kappa*dD0dc/D0)+dDdc*(kappa/D0))*gradc)*H[j]
					  +(D*gradN[j])*kappa)*(R*T/D0));
				ke[5*i+3][5*j+4] += (gradN[i]*wc)*(tmp*dt);
				
				// calculate the kcc matrix
				jc = ((D*dkdc+dDdc*kappa)*gc)*H[j]
				+D*((-gradN[j]*phiw+wc*(c/D0))*kappa);
				ke[5*i+4][5*j+4] += (gradN[i]*jc)*(tmp*dt);
				
			}
		}
	}
	
	// Enforce symmetry by averaging top-right and bottom-left corners of stiffness matrix
	if (bsymm) {
		for (i=0; i<5*neln; ++i)
			for (j=i+1; j<5*neln; ++j) {
				tmp = 0.5*(ke[i][j]+ke[j][i]);
				ke[i][j] = ke[j][i] = tmp;
			}
	}
	
	return true;
}

//-----------------------------------------------------------------------------
//! This function calculates the element stiffness matrix. It calls the material
//! stiffness function, the geometrical stiffness function and, if necessary, the
//! dilatational stiffness function. Note that these three functions only calculate
//! the upper diagonal matrix due to the symmetry of the element stiffness matrix
//! The last section of this function fills the rest of the element stiffness matrix.

void FEBiphasicSoluteDomain::SolidElementStiffness(FEM& fem, FESolidElement& el, matrix& ke)
{
	// calculate material stiffness (i.e. constitutive component)
	BiphasicSoluteMaterialStiffness(fem, el, ke);
	
	// calculate geometrical stiffness
	GeometricalStiffness(el, ke);
	
	// assign symmetic parts
	// TODO: Can this be omitted by changing the Assemble routine so that it only
	// grabs elements from the upper diagonal matrix?
	int ndof = 3*el.Nodes();
	int i, j;
	for (i=0; i<ndof; ++i)
		for (j=i+1; j<ndof; ++j)
			ke[j][i] = ke[i][j];
}

//-----------------------------------------------------------------------------
//! Calculates element material stiffness element matrix

void FEBiphasicSoluteDomain::BiphasicSoluteMaterialStiffness(FEM& fem, FESolidElement &el, matrix &ke)
{
	assert(fem.m_pStep->m_nModule == FE_POROSOLUTE);
	
	int i, i3, j, j3, n;
	
	// Get the current element's data
	const int nint = el.GaussPoints();
	const int neln = el.Nodes();
	const int ndof = 3*neln;
	
	// global derivatives of shape functions
	// NOTE: hard-coding of hex elements!
	// Gx = dH/dx
	double Gx[8], Gy[8], Gz[8];
	
	double Gxi, Gyi, Gzi;
	double Gxj, Gyj, Gzj;
	
	// The 'D' matrix
	double D[6][6] = {0};	// The 'D' matrix
	
	// The 'D*BL' matrix
	double DBL[6][3];
	
	double *Grn, *Gsn, *Gtn;
	double Gr, Gs, Gt;
	
	// jacobian
	double Ji[3][3], detJt;

	// nodal concentrations
	double ct[8];
	for (i=0; i<neln; ++i) ct[i] = m_pMesh->Node(el.m_node[i]).m_ct;

	// weights at gauss points
	const double *gw = el.GaussWeights();
	
	// see if this is a biphasic-solute material
	FEBiphasicSolute* pmat = dynamic_cast<FEBiphasicSolute*>(fem.GetMaterial(el.GetMatID()));
	assert(pmat);
	
	// calculate element stiffness matrix
	for (n=0; n<nint; ++n)
	{
		// calculate jacobian
		detJt = invjact(el, Ji, n)*gw[n];
		
		Grn = el.Gr(n);
		Gsn = el.Gs(n);
		Gtn = el.Gt(n);
		
		// setup the material point
		// NOTE: deformation gradient and determinant have already been evaluated in the stress routine
		FEMaterialPoint& mp = *el.m_State[n];
		FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
		
		// evaluate concentration at gauss-point
		FESolutePoroElasticMaterialPoint& ppt = *(mp.ExtractData<FESolutePoroElasticMaterialPoint>());
		ppt.m_c = el.Evaluate(ct, n);
		
		// get the 'D' matrix
		tens4ds C = pmat->Tangent(mp);
		C.extract(D);
		
		for (i=0; i<neln; ++i)
		{
			Gr = Grn[i];
			Gs = Gsn[i];
			Gt = Gtn[i];
			
			// calculate global gradient of shape functions
			// note that we need the transposed of Ji, not Ji itself !
			Gx[i] = Ji[0][0]*Gr+Ji[1][0]*Gs+Ji[2][0]*Gt;
			Gy[i] = Ji[0][1]*Gr+Ji[1][1]*Gs+Ji[2][1]*Gt;
			Gz[i] = Ji[0][2]*Gr+Ji[1][2]*Gs+Ji[2][2]*Gt;
		}
		
		// we only calculate the upper triangular part
		// since ke is symmetric. The other part is
		// determined below using this symmetry.
		for (i=0, i3=0; i<neln; ++i, i3 += 3)
		{
			Gxi = Gx[i];
			Gyi = Gy[i];
			Gzi = Gz[i];
			
			for (j=i, j3 = i3; j<neln; ++j, j3 += 3)
			{
				Gxj = Gx[j];
				Gyj = Gy[j];
				Gzj = Gz[j];
				
				// calculate D*BL matrices
				DBL[0][0] = (D[0][0]*Gxj+D[0][3]*Gyj+D[0][5]*Gzj);
				DBL[0][1] = (D[0][1]*Gyj+D[0][3]*Gxj+D[0][4]*Gzj);
				DBL[0][2] = (D[0][2]*Gzj+D[0][4]*Gyj+D[0][5]*Gxj);
				
				DBL[1][0] = (D[1][0]*Gxj+D[1][3]*Gyj+D[1][5]*Gzj);
				DBL[1][1] = (D[1][1]*Gyj+D[1][3]*Gxj+D[1][4]*Gzj);
				DBL[1][2] = (D[1][2]*Gzj+D[1][4]*Gyj+D[1][5]*Gxj);
				
				DBL[2][0] = (D[2][0]*Gxj+D[2][3]*Gyj+D[2][5]*Gzj);
				DBL[2][1] = (D[2][1]*Gyj+D[2][3]*Gxj+D[2][4]*Gzj);
				DBL[2][2] = (D[2][2]*Gzj+D[2][4]*Gyj+D[2][5]*Gxj);
				
				DBL[3][0] = (D[3][0]*Gxj+D[3][3]*Gyj+D[3][5]*Gzj);
				DBL[3][1] = (D[3][1]*Gyj+D[3][3]*Gxj+D[3][4]*Gzj);
				DBL[3][2] = (D[3][2]*Gzj+D[3][4]*Gyj+D[3][5]*Gxj);
				
				DBL[4][0] = (D[4][0]*Gxj+D[4][3]*Gyj+D[4][5]*Gzj);
				DBL[4][1] = (D[4][1]*Gyj+D[4][3]*Gxj+D[4][4]*Gzj);
				DBL[4][2] = (D[4][2]*Gzj+D[4][4]*Gyj+D[4][5]*Gxj);
				
				DBL[5][0] = (D[5][0]*Gxj+D[5][3]*Gyj+D[5][5]*Gzj);
				DBL[5][1] = (D[5][1]*Gyj+D[5][3]*Gxj+D[5][4]*Gzj);
				DBL[5][2] = (D[5][2]*Gzj+D[5][4]*Gyj+D[5][5]*Gxj);
				
				ke[i3  ][j3  ] += (Gxi*DBL[0][0] + Gyi*DBL[3][0] + Gzi*DBL[5][0] )*detJt;
				ke[i3  ][j3+1] += (Gxi*DBL[0][1] + Gyi*DBL[3][1] + Gzi*DBL[5][1] )*detJt;
				ke[i3  ][j3+2] += (Gxi*DBL[0][2] + Gyi*DBL[3][2] + Gzi*DBL[5][2] )*detJt;
				
				ke[i3+1][j3  ] += (Gyi*DBL[1][0] + Gxi*DBL[3][0] + Gzi*DBL[4][0] )*detJt;
				ke[i3+1][j3+1] += (Gyi*DBL[1][1] + Gxi*DBL[3][1] + Gzi*DBL[4][1] )*detJt;
				ke[i3+1][j3+2] += (Gyi*DBL[1][2] + Gxi*DBL[3][2] + Gzi*DBL[4][2] )*detJt;
				
				ke[i3+2][j3  ] += (Gzi*DBL[2][0] + Gyi*DBL[4][0] + Gxi*DBL[5][0] )*detJt;
				ke[i3+2][j3+1] += (Gzi*DBL[2][1] + Gyi*DBL[4][1] + Gxi*DBL[5][1] )*detJt;
				ke[i3+2][j3+2] += (Gzi*DBL[2][2] + Gyi*DBL[4][2] + Gxi*DBL[5][2] )*detJt;
			}
		}
	}
}


//-----------------------------------------------------------------------------
void FEBiphasicSoluteDomain::UpdateStresses(FEModel &fem)
{
	int i, n;
	int nint, neln;
	double* gw;
	vec3d r0[8];
	vec3d rt[8];
	double pn[8], ct[8];

	FEMesh& mesh = *m_pMesh;
	
	for (i=0; i<(int) m_Elem.size(); ++i)
	{
		// get the solid element
		FESolidElement& el = m_Elem[i];
		
		assert(!el.IsRigid());
		
		assert(el.Type() != FE_UDGHEX);
		
		// get the number of integration points
		nint = el.GaussPoints();

		// get the number of nodes
		neln = el.Nodes();
		assert(neln <= 8);
		
		// get the integration weights
		gw = el.GaussWeights();

		// get the nodal data
		for (int j=0; j<neln; ++j)
		{
			r0[j] = mesh.Node(el.m_node[j]).m_r0;
			rt[j] = mesh.Node(el.m_node[j]).m_rt;
			pn[j] = mesh.Node(el.m_node[j]).m_pt;
			ct[j] = mesh.Node(el.m_node[j]).m_ct;
		}
		
		// get the material
		FEMaterial* pm = dynamic_cast<FEMaterial*>(fem.GetMaterial(el.GetMatID()));
		
		assert(dynamic_cast<FEBiphasicSolute*>(pm) != 0);
		
		// get the biphasic-solute material
		FEBiphasicSolute* pmb = dynamic_cast<FEBiphasicSolute*>(pm);
		
		// extract the elastic component
		FEElasticMaterial* pme = pmb->m_pSolid;
		
		// loop over the integration points and calculate
		// the stress at the integration point
		for (n=0; n<nint; ++n)
		{
			FEMaterialPoint& mp = *el.m_State[n];
			FEElasticMaterialPoint& pt = *(mp.ExtractData<FEElasticMaterialPoint>());
			
			// material point coordinates
			// TODO: I'm not entirly happy with this solution
			//		 since the material point coordinates are used by most materials.
			pt.r0 = el.Evaluate(r0, n);
			pt.rt = el.Evaluate(rt, n);
			
			// get the deformation gradient and determinant
			pt.J = defgrad(el, pt.F, n);
			
			// solute-poroelastic data
			FESolutePoroElasticMaterialPoint& ppt = *(mp.ExtractData<FESolutePoroElasticMaterialPoint>());
			
			// evaluate fluid pressure at gauss-point
			ppt.m_p = el.Evaluate(pn, n);
			
			// calculate the gradient of p at gauss-point
			ppt.m_gradp = gradient(el, pn, n);
			
			// evaluate effective solute concentration at gauss-point
			ppt.m_c = el.Evaluate(ct, n);
			
			// calculate the gradient of c at gauss-point
			ppt.m_gradc = gradient(el, ct, n);
			
			// for biphasic-solute materials also update the fluid and solute fluxes
			// and evaluate the actual fluid pressure and solute concentration
			ppt.m_w = pmb->FluidFlux(mp);
			ppt.m_pa = pmb->Pressure(mp);
			ppt.m_j = pmb->SoluteFlux(mp);
			ppt.m_ca = pmb->Concentration(mp);

			// calculate the stress at this material point (must be done after evaluating m_pa)
			pt.s = pmb->Stress(mp);
		}
	}
}
