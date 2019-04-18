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

#pragma once
#include "cmdoptions.h"

class FEBioModel;

class FEBioApp
{
public:
	FEBioApp();

	bool Init(int nargs, char* argv[]);

	int Run();

	void Finish();

	void ProcessCommands();

	CMDOPTIONS& CommandOptions();

	bool ParseCmdLine(int argc, char* argv[]);

	// run an febio model
	int RunModel();

public:
	// get the current model
	FEBioModel* GetCurrentModel();

protected:
	// show FEBio prompt
	int prompt();

	// set the currently active model
	void SetCurrentModel(FEBioModel* fem);

public:
	static FEBioApp* GetInstance();

private:
	CMDOPTIONS	m_ops;				// command line options

	FEBioModel*		m_fem;			// current model (or null if not model is running)

	static FEBioApp*	m_This;
};