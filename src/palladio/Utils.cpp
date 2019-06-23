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

#include "Utils.h"
#include "LogHandler.h"

#include "prt/API.h"
#include "prt/StringUtils.h"

#ifndef _WIN32
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif
#include "BoostRedirect.h"
#include PLD_BOOST_INCLUDE(/algorithm/string.hpp)
#ifndef _WIN32
#	pragma GCC diagnostic pop
#endif

#include <filesystem>

#ifdef _WIN32
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif


void getCGBs(const ResolveMapSPtr& rm, std::vector<std::pair<std::wstring,std::wstring>>& cgbs) {
	constexpr const wchar_t* PROJECT    = L"";
	constexpr const wchar_t* PATTERN    = L"*.cgb";
	constexpr const size_t   START_SIZE = 16 * 1024;

	size_t resultSize = START_SIZE;
	auto * result = new wchar_t[resultSize]; // TODO: use std::array
	rm->searchKey(PROJECT, PATTERN, result, &resultSize);
	if (resultSize >= START_SIZE) {
		delete[] result;
		result = new wchar_t[resultSize];
		rm->searchKey(PROJECT, PATTERN, result, &resultSize);
	}
	std::wstring cgbList(result);
	delete[] result;
	LOG_DBG << "   cgbList = '" << cgbList << "'";

	std::vector<std::wstring> tok;
	PLD_BOOST_NS::split(tok, cgbList, PLD_BOOST_NS::is_any_of(L";"), PLD_BOOST_NS::algorithm::token_compress_on);
	for(const std::wstring& t: tok) {
		if (t.empty()) continue;
		LOG_DBG << "token: '" << t << "'";
		const wchar_t* s = rm->getString(t.c_str());
		if (s != nullptr) {
			cgbs.emplace_back(t, s);
			LOG_DBG << L"got cgb: " << cgbs.back().first << L" => " << cgbs.back().second;
		}
	}
}

const prt::AttributeMap* createValidatedOptions(const wchar_t* encID, const prt::AttributeMap* unvalidatedOptions) {
	const EncoderInfoUPtr encInfo(prt::createEncoderInfo(encID));
	const prt::AttributeMap* validatedOptions = nullptr;
	const prt::AttributeMap* optionStates = nullptr;
	const prt::Status s = encInfo->createValidatedOptionsAndStates(unvalidatedOptions, &validatedOptions, &optionStates);
	if (optionStates != nullptr)
		optionStates->destroy();
	return (s == prt::STATUS_OK) ? validatedOptions : nullptr;
}

std::string objectToXML(prt::Object const* obj) {
	if (obj == nullptr)
		throw std::invalid_argument("object pointer is not valid");
	constexpr size_t SIZE = 4096;
	size_t actualSize = SIZE;
	std::vector<char> buffer(SIZE, ' ');
	obj->toXML(buffer.data(), &actualSize);
	buffer.resize(actualSize);
	if(actualSize > SIZE)
		obj->toXML(buffer.data(), &actualSize);
	return std::string(buffer.data());
}

void getLibraryPath(std::filesystem::path& path, const void* func) {
#ifdef _WIN32
	HMODULE dllHandle = 0;
	if(!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)getLibraryPath, &dllHandle)) {
		DWORD c = GetLastError();
		char msg[255];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, 0, c, 0, msg, 255, 0);
		throw std::runtime_error("error while trying to get current module handle': " + std::string(msg));
	}
	assert(sizeof(TCHAR) == 1);
	const size_t PATHMAXSIZE = 4096;
	TCHAR pathA[PATHMAXSIZE];
	DWORD pathSize = GetModuleFileName(dllHandle, pathA, PATHMAXSIZE);
	if(pathSize == 0 || pathSize == PATHMAXSIZE) {
		DWORD c = GetLastError();
		char msg[255];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, 0, c, 0, msg, 255, 0);
		throw std::runtime_error("error while trying to get current module path': " + std::string(msg));
	}
	path = pathA;
#else /* macosx or linux */
	Dl_info dl_info;
	if(dladdr(func, &dl_info) == 0) {
		char* error = dlerror();
		throw std::runtime_error("error while trying to get current module path': " + std::string(error ? error : ""));
	}
	path = dl_info.dli_fname;
#endif
}

std::string getSharedLibraryPrefix() {
#if defined(_WIN32)
	return "";
#elif defined(__APPLE__)
	return "lib";
#elif defined(__linux__)
	return "lib";
#else
#	error unsupported build platform
#endif
}

std::string getSharedLibrarySuffix() {
#if defined(_WIN32)
	return ".dll";
#elif defined(__APPLE__)
	return ".dylib";
#elif defined(__linux__)
	return ".so";
#else
#	error unsupported build platform
#endif
}

std::string toOSNarrowFromUTF16(const std::wstring& osWString) {
	std::vector<char> temp(osWString.size());
	size_t size = temp.size();
	prt::Status status = prt::STATUS_OK;
	prt::StringUtils::toOSNarrowFromUTF16(osWString.c_str(), temp.data(), &size, &status);
	if(size > temp.size()) {
		temp.resize(size);
		prt::StringUtils::toOSNarrowFromUTF16(osWString.c_str(), temp.data(), &size, &status);
	}
	return std::string(temp.data());
}

std::wstring toUTF16FromOSNarrow(const std::string& osString) {
	std::vector<wchar_t> temp(osString.size());
	size_t size = temp.size();
	prt::Status status = prt::STATUS_OK;
	prt::StringUtils::toUTF16FromOSNarrow(osString.c_str(), temp.data(), &size, &status);
	if(size > temp.size()) {
		temp.resize(size);
		prt::StringUtils::toUTF16FromOSNarrow(osString.c_str(), temp.data(), &size, &status);
	}
	return std::wstring(temp.data());
}

std::string toUTF8FromOSNarrow(const std::string& osString) {
	std::wstring utf16String = toUTF16FromOSNarrow(osString);
	std::vector<char> temp(utf16String.size());
	size_t size = temp.size();
	prt::Status status = prt::STATUS_OK;
	prt::StringUtils::toUTF8FromUTF16(utf16String.c_str(), temp.data(), &size, &status);
	if(size > temp.size()) {
		temp.resize(size);
		prt::StringUtils::toUTF8FromUTF16(utf16String.c_str(), temp.data(), &size, &status);
	}
	return std::string(temp.data());
}

std::wstring toFileURI(const std::filesystem::path& p) {
#ifdef _WIN32
	static const std::wstring schema = L"file:/";
#else
	static const std::wstring schema = L"file:";
#endif
	std::string utf8Path = toUTF8FromOSNarrow(p.generic_string());
	std::wstring pecString = percentEncode(utf8Path);
	return schema + pecString;
}

std::wstring percentEncode(const std::string& utf8String) {
	std::vector<char> temp(2*utf8String.size());
	size_t size = temp.size();
	prt::Status status = prt::STATUS_OK;
	prt::StringUtils::percentEncode(utf8String.c_str(), temp.data(), &size, &status);
	if(size > temp.size()) {
		temp.resize(size);
		prt::StringUtils::percentEncode(utf8String.c_str(), temp.data(), &size, &status);
	}

	std::vector<wchar_t> u16temp(temp.size());
	size = u16temp.size();
	prt::StringUtils::toUTF16FromUTF8(temp.data(), u16temp.data(), &size, &status);
	if(size > u16temp.size()) {
		u16temp.resize(size);
		prt::StringUtils::toUTF16FromUTF8(temp.data(), u16temp.data(), &size, &status);
	}

	return std::wstring(u16temp.data());
}
