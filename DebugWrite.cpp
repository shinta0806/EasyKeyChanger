// ============================================================================
//
// デバッグ用情報のファイル出力
//
// ============================================================================

// ----------------------------------------------------------------------------
// stdafx
#include "stdafx.h"
// ----------------------------------------------------------------------------
// Unit
#include "DebugWrite.h"
// ----------------------------------------------------------------------------
// Project
#include "Common.h"
// ----------------------------------------------------------------------------
// C++
#include <boost/lexical_cast.hpp>
// ----------------------------------------------------------------------------
using namespace boost;
// ----------------------------------------------------------------------------

// ============================================================================
// デバッグ出力フラグ有効時専用定数
// ============================================================================

#ifdef DEBUGWRITE
const wstring ASSERT_CAPTION = L"*** ASSERT *** ";
const wchar_t* DEBUG_WRITE_FILE_NAME = L"R:\\Debug.txt";
#endif

// ============================================================================
// デバッグ出力フラグ有効時専用関数
// ============================================================================

#ifdef DEBUGWRITE

// ----------------------------------------------------------------------------
// ASSERT（中断しない）
// ----------------------------------------------------------------------------
void AssertWriteFunc(bool oCond, const wstring& oMsg)
{
	if (!oCond) {
		DebugWrite(ASSERT_CAPTION + oMsg);
	}
}

// ----------------------------------------------------------------------------
// デバッグ用の出力
// ----------------------------------------------------------------------------
void DebugWriteFunc(const wstring& oMsg)
{
#ifdef DEBUGWRITE
	HANDLE aFile = INVALID_HANDLE_VALUE;

	try {
		// ログファイルを開く
		aFile = CreateFile(DEBUG_WRITE_FILE_NAME, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (aFile == INVALID_HANDLE_VALUE) {
			throw - 1;
		}

		// メッセージ
		wstring aWriteMsg = oMsg + L"\n";

		// 書き込む
		if (SetFilePointer(aFile, 0, NULL, FILE_END) == INVALID_SET_FILE_POINTER) {
			throw - 1;
		}
		DWORD aWroteBytes;
		if (!WriteFile(aFile, aWriteMsg.c_str(), aWriteMsg.length() * 2, &aWroteBytes, NULL)) {
			throw - 1;
		}
	}
	catch (...) {
	}

	if (aFile != INVALID_HANDLE_VALUE) {
		CloseHandle(aFile);
	}
#endif
}

// ----------------------------------------------------------------------------
// デバッグ用のメディアタイプ出力
// ----------------------------------------------------------------------------
void DebugWriteMediaTypeFunc(const wstring& oTitle, const CMediaType* oMediaType)
{
#ifdef DEBUGWRITE
	wstring aMsg = oTitle;

	if (oMediaType == NULL) {
		aMsg += L" MediaType is NULL";
	}
	else {
		aMsg += L", MediaMajorType: " + GuidToWString(&oMediaType->majortype);
		if (oMediaType->majortype == MEDIATYPE_Audio) {
			aMsg += L" (Audio)";
		}
		else {
			aMsg += L" (Unknown)";
		}
		aMsg += L", MediaSubType: " + GuidToWString(&oMediaType->subtype);
		if (oMediaType->subtype == MEDIASUBTYPE_PCM) {
			aMsg += L" (PCM)";
		}
		else {
			aMsg += L" (Unknown)";
		}
		aMsg += L", Fixed: " + lexical_cast<wstring>(oMediaType->bFixedSizeSamples);
		aMsg += L", TmpComp: " + lexical_cast<wstring>(oMediaType->bTemporalCompression);
		aMsg += L", SampleSize: " + lexical_cast<wstring>(oMediaType->lSampleSize);
		aMsg += L", FormatType: " + GuidToWString(&oMediaType->formattype);
		if (oMediaType->formattype == FORMAT_WaveFormatEx) {
			aMsg += L" (WaveFormatEx)";

			WAVEFORMATEX* aWaveEx = reinterpret_cast<WAVEFORMATEX*>(oMediaType->Format());
			aMsg += L", Ch: " + lexical_cast<wstring>(aWaveEx->nChannels);
			aMsg += L", SampleRate: " + lexical_cast<wstring>(aWaveEx->nSamplesPerSec);
			aMsg += L", Bit: " + lexical_cast<wstring>(aWaveEx->wBitsPerSample);
		}
		else {
			aMsg += L" (Unknown)";
		}
	}
	DebugWrite(aMsg);
#endif
}
#endif