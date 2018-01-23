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

#include "ShapeGenerator.h"
#include "ShapeData.h"
#include "PrimitivePartition.h"
#include "PrimitiveClassifier.h"
#include "AttributeConversion.h"
#include "LogHandler.h"
#include "MultiWatch.h"

#include "GA/GA_Primitive.h"
#include "GU/GU_Detail.h"

#include <unordered_map>


namespace {

constexpr bool DBG = true;

const std::set<UT_StringHolder> ATTRIBUTE_BLACKLIST = { PLD_PRIM_CLS_NAME, PLD_RPK, PLD_RULE_FILE,
                                                        PLD_START_RULE, PLD_STYLE, PLD_RANDOM_SEED };

} // namespace


void ShapeGenerator::get(const GU_Detail* detail, const PrimitiveClassifier& primCls,
                         ShapeData& shapeData, const PRTContextUPtr& prtCtx)
{
	WA("all");

	// extract initial shape geometry
	ShapeConverter::get(detail, primCls, shapeData, prtCtx);

	// collect all primitive attributes
	std::unordered_map<UT_StringHolder, GA_ROAttributeRef> attributes;
	{
		GA_Attribute* a;
		GA_FOR_ALL_PRIMITIVE_ATTRIBUTES(detail, a) {
			const UT_StringHolder& n = a->getName();

			// do not add the main attributes as normal initial shape attributes
			// else they end up as primitive attributes on the generated geometry
			if (ATTRIBUTE_BLACKLIST.count(n) > 0)
				continue;

			attributes.emplace(n, GA_ROAttributeRef(a));
		}

		// also filter out the actual primitive classifier attribute
		std::set<UT_StringHolder> removeMe;
		const GA_Primitive* p;
		GA_FOR_ALL_PRIMITIVES(detail, p) {
			PrimitiveClassifier pc;
			primCls.updateFromPrimitive(pc, detail, p);
			if (removeMe.emplace(pc.name).second) {
				auto aIt = attributes.find(pc.name);
				if (aIt != attributes.end()) {
					attributes.erase(aIt);
				}
			}
		}
	}

	// loop over all initial shapes and use the first primitive to get the attribute values
	for (size_t isIdx = 0; isIdx < shapeData.getInitialShapeBuilders().size(); isIdx++) {
		const auto& pv = shapeData.getPrimitiveMapping(isIdx);
		if (pv.empty())
			continue;

		const auto& firstPrimitive = pv.front();
		const auto& primitiveMapOffset = firstPrimitive->getMapOffset();

		if (DBG) LOG_DBG << "   -- creating initial shape " << isIdx << ", prim count = " << pv.size();

		// extract main attrs from first prim in initial shape prim group
		if (!getMainAttributes(detail, firstPrimitive))
			continue;

		const ResolveMapUPtr& assetsMap = prtCtx->getResolveMap(mRPK);
		if (!assetsMap)
			continue;

		// extract primitive attributes
		AttributeMapBuilderUPtr amb(prt::AttributeMapBuilder::create());
		for (const auto& attr: attributes) {
			const GA_ROAttributeRef& ar = attr.second;

			if (ar.isInvalid())
				continue;

			const std::wstring ruleAttrName = NameConversion::toRuleAttr(mStyle, attr.first);

			switch (ar.getStorageClass()) {
				case GA_STORECLASS_FLOAT: {
					GA_ROHandleD av(ar);
					if (av.isValid()) {
						double v = av.get(primitiveMapOffset);
						if (DBG) LOG_DBG << "   prim float attr: " << ar->getName() << " = " << v;
						amb->setFloat(ruleAttrName.c_str(), v);
					}
					break;
				}
				case GA_STORECLASS_STRING: {
					GA_ROHandleS av(ar);
					if (av.isValid()) {
						const char* v = av.get(primitiveMapOffset);
						const std::wstring wv = toUTF16FromOSNarrow(v);
						if (DBG) LOG_DBG << "   prim string attr: " << ar->getName() << " = " << v;
						amb->setString(ruleAttrName.c_str(), wv.c_str());
					}
					break;
				}
				case GA_STORECLASS_INT: {
					GA_ROHandleI av(ar);
					if (av.isValid()) {
						const int v = av.get(primitiveMapOffset);
						const bool bv = (v > 0);
						if (DBG) LOG_DBG << "   prim bool attr: " << ar->getName() << " = " << v;
						amb->setBool(ruleAttrName.c_str(), bv);
					}
					break;
				}
				default: {
					LOG_WRN << "prim attr " << ar->getName() << ": unsupported storage class";
					break;
				}
			} // switch key type
		} // for each primitive attribute

		AttributeMapUPtr ruleAttr(amb->createAttributeMap());

		auto& isb = shapeData.getInitialShapeBuilder(isIdx);
		const int32_t randomSeed = shapeData.getInitialShapeRandomSeed(isIdx);
		const auto& shapeName = shapeData.getInitialShapeName(isIdx);
		const auto fqStartRule = getFullyQualifiedStartRule();

		isb->setAttributes(
				mRuleFile.c_str(),
				fqStartRule.c_str(),
				randomSeed,
				shapeName.c_str(),
				ruleAttr.get(),
				assetsMap.get()
		);

		prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
		const prt::InitialShape* initialShape = isb->createInitialShapeAndReset(&status);
		if (status == prt::STATUS_OK && initialShape != nullptr) {
			if (DBG) LOG_DBG << objectToXML(initialShape);
			shapeData.addShape(initialShape, std::move(amb), std::move(ruleAttr));
		}
		else
			LOG_WRN << "failed to create initial shape " << shapeName << ": " << prt::getStatusDescription(status);

	} // for each partition
}
