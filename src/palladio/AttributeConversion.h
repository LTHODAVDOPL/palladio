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

#pragma once

#include "PalladioMain.h"
#include "Utils.h"

#include "GU/GU_Detail.h"

#include <unordered_map>
#include <variant>


namespace std {
    template<> struct hash<UT_String> {
        std::size_t operator()(UT_String const& s) const noexcept {
            return s.hash();
        }
    };
}


namespace AttributeConversion {

/**
 * attribute type conversion from PRT to Houdini:
 * wstring -> narrow string
 * int32_t -> int32_t
 * bool    -> int8_t
 * double  -> float (single precision!)
 */
using NoHandle   = int8_t;
using HandleType = std::variant<NoHandle, GA_RWBatchHandleS, GA_RWHandleI, GA_RWHandleC, GA_RWHandleF>;


// bound to life time of PRT attribute map
struct ProtoHandle {
	HandleType                       handleType;
	std::wstring                     key;
	prt::AttributeMap::PrimitiveType type; // original PRT type
	size_t                           cardinality;
};

using HandleMap = std::unordered_map<UT_StringHolder, ProtoHandle>;

PLD_TEST_EXPORTS_API void extractAttributeNames(HandleMap& handleMap, const prt::AttributeMap* attrMap);
void createAttributeHandles(GU_Detail* detail, HandleMap& handleMap);
void setAttributeValues(HandleMap& handleMap, const prt::AttributeMap* attrMap,
                        const GA_IndexMap& primIndexMap, const GA_Offset rangeStart,
                        const GA_Size rangeSize);

} // namespace AttributeConversion


namespace NameConversion {

std::wstring addStyle(const std::wstring& n, const std::wstring& style);
std::wstring removeStyle(const std::wstring& n);
PLD_TEST_EXPORTS_API void separate(const std::wstring& fqName, std::wstring& style, std::wstring& name);

UT_String toPrimAttr(const std::wstring& name);
std::wstring toRuleAttr(const std::wstring& style, const UT_StringHolder& name);

} // namespace NameConversion
