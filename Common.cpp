// ============================================================================
//
// ã§í ä÷êî
//
// ============================================================================

// ----------------------------------------------------------------------------
// stdafx
#include "stdafx.h"
// ----------------------------------------------------------------------------
// Unit
#include "Common.h"
// ----------------------------------------------------------------------------
// Windows
// ----------------------------------------------------------------------------

// ============================================================================
// ã§í ä÷êî
// ============================================================================

// ----------------------------------------------------------------------------
// GUID Çï∂éöóÒÇ…ïœä∑Ç∑ÇÈ
// ----------------------------------------------------------------------------
wstring GuidToWString(const GUID* oGuid)
{
	wstring aGuidString;
	RPC_WSTR aRpcString;

	// GUIDÇï∂éöóÒÇ÷ïœä∑Ç∑ÇÈ
	if (UuidToString(oGuid, &aRpcString) == RPC_S_OK) {
		aGuidString = reinterpret_cast<WCHAR*>(aRpcString);
		RpcStringFree(&aRpcString);
	}

	return aGuidString;
}

// ----------------------------------------------------------------------------
// wstring Ç UTF-8 ÇÃ string Ç…ïœä∑Ç∑ÇÈ
// ----------------------------------------------------------------------------
string WStringToUtf8String(const wstring& oWString)
{
	if (oWString.empty()) {
		return string();
	}

	int aNumBytes = WideCharToMultiByte(CP_UTF8, 0, oWString.c_str(), static_cast<int>(oWString.length()), NULL, 0, NULL, NULL);
	if (aNumBytes <= 0) {
		return string();
	}

	string aString(aNumBytes, '\0');
	WideCharToMultiByte(CP_UTF8, 0, oWString.c_str(), static_cast<int>(oWString.length()), &aString[0], aNumBytes, NULL, NULL);
	return aString;
}




