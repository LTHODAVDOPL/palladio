/*
 * Copyright 2014-2018 Esri R&D Zurich and VRBN
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

#pragma once

#include "PRTContext.h"
#include "Utils.h"

#include "UT/UT_String.h"

#include "boost/filesystem/path.hpp"

#include <string>


class GU_Detail;
class GA_Primitive;
class OP_Context;
class SOP_Node;
class PrimitiveClassifier;
struct MainAttributeHandles;
class ShapeData;

const UT_String PLD_RPK         = "pldRPK";
const UT_String PLD_RULE_FILE   = "pldRuleFile";
const UT_String PLD_START_RULE  = "pldStartRule";
const UT_String PLD_STYLE       = "pldStyle";
const UT_String PLD_RANDOM_SEED = "pldRandomSeed";

enum class GroupCreation { NONE, PRIMCLS };

class ShapeConverter {
public:
	virtual void get(const GU_Detail* detail,  const PrimitiveClassifier& primCls,
	                 ShapeData& shapeData, const PRTContextUPtr& prtCtx);
	void put(GU_Detail* detail, PrimitiveClassifier& primCls, const ShapeData& shapeData) const;

	void getMainAttributes(SOP_Node* node, const OP_Context& context);
	bool getMainAttributes(const GU_Detail* detail, const GA_Primitive* prim);
	void putMainAttributes(MainAttributeHandles& mah, const GA_Primitive* primitive) const;

	RuleFileInfoUPtr getRuleFileInfo(const ResolveMapUPtr& resolveMap, prt::Cache* prtCache) const;
	std::wstring getFullyQualifiedStartRule() const;

public:
	boost::filesystem::path mRPK;
	std::wstring            mRuleFile;
	std::wstring            mStyle;
	std::wstring            mStartRule;
};

using ShapeConverterUPtr = std::unique_ptr<ShapeConverter>;

