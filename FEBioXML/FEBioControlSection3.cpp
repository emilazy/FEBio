#include "stdafx.h"
#include "FEBioControlSection.h"
#include "FECore/FEAnalysis.h"
#include "FECore/FEModel.h"
#include "FECore/FECoreKernel.h"

//-----------------------------------------------------------------------------
void FEBioControlSection3::Parse(XMLTag& tag)
{
	FEModel& fem = *GetFEModel();
	FEAnalysis* pstep = GetBuilder()->GetStep();
	if (pstep == 0)
	{
		throw XMLReader::InvalidTag(tag);
	}

	// Get the solver
	FESolver* psolver = pstep->GetFESolver();
	if (psolver == 0) 
	{
		string m = GetBuilder()->GetModuleName();
		throw FEBioImport::FailedAllocatingSolver(m.c_str());
	}

	++tag;
	do
	{
		// first parse common control parameters
		if (ParseCommonParams(tag) == false)
		{
			if (tag == "solver")
			{
				ReadParameterList(tag, psolver);
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// Parse control parameters common to all solvers/modules
bool FEBioControlSection3::ParseCommonParams(XMLTag& tag)
{
	FEModelBuilder* feb = GetBuilder();

	FEBioImport* imp = GetFEBioImport();

	FEModel& fem = *GetFEModel();
	FEAnalysis* pstep = GetBuilder()->GetStep();

	FEParameterList& modelParams = fem.GetParameterList();
	FEParameterList& stepParams = pstep->GetParameterList();

	if (ReadParameter(tag, modelParams) == false)
	{
		if (ReadParameter(tag, stepParams) == false)
		{
			if (tag == "analysis")
			{
				XMLAtt& att = tag.Attribute("type");
				if      (att == "static"      ) pstep->m_nanalysis = FE_STATIC;
				else if (att == "dynamic"     ) pstep->m_nanalysis = FE_DYNAMIC;
				else if (att == "steady-state") pstep->m_nanalysis = FE_STEADY_STATE;
				else if (att == "transient"   ) pstep->m_nanalysis = FE_DYNAMIC;
				else throw XMLReader::InvalidAttributeValue(tag, "type", att.cvalue());
			}
			else if (tag == "restart" )
			{
				const char* szf = tag.AttributeValue("file", true);
				if (szf) imp->SetDumpfileName(szf);
				char szval[256];
				tag.value(szval);
				if		(strcmp(szval, "DUMP_DEFAULT"    ) == 0) {} // don't change the restart level
				else if (strcmp(szval, "DUMP_NEVER"      ) == 0) pstep->SetDumpLevel(FE_DUMP_NEVER);
				else if (strcmp(szval, "DUMP_MAJOR_ITRS" ) == 0) pstep->SetDumpLevel(FE_DUMP_MAJOR_ITRS);
				else if (strcmp(szval, "DUMP_STEP"       ) == 0) pstep->SetDumpLevel(FE_DUMP_STEP);
				else if (strcmp(szval, "0" ) == 0) pstep->SetDumpLevel(FE_DUMP_NEVER);		// for backward compatibility only
				else if (strcmp(szval, "1" ) == 0) pstep->SetDumpLevel(FE_DUMP_MAJOR_ITRS); // for backward compatibility only
				else throw XMLReader::InvalidValue(tag);
			}
			else if (tag == "time_stepper")
			{
				pstep->m_bautostep = true;
				FETimeStepController& tc = pstep->m_timeController;
				FEParameterList& pl = tc.GetParameterList();
				ReadParameterList(tag, pl);
			}
			else if (tag == "use_three_field_hex") tag.value(feb->m_b3field_hex);
			else if (tag == "use_three_field_tet") tag.value(feb->m_b3field_tet);
			else if (tag == "use_three_field_shell") tag.value(feb->m_b3field_shell);
            else if (tag == "use_three_field_quad") tag.value(feb->m_b3field_quad);
            else if (tag == "use_three_field_tri") tag.value(feb->m_b3field_tri);
			else if (tag == "shell_formulation")
			{
				FEMesh& mesh = GetFEModel()->GetMesh();
				int nshell = 0;
				tag.value(nshell);
				switch (nshell)
				{
				case 0: feb->m_default_shell = OLD_SHELL; break;
				case 1: feb->m_default_shell = NEW_SHELL; break;
				case 2: feb->m_default_shell = EAS_SHELL; break;
				case 3: feb->m_default_shell = ANS_SHELL; break;
				default:
					throw XMLReader::InvalidValue(tag);
				}
			}
			else if (tag == "integration") ParseIntegrationRules(tag);
			else return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
void FEBioControlSection3::ParseIntegrationRules(XMLTag& tag)
{
	FEModelBuilder* feb = GetBuilder();
	FEModel& fem = *GetFEModel();

	++tag;
	do
	{
		if (tag == "rule")
		{
			XMLAtt& elem = tag.Attribute("elem");
			const char* szv = get_value_string(tag);

			if (elem == "hex8")
			{
				if      (strcmp(szv, "GAUSS8") == 0) feb->m_nhex8 = FE_HEX8G8;
				else if (strcmp(szv, "POINT6") == 0) feb->m_nhex8 = FE_HEX8RI;
				else if (strcmp(szv, "UDG"   ) == 0) feb->m_nhex8 = FE_HEX8G1;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tet10")
			{
				if      (strcmp(szv, "GAUSS1"   ) == 0) feb->m_ntet10 = FE_TET10G1;
				else if (strcmp(szv, "GAUSS4"   ) == 0) feb->m_ntet10 = FE_TET10G4;
				else if (strcmp(szv, "GAUSS8"   ) == 0) feb->m_ntet10 = FE_TET10G8;
				else if (strcmp(szv, "LOBATTO11") == 0) feb->m_ntet10 = FE_TET10GL11;
				else if (strcmp(szv, "GAUSS4RI1") == 0) feb->m_ntet10 = FE_TET10G4RI1;
				else if (strcmp(szv, "GAUSS8RI4") == 0) feb->m_ntet10 = FE_TET10G8RI4;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tet15")
			{
				if      (strcmp(szv, "GAUSS8"    ) == 0) feb->m_ntet15 = FE_TET15G8;
				else if (strcmp(szv, "GAUSS11"   ) == 0) feb->m_ntet15 = FE_TET15G11;
				else if (strcmp(szv, "GAUSS15"   ) == 0) feb->m_ntet15 = FE_TET15G15;
				else if (strcmp(szv, "GAUSS15RI4") == 0) feb->m_ntet10 = FE_TET15G15RI4;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tet20")
			{
				if (strcmp(szv, "GAUSS15") == 0) feb->m_ntet20 = FE_TET20G15;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tri3")
			{
				if      (strcmp(szv, "GAUSS1") == 0) feb->m_ntri3 = FE_TRI3G1;
				else if (strcmp(szv, "GAUSS3") == 0) feb->m_ntri3 = FE_TRI3G3;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tri6")
			{
				if      (strcmp(szv, "GAUSS3"    ) == 0) feb->m_ntri6 = FE_TRI6G3;
				else if (strcmp(szv, "GAUSS6"    ) == 0) feb->m_ntri6 = FE_TRI6NI;
				else if (strcmp(szv, "GAUSS4"    ) == 0) feb->m_ntri6 = FE_TRI6G4;
				else if (strcmp(szv, "GAUSS7"    ) == 0) feb->m_ntri6 = FE_TRI6G7;
				else if (strcmp(szv, "LOBATTO7"  ) == 0) feb->m_ntri6 = FE_TRI6GL7;
				else if (strcmp(szv, "MOD_GAUSS7") == 0) feb->m_ntri6 = FE_TRI6MG7;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tri7")
			{
				if      (strcmp(szv, "GAUSS3"  ) == 0) feb->m_ntri7 = FE_TRI7G3;
				else if (strcmp(szv, "GAUSS4"  ) == 0) feb->m_ntri7 = FE_TRI7G4;
				else if (strcmp(szv, "GAUSS7"  ) == 0) feb->m_ntri7 = FE_TRI7G7;
				else if (strcmp(szv, "LOBATTO7") == 0) feb->m_ntri7 = FE_TRI7GL7;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tri10")
			{
				if      (strcmp(szv, "GAUSS7" ) == 0) feb->m_ntri10 = FE_TRI10G7;
				else if (strcmp(szv, "GAUSS12") == 0) feb->m_ntri10 = FE_TRI10G12;
				else throw XMLReader::InvalidValue(tag);
			}
			else if (elem == "tet4")
			{
				if (tag.isleaf())
				{
					if      (strcmp(szv, "GAUSS4") == 0) feb->m_ntet4 = FE_TET4G4;
					else if (strcmp(szv, "GAUSS1") == 0) feb->m_ntet4 = FE_TET4G1;
					else if (strcmp(szv, "UT4"   ) == 0) feb->m_but4 = true;
					else throw XMLReader::InvalidValue(tag);
				}
				else
				{
					const char* szt = tag.AttributeValue("type");
					if      (strcmp(szt, "GAUSS4") == 0) feb->m_ntet4 = FE_TET4G4;
					else if (strcmp(szt, "GAUSS1") == 0) feb->m_ntet4 = FE_TET4G1;
					else if (strcmp(szt, "UT4"   ) == 0) feb->m_but4 = true;
					else throw XMLReader::InvalidAttributeValue(tag, "type", szv);

					++tag;
					do
					{
						if      (tag == "alpha"   ) tag.value(fem.m_ut4_alpha);
						else if (tag == "iso_stab") tag.value(fem.m_ut4_bdev);
						else if (tag == "stab_int")
						{
							const char* sz = tag.szvalue();
							if      (strcmp(sz, "GAUSS4") == 0) feb->m_ntet4 = FE_TET4G4;
							else if (strcmp(sz, "GAUSS1") == 0) feb->m_ntet4 = FE_TET4G1;
						}
						else throw XMLReader::InvalidTag(tag);
						++tag;
					}
					while (!tag.isend());
				}
			}
			else throw XMLReader::InvalidAttributeValue(tag, "elem", elem.cvalue());
		}
		else throw XMLReader::InvalidValue(tag);
		++tag;
	}
	while (!tag.isend());
}
