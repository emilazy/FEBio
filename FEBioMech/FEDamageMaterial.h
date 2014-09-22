//
//  FEDamageMaterial.h
//  FEBioMech
//
//  Created by Gerard Ateshian on 9/18/14.
//  Copyright (c) 2014 febio.org. All rights reserved.
//

#ifndef __FEBioMech__FEDamageMaterial__
#define __FEBioMech__FEDamageMaterial__

#include "FEElasticMaterial.h"
#include "FEDamageMaterialPoint.h"

class FEDamageCriterion;
class FEDamageCDF;

//-----------------------------------------------------------------------------
// This material models damage in any hyper-elastic materials.

class FEDamageMaterial : public FEElasticMaterial
{
public:
	FEDamageMaterial(FEModel* pfem);
    
public:
	//! calculate stress at material point
	mat3ds Stress(FEMaterialPoint& pt);
    
	//! calculate tangent stiffness at material point
	tens4ds Tangent(FEMaterialPoint& pt);
    
	//! calculate strain energy density at material point
	double StrainEnergyDensity(FEMaterialPoint& pt);
    
    //! damage
    double Damage(FEMaterialPoint& pt);
    
	//! data initialization and checking
	void Init();
    
	// returns a pointer to a new material point object
	FEMaterialPoint* CreateMaterialPointData()
	{
		return new FEDamageMaterialPoint(new FEElasticMaterialPoint);
	}
    
public:
	// get a material parameter
	FEParam* GetParameter(const ParamString& s);
    
	//! get the number of material properties
	int Properties();
    
	//! get a specific material property
	FECoreBase* GetProperty(int i);
    
	//! find a material property index ( returns <0 for error)
	int FindPropertyIndex(const char* szname);
    
	//! set a material property (returns false on error)
	bool SetProperty(int i, FECoreBase* pm);
    
	//! Set the local coordinate system for a material point (overridden from FEMaterial)
	void SetLocalCoordinateSystem(FEElement& el, int n, FEMaterialPoint& mp);
    
public:
    FEElasticMaterial*  m_pBase;    // base elastic material
    FEDamageCDF*        m_pDamg;    // damage model
    FEDamageCriterion*  m_pCrit;    // damage criterion
};

#endif /* defined(__FEBioMech__FEDamageMaterial__) */
