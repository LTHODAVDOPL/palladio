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

#include "ShapeConverter.h"
#include "ShapeData.h"
#include "PrimitiveClassifier.h"
#include "AttributeConversion.h"
#include "LogHandler.h"
#include "MultiWatch.h"

#include "GU/GU_Detail.h"
#include "GEO/GEO_PrimPolySoup.h"
#include "UT/UT_String.h"

#include "BoostRedirect.h"
#include PLD_BOOST_INCLUDE(/variant.hpp)
#include PLD_BOOST_INCLUDE(/algorithm/string.hpp)
#include PLD_BOOST_INCLUDE(/functional/hash.hpp)


namespace {

constexpr bool DBG = false;

template<typename T>
T safeGet(GA_Offset off, const GA_ROAttributeRef& ref) {
	GA_ROHandleS h(ref);
	const char* s = h.get(off);
	return (s != nullptr) ? T{s} : T{};
}

} // namespace


struct MainAttributeHandles {
	GA_RWHandleS rpk;
	GA_RWHandleS ruleFile;
	GA_RWHandleS startRule;
	GA_RWHandleS style;
	GA_RWHandleI seed;

	void setup(GU_Detail* detail) {
		GA_RWAttributeRef rpkRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_RPK, 1));
		rpk.bind(rpkRef);

		GA_RWAttributeRef ruleFileRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_RULE_FILE, 1));
		ruleFile.bind(ruleFileRef);

		GA_RWAttributeRef startRuleRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_START_RULE, 1));
		startRule.bind(startRuleRef);

		GA_RWAttributeRef styleRef(detail->addStringTuple(GA_ATTRIB_PRIMITIVE, PLD_STYLE, 1));
		style.bind(styleRef);

		GA_RWAttributeRef seedRef(detail->addIntTuple(GA_ATTRIB_PRIMITIVE, PLD_RANDOM_SEED, 1, GA_Defaults(0), nullptr, nullptr, GA_STORE_INT32));
		seed.bind(seedRef);
	}
};

void ShapeConverter::get(const GU_Detail* detail, const PrimitiveClassifier& primCls,
                         ShapeData& shapeData, const PRTContextUPtr& prtCtx)
{
	WA("all");

	assert(shapeData.isValid());

	// -- partition primitives into initial shapes by primitive classifier values
	PrimitivePartition primPart(detail, primCls);
	const PrimitivePartition::PartitionMap& partitions = primPart.get();

	// -- copy all coordinates
	std::vector<double> coords;
	assert(detail->getPointRange().getEntries() == detail->getNumPoints());
	coords.reserve(detail->getNumPoints()*3);
	GA_Offset ptoff;
	GA_FOR_ALL_PTOFF(detail, ptoff) {
		const UT_Vector3 p = detail->getPos3(ptoff);
		if (DBG) LOG_DBG << "coords " << coords.size()/3 << ": " << p.x() << ", " << p.y() << ", " << p.z();
		coords.push_back(static_cast<double>(p.x()));
		coords.push_back(static_cast<double>(p.y()));
		coords.push_back(static_cast<double>(p.z()));
	}

	// -- loop over all primitive partitions and create shape builders
	uint32_t isIdx = 0;
	for (auto pIt = partitions.cbegin(); pIt != partitions.cend(); ++pIt, ++isIdx) {
		if (DBG) LOG_DBG << "   -- creating initial shape " << isIdx << ", prim count = " << pIt->second.size();

		// merge primitive geometry inside partition (potential multi-polygon initial shape)
		std::vector<uint32_t> indices, faceCounts, holes;
		std::array<double,3> centroid = { 0.0, 0.0, 0.0 };
		for (const auto& prim: pIt->second) {
			if (DBG) LOG_DBG << "   -- prim index " << prim->getMapIndex() << ", type: " << prim->getTypeName() << ", id = " << prim->getTypeId().get();
			const auto& primType = prim->getTypeId();
			if (primType == GA_PRIMPOLY || primType == GA_PRIMPOLYSOUP) {
				const GA_Size vtxCnt = prim->getVertexCount();
				faceCounts.push_back(static_cast<uint32_t>(vtxCnt));
				for (GA_Size i = vtxCnt - 1; i >= 0; i--) {
					indices.push_back(static_cast<uint32_t>(prim->getPointIndex(i)));
					if (DBG) LOG_DBG << "      vtx " << i << ": point idx = " << prim->getPointIndex(i);

					centroid[0] += coords[3*indices.back()+0];
					centroid[1] += coords[3*indices.back()+1];
					centroid[2] += coords[3*indices.back()+2];
				}
			}
		} // for each polygon

		// prepare and store initial shape builder
		InitialShapeBuilderUPtr isb(prt::InitialShapeBuilder::create());
		isb->setGeometry(coords.data(), coords.size(),
		                 indices.data(), indices.size(),
		                 faceCounts.data(), faceCounts.size(),
		                 holes.data(), holes.size());

		// try to get random seed from incoming primitive attributes (important for default rule attr eval)
		// use centroid based hash as fallback
		int32_t randomSeed = 0;
		GA_ROAttributeRef seedRef(detail->findPrimitiveAttribute(PLD_RANDOM_SEED));
		if (!seedRef.isInvalid() && (seedRef->getStorageClass() == GA_STORECLASS_INT)) { // TODO: check for 32bit
			GA_ROHandleI seedH(seedRef);
			randomSeed = seedH.get(pIt->second.front()->getMapOffset());
		}
		else {
			centroid[0] /= (double)indices.size();
			centroid[1] /= (double)indices.size();
			centroid[2] /= (double)indices.size();

			size_t hash = 0;
			PLD_BOOST_NS::hash_combine(hash, centroid[0]);
			PLD_BOOST_NS::hash_combine(hash, centroid[1]);
			PLD_BOOST_NS::hash_combine(hash, centroid[2]);
			randomSeed = static_cast<int32_t>(hash); // TODO: do we still get a good hash with this truncation?
		}

		shapeData.addBuilder(std::move(isb), randomSeed, pIt->second, pIt->first);
	} // for each primitive partition

	assert(shapeData.isValid());
}

void ShapeConverter::put(GU_Detail* detail, PrimitiveClassifier& primCls, const ShapeData& shapeData) const {
	WA("all");

	primCls.setupAttributeHandles(detail);

	MainAttributeHandles mah;
	mah.setup(detail);

	// generate primitive attribute handles from all default rule attribute names from all initial shapes
	AttributeMapVector defaultRuleAttributeMaps;
	std::map<const wchar_t*, GA_RWAttributeRef> mAttrRefs; // key points to defaultRuleAttributeMaps
	for (auto& amb: shapeData.getRuleAttributeMapBuilders()) {
		defaultRuleAttributeMaps.emplace_back(amb->createAttributeMap());
		const auto& dra = defaultRuleAttributeMaps.back();

		size_t keyCount = 0;
		const wchar_t* const* cKeys = dra->getKeys(&keyCount);
		for (size_t k = 0; k < keyCount; k++) {
			const wchar_t* key = cKeys[k];

			// make sure to only generate an attribute handle once
			if (mAttrRefs.count(key) > 0)
				continue;

			const UT_String primAttrName = NameConversion::toPrimAttr(key);

			GA_Attribute* primAttr = nullptr;
			switch (dra->getType(key)) {
				case prt::AttributeMap::PT_FLOAT: {
					primAttr = detail->addFloatTuple(GA_ATTRIB_PRIMITIVE, primAttrName, 1); // TODO: use double storage
					break;
				}
				case prt::AttributeMap::PT_BOOL: {
					primAttr = detail->addIntTuple(GA_ATTRIB_PRIMITIVE, primAttrName, 1); // TODO: use store type uint8
					break;
				}
				case prt::AttributeMap::PT_STRING: {
					primAttr = detail->addStringTuple(GA_ATTRIB_PRIMITIVE, primAttrName, 1);
					break;
				}
				default:
					break;
			} // switch type

			if (primAttr != nullptr)
				mAttrRefs.emplace(key, primAttr);
			else
				LOG_ERR << "Could not create primitive attribute handle: " << key << " -> " << primAttrName << " (type " << dra->getType(key) << ")";

		} // for rule attribute
	} // for each initial shape

	for (size_t isIdx = 0; isIdx < shapeData.getRuleAttributeMapBuilders().size(); isIdx++) {
		const auto& pv = shapeData.getPrimitiveMapping(isIdx);
		const auto& defaultRuleAttributes = defaultRuleAttributeMaps[isIdx];
		const int32_t randomSeed = shapeData.getInitialShapeRandomSeed(isIdx);

		for (auto& prim: pv) {
			primCls.put(prim);
			putMainAttributes(mah, prim);

			const GA_Offset& off = prim->getMapOffset();

			mah.seed.set(off, randomSeed);

			size_t keyCount = 0;
			const wchar_t* const* cKeys = defaultRuleAttributes->getKeys(&keyCount);
			for (size_t k = 0; k < keyCount; k++) {
				const wchar_t* const key = cKeys[k];

				const auto& attrRefIt = mAttrRefs.find(key);
				if (attrRefIt == mAttrRefs.end())
					continue;
				const auto& attrRef = attrRefIt->second;

				switch (defaultRuleAttributes->getType(key)) {
					case prt::AttributeMap::PT_FLOAT: {
						GA_RWHandleD av(attrRef);
						if (av.isValid()) {
							const double defVal = defaultRuleAttributes->getFloat(key);
							av.set(off, defVal);
						}
						break;
					}
					case prt::AttributeMap::PT_BOOL: {
						GA_RWHandleI av(attrRef);
						const bool defVal = defaultRuleAttributes->getBool(key);
						av.set(off, defVal ? 1 : 0);
						break;
					}
					case prt::AttributeMap::PT_STRING: {
						GA_RWHandleS av(attrRef);
						const wchar_t* const defVal = defaultRuleAttributes->getString(key);
						const std::string nDefVal = toOSNarrowFromUTF16(defVal); // !!!
						av.set(off, nDefVal.c_str());
						break;
					}
					default: {
						LOG_ERR << "Array attribute support not implemented yet";
					}
				} // switch type
			} // for default rule attribute keys
		} // for all primitives in initial shape
	} // for all initial shapes
}

void ShapeConverter::getMainAttributes(SOP_Node* node, const OP_Context& context) {
	const fpreal now = context.getTime();
	mRPK       = AssignNodeParams::getRPK(node, now);
	mRuleFile  = AssignNodeParams::getRuleFile(node, now);
	mStyle     = AssignNodeParams::getStyle(node, now);
	mStartRule = AssignNodeParams::getStartRule(node, now);
}

bool ShapeConverter::getMainAttributes(const GU_Detail* detail, const GA_Primitive* prim) {
	GA_ROAttributeRef rpkRef(detail->findPrimitiveAttribute(PLD_RPK));
	if (rpkRef.isInvalid())
		return false;

	GA_ROAttributeRef ruleFileRef(detail->findPrimitiveAttribute(PLD_RULE_FILE));
	if (ruleFileRef.isInvalid())
		return false;

	GA_ROAttributeRef startRuleRef(detail->findPrimitiveAttribute(PLD_START_RULE));
	if (startRuleRef.isInvalid())
		return false;

	GA_ROAttributeRef styleRef(detail->findPrimitiveAttribute(PLD_STYLE));
	if (styleRef.isInvalid())
		return false;

	const GA_Offset firstOffset = prim->getMapOffset();
	mRPK       = safeGet<PLD_BOOST_NS::filesystem::path>(firstOffset, rpkRef);
	mRuleFile  = toUTF16FromOSNarrow(safeGet<std::string>(firstOffset, ruleFileRef));
	mStartRule = toUTF16FromOSNarrow(safeGet<std::string>(firstOffset, startRuleRef));
	mStyle     = toUTF16FromOSNarrow(safeGet<std::string>(firstOffset, styleRef));

	return true;
}

void ShapeConverter::putMainAttributes(MainAttributeHandles& mah, const GA_Primitive* primitive) const {
	const GA_Offset &off = primitive->getMapOffset();
	mah.rpk.set(off, mRPK.string().c_str());
	mah.ruleFile.set(off, toOSNarrowFromUTF16(mRuleFile).c_str());
	mah.startRule.set(off, toOSNarrowFromUTF16(mStartRule).c_str());
	mah.style.set(off, toOSNarrowFromUTF16(mStyle).c_str());
}

RuleFileInfoUPtr ShapeConverter::getRuleFileInfo(const ResolveMapUPtr& resolveMap, prt::Cache* prtCache) const {
	if (!resolveMap->hasKey(mRuleFile.c_str())) // workaround for bug in getString
		return {};

	const auto cgbURI = resolveMap->getString(mRuleFile.c_str());
	if (cgbURI == nullptr)
		return {};

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	RuleFileInfoUPtr rfi(prt::createRuleFileInfo(cgbURI, prtCache, &status));
	if (status != prt::STATUS_OK)
		return {};

	return rfi;
}

std::wstring ShapeConverter::getFullyQualifiedStartRule() const {
	return L"Default$" + mStartRule; // PRT does not have full style support yet
}
