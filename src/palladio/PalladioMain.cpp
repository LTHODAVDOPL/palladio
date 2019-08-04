/*
 * Copyright 2014-2019 Esri R&D Zurich and VRBN
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PalladioMain.h"

#include "PRTContext.h"
#include "SOPAssign.h"
#include "SOPGenerate.h"
#include "RulePackageFS.h"
#include "NodeParameter.h"

#include "OP/OP_OperatorTable.h"
#include "UT/UT_Exit.h"

#include "UT/UT_DSOVersion.h"


namespace {

// prt lifecycle
PRTContextUPtr prtCtx;

std::unique_ptr<RulePackageReader> rpkReader;
std::unique_ptr<RulePackageInfoHelper> rpkInfoHelper;

} // namespace


void newSopOperator(OP_OperatorTable *table) {
	if (!prtCtx) {
		prtCtx.reset(new PRTContext());
		UT_Exit::addExitCallback([](void *) { prtCtx.reset(); });
	}

	if (!prtCtx->isAlive())
		return;

	// instantiate assign sop
	auto createSOPAssign = [](OP_Network *net, const char *name, OP_Operator *op) -> OP_Node* {
		return new SOPAssign(prtCtx, net, name, op);
	};
	table->addOperator(new OP_Operator(OP_PLD_ASSIGN, OP_PLD_ASSIGN, createSOPAssign,
			AssignNodeParams::PARAM_TEMPLATES, 1, 1, nullptr, OP_FLAG_GENERATOR
	));

	// instantiate generator sop
	auto createSOPGenerate = [](OP_Network *net, const char *name, OP_Operator *op) -> OP_Node* {
		return new SOPGenerate(prtCtx, net, name, op);
	};
	table->addOperator(new OP_Operator(OP_PLD_GENERATE, OP_PLD_GENERATE, createSOPGenerate,
	       GenerateNodeParams::PARAM_TEMPLATES, 1, 1, nullptr, OP_FLAG_GENERATOR
	));
}

void installFSHelpers() {
	rpkReader = std::make_unique<RulePackageReader>();
    rpkInfoHelper = std::make_unique<RulePackageInfoHelper>();
}
