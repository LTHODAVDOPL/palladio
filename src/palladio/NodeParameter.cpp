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

#include "NodeParameter.h"
#include "SOPAssign.h"
#include "AttributeConversion.h"
#include "ShapeConverter.h"
#include "Utils.h"
#include "LogHandler.h"

#include "prt/API.h"

#include "CH/CH_Manager.h"

#include <string>
#include <vector>
#include <filesystem>


namespace {

constexpr const wchar_t* CGA_ANNOTATION_START_RULE = L"@StartRule";
constexpr const size_t   CGA_NO_START_RULE_FOUND   = size_t(-1);

using StringPairVector = std::vector<std::pair<std::string,std::string>>;
bool compareSecond (const StringPairVector::value_type& a, const StringPairVector::value_type& b) {
	return ( a.second < b.second );

}

/**
 * find start rule (first annotated start rule or just first rule as fallback)
 */
std::wstring findStartRule(const RuleFileInfoUPtr& info) {
	const size_t numRules = info->getNumRules();
	assert(numRules > 0);

	auto startRuleIdx = CGA_NO_START_RULE_FOUND;
	for (size_t ri = 0; ri < numRules; ri++) {
		const prt::RuleFileInfo::Entry *re = info->getRule(ri);
		for (size_t ai = 0; ai < re->getNumAnnotations(); ai++) {
			if (std::wcscmp(re->getAnnotation(ai)->getName(), CGA_ANNOTATION_START_RULE) == 0) {
				startRuleIdx = ri;
				break;
			}
		}
	}

	if (startRuleIdx == CGA_NO_START_RULE_FOUND)
		startRuleIdx = 0; // use first rule as fallback

	const prt::RuleFileInfo::Entry *re = info->getRule(startRuleIdx);
	return { re->getName() };
}

constexpr const int NOT_CHANGED = 0;
constexpr const int CHANGED     = 1;

} // namespace


namespace AssignNodeParams {

/**
 * validates and updates all parameters/states depending on the rule package
 */
int updateRPK(void* data, int, fpreal32 time, const PRM_Template*) {
	auto* node = static_cast<SOPAssign*>(data);
	const PRTContextUPtr& prtCtx = node->getPRTCtx();

	UT_String utNextRPKStr;
	node->evalString(utNextRPKStr, AssignNodeParams::RPK.getToken(), 0, time);
	const std::filesystem::path nextRPK(utNextRPKStr.toStdString());

	ResolveMapSPtr resolveMap = prtCtx->getResolveMap(nextRPK);
	if (!resolveMap ) {
		LOG_WRN << "invalid resolve map";
		return NOT_CHANGED;
	}

	// -- try get first rule file
	std::vector<std::pair<std::wstring, std::wstring>> cgbs; // key -> uri
	getCGBs(resolveMap, cgbs);
	if (cgbs.empty()) {
		LOG_ERR << "no rule files found in rule package";
		return NOT_CHANGED;
	}
	const std::wstring cgbKey = cgbs.front().first;
	const std::wstring cgbURI = cgbs.front().second;
	LOG_DBG << "cgbKey = " << cgbKey << ", " << "cgbURI = " << cgbURI;

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	const RuleFileInfoUPtr ruleFileInfo(prt::createRuleFileInfo(cgbURI.c_str(), prtCtx->mPRTCache.get(), &status)); // TODO: cache
	if (!ruleFileInfo || (status != prt::STATUS_OK) || (ruleFileInfo->getNumRules() == 0)) {
		LOG_ERR << "failed to get rule file info or rule file does not contain any rules";
		return NOT_CHANGED;
	}
	const std::wstring fqStartRule = findStartRule(ruleFileInfo);

	// -- get style/name from start rule
	auto getStartRuleComponents = [](const std::wstring& fqRule) -> std::pair<std::wstring,std::wstring> {
		std::wstring style, name;
		NameConversion::separate(fqRule, style, name);
		return { style, name };
	};
	const auto startRuleComponents = getStartRuleComponents(fqStartRule);
	LOG_DBG << "start rule: style = " << startRuleComponents.first << ", name = " << startRuleComponents.second;

	// -- update the node
	AssignNodeParams::setRuleFile(node, cgbKey, time);
	AssignNodeParams::setStyle(node, startRuleComponents.first, time);
	AssignNodeParams::setStartRule(node, startRuleComponents.second, time);

	// reset was successful, try to optimize the cache
	prtCtx->mPRTCache->flushAll();

	return CHANGED;
}

void buildStartRuleMenu(void* data, PRM_Name* theMenu, int theMaxSize, const PRM_SpareData*, const PRM_Parm* parm) {
	constexpr bool DBG = false;

	const auto* node = static_cast<SOPAssign*>(data);
	const PRTContextUPtr& prtCtx = node->getPRTCtx();

    const fpreal now = CHgetEvalTime();
	const std::filesystem::path rpk = getRPK(node, now);
	const std::wstring ruleFile = getRuleFile(node, now);

	if (DBG) {
		LOG_DBG << "buildStartRuleMenu";
		LOG_DBG << "   mRPK = " << rpk;
		LOG_DBG << "   mRuleFile = " << ruleFile;
	}

	if (rpk.empty() || ruleFile.empty()) {
		theMenu[0].setTokenAndLabel(nullptr, nullptr);
		return;
	}

	ResolveMapSPtr resolveMap = prtCtx->getResolveMap(rpk);
	if (!resolveMap) {
		theMenu[0].setTokenAndLabel(nullptr, nullptr);
		return;
	}

	const wchar_t* cgbURI = resolveMap->getString(ruleFile.c_str());
	if (cgbURI == nullptr) {
		LOG_ERR << L"failed to resolve rule file '" << ruleFile << "', aborting.";
		theMenu[0].setTokenAndLabel(nullptr, nullptr);
		return;
	}

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	RuleFileInfoUPtr rfi(prt::createRuleFileInfo(cgbURI, nullptr, &status));
	if (status == prt::STATUS_OK) {
		StringPairVector startRules, rules;
		for (size_t ri = 0; ri < rfi->getNumRules(); ri++) {
			const prt::RuleFileInfo::Entry* re = rfi->getRule(ri);
			const std::wstring wRuleName = NameConversion::removeStyle(re->getName());
			const std::string rn = toOSNarrowFromUTF16(wRuleName);

			bool hasStartRuleAnnotation = false;
			for (size_t ai = 0; ai < re->getNumAnnotations(); ai++) {
				if (std::wcscmp(re->getAnnotation(ai)->getName(), CGA_ANNOTATION_START_RULE) == 0) {
					hasStartRuleAnnotation = true;
					break;
				}
			}

			if (hasStartRuleAnnotation)
				startRules.emplace_back(rn, rn + " (@StartRule)");
			else
				rules.emplace_back(rn, rn);
		}

		std::sort(startRules.begin(), startRules.end(), compareSecond);
		std::sort(rules.begin(), rules.end(), compareSecond);
		rules.reserve(rules.size() + startRules.size());
		rules.insert(rules.begin(), startRules.begin(), startRules.end());

		const size_t limit = std::min<size_t>(rules.size(), static_cast<size_t>(theMaxSize));
		for (size_t ri = 0; ri < limit; ri++) {
			theMenu[ri].setTokenAndLabel(rules[ri].first.c_str(), rules[ri].second.c_str());
		}
		theMenu[limit].setTokenAndLabel(nullptr, nullptr); // need a null terminator
	}
}

void buildRuleFileMenu(void* data, PRM_Name* theMenu, int theMaxSize, const PRM_SpareData*, const PRM_Parm* parm) {
	const auto* node = static_cast<SOPAssign*>(data);
	const auto& prtCtx = node->getPRTCtx();

    const fpreal now = CHgetEvalTime();
	const std::filesystem::path rpk = getRPK(node, now);

	ResolveMapSPtr resolveMap = prtCtx->getResolveMap(rpk);
	if (!resolveMap) {
		theMenu[0].setToken(nullptr);
		return;
	}

	std::vector<std::pair<std::wstring,std::wstring>> cgbs; // key -> uri
	getCGBs(resolveMap, cgbs);

	const size_t limit = std::min<size_t>(cgbs.size(), static_cast<size_t>(theMaxSize));
	for (size_t ri = 0; ri < limit; ri++) {
		std::string tok = toOSNarrowFromUTF16(cgbs[ri].first);
		theMenu[ri].setTokenAndLabel(tok.c_str(), tok.c_str());
	}
	theMenu[limit].setTokenAndLabel(nullptr, nullptr); // need a null terminator
}

std::string extractStyle(const prt::RuleFileInfo::Entry* re) {
	std::wstring style, name;
	NameConversion::separate(re->getName(), style, name);
	return toOSNarrowFromUTF16(style);
}

void buildStyleMenu(void* data, PRM_Name* theMenu, int theMaxSize, const PRM_SpareData*, const PRM_Parm*) {
	const auto* node = static_cast<SOPAssign*>(data);
	const PRTContextUPtr& prtCtx = node->getPRTCtx();

    const fpreal now = CHgetEvalTime();
	const std::filesystem::path rpk = getRPK(node, now);
	const std::wstring ruleFile = getRuleFile(node, now);

	ResolveMapSPtr resolveMap = prtCtx->getResolveMap(rpk);
	if (!resolveMap) {
		theMenu[0].setTokenAndLabel(nullptr, nullptr);
		return;
	}

	const wchar_t* cgbURI = resolveMap->getString(ruleFile.c_str());
	if (cgbURI == nullptr) {
		theMenu[0].setTokenAndLabel(nullptr, nullptr);
		return;
	}

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	RuleFileInfoUPtr rfi(prt::createRuleFileInfo(cgbURI, nullptr, &status));
	if (rfi && (status == prt::STATUS_OK)) {
		std::set<std::string> styles;
		for (size_t ri = 0; ri < rfi->getNumRules(); ri++) {
			const prt::RuleFileInfo::Entry* re = rfi->getRule(ri);
			styles.emplace(extractStyle(re));
		}
 		for (size_t ai = 0; ai < rfi->getNumAttributes(); ai++) {
			const prt::RuleFileInfo::Entry* re = rfi->getAttribute(ai);
			styles.emplace(extractStyle(re));
		}
		size_t si = 0;
		for (const auto& s : styles) {
			theMenu[si].setTokenAndLabel(s.c_str(), s.c_str());
			si++;
		}
		const size_t limit = std::min<size_t>(styles.size(), static_cast<size_t>(theMaxSize));
		theMenu[limit].setTokenAndLabel(nullptr, nullptr); // need a null terminator
	}
}

} // namespace AssignNodeParams
