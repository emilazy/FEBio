/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2021 University of Utah, The Trustees of Columbia University in
the City of New York, and others.

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
#include "FESymmetryPlane.h"
#include "FECore/FECoreKernel.h"
//#include <FECore/FEModel.h>

//-----------------------------------------------------------------------------
FESymmetryPlane::FESymmetryPlane(FEModel* pfem) : FELinearConstraintSet(pfem), m_surf(pfem)
{
	m_binit = false;
}

//-----------------------------------------------------------------------------
//! Initializes data structures.
void FESymmetryPlane::Activate()
{
    // don't forget to call base class
    FELinearConstraintSet::Activate();

    // get the dof indices
    int dofX = GetDOFIndex("x");
    int dofY = GetDOFIndex("y");
    int dofZ = GetDOFIndex("z");
    int dofSX = GetDOFIndex("sx");
    int dofSY = GetDOFIndex("sy");
    int dofSZ = GetDOFIndex("sz");
    
    // evaluate the nodal normals
    int N = m_surf.Nodes();
    vec3d nu(0,0,0);
    
    // loop over all elements to get average surface normal
    // (assumes that surface elements are all on same plane)
    for (int i=0; i<m_surf.Elements(); ++i)
    {
        FESurfaceElement& el = m_surf.Element(i);
        nu += m_surf.SurfaceNormal(el, 0);
    }
    nu.unit();
    
    // create linear constraints
    // for a symmetry plane the constraint on (ux, uy, uz) is
    // nx*ux + ny*uy + nz*uz = 0
    for (int i=0; i<N; ++i) {
        FENode node = m_surf.Node(i);
        if ((node.HasFlags(FENode::EXCLUDE) == false) && (node.m_rid == -1)) {
            FEAugLagLinearConstraint* pLC = new FEAugLagLinearConstraint;
            for (int j=0; j<3; ++j) {
                FEAugLagLinearConstraint::DOF dof;
                dof.node = node.GetID() - 1;    // zero-based
                switch (j) {
                    case 0:
                        dof.bc = dofX;
                        dof.val = nu.x;
                        break;
                    case 1:
                        dof.bc = dofY;
                        dof.val = nu.y;
                        break;
                    case 2:
                        dof.bc = dofZ;
                        dof.val = nu.z;
                        break;
                    default:
                        break;
                }
                pLC->m_dof.push_back(dof);
            }
            // add the linear constraint to the system
            add(pLC);
        }
    }
    
    // for nodes that belong to shells, also constraint the shell bottom face displacements
    for (int i=0; i<N; ++i) {
        FENode node = m_surf.Node(i);
        if ((node.HasFlags(FENode::EXCLUDE) == false) && (node.HasFlags(FENode::SHELL)) && (node.m_rid == -1)) {
            FEAugLagLinearConstraint* pLC = new FEAugLagLinearConstraint;
            for (int j=0; j<3; ++j) {
                FEAugLagLinearConstraint::DOF dof;
                dof.node = node.GetID() - 1;    // zero-based
                switch (j) {
                    case 0:
                        dof.bc = dofSX;
                        dof.val = nu.x;
                        break;
                    case 1:
                        dof.bc = dofSY;
                        dof.val = nu.y;
                        break;
                    case 2:
                        dof.bc = dofSZ;
                        dof.val = nu.z;
                        break;
                    default:
                        break;
                }
                pLC->m_dof.push_back(dof);
            }
            // add the linear constraint to the system
            add(pLC);
        }
    }
}

//-----------------------------------------------------------------------------
bool FESymmetryPlane::Init()
{
	// initialize surface
    return m_surf.Init();
}
