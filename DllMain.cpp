// ============================================================================
// 
// 簡易キーチェンジャー
// 
// ============================================================================

// ----------------------------------------------------------------------------
// リリースビルドの Strmbase.lib を使って EasyKeyChanger をデバッグビルドすると落ちるので注意（DEBUG フラグ立てるだけでもダメ）
// MFC をスタティックにする
//   全般→MFC の使用：スタティック
//   C++ コード生成→ランタイムライブラリ：マルチスレッド
//   ※Strmbase.lib も同じオプションでビルドしておく必要がある
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// stdafx
#include "stdafx.h"
// ----------------------------------------------------------------------------
// Windows
#include <Streams.h>
// C++
#include <iostream>
// Project
#include "EasyKeyChanger.h"
// ----------------------------------------------------------------------------
using namespace std;
// ----------------------------------------------------------------------------

// ============================================================================
// Strmbase.lib で使用されるファクトリテンプレートの宣言
// ============================================================================

// 入力ピンのメディアタイプ
const AMOVIESETUP_MEDIATYPE gPinMediaTypeIn =
{
	&MEDIATYPE_Audio, &MEDIASUBTYPE_NULL
};

// 出力ピンのメディアタイプ
const AMOVIESETUP_MEDIATYPE gPinMediaTypeOut =
{
	&MEDIATYPE_Audio, &MEDIASUBTYPE_NULL
};

// ピン情報
const AMOVIESETUP_PIN gPinInfo[] =
{
	{
		L"Input",			// ピンの名前（廃止）
		FALSE,				// このピンからの入力をレンダリングする
		FALSE,				// 出力ピンかどうか
		FALSE,				// TRUE の場合は、フィルタがこのピンのインスタンスを 1 つも持たないことがある
		FALSE,				// TRUE の場合は、フィルタがこの種類のピンのインスタンスを複数作成できる
		&CLSID_NULL,		// 廃止
		NULL,				// 廃止
		1,					// メディアタイプの数
		&gPinMediaTypeIn	// メディアタイプ
	},
	{
		L"Output",			// ピンの名前（廃止）
		FALSE,				// このピンからの入力をレンダリングする
		TRUE,				// 出力ピンかどうか
		FALSE,				// TRUE の場合は、フィルタがこのピンのインスタンスを 1 つも持たないことがある
		FALSE,				// TRUE の場合は、フィルタがこの種類のピンのインスタンスを複数作成できる
		&CLSID_NULL,		// 廃止
		NULL,				// 廃止
		1,					// メディアタイプの数
		&gPinMediaTypeOut	// メディアタイプ
	}
};

// フィルター
const AMOVIESETUP_FILTER gFilterInfo =
{
	&CLSID_EasyKeyChanger,			// クラスID
	FILTER_NAME,					// フィルタ名
	MERIT_DO_NOT_USE,				// メリット
	2,								// ピンの数
	gPinInfo						// ピン情報
};

// ファクトリテンプレート
CFactoryTemplate g_Templates[] =
{
	{
		FILTER_NAME,
		&CLSID_EasyKeyChanger,
		CEasyKeyChanger::CreateInstance,
		NULL,
		&gFilterInfo
	}
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

// ============================================================================
// グローバル関数
// ============================================================================

// ============================================================================
BOOL APIENTRY DllMain(HMODULE oHModule, DWORD  oReason, LPVOID oReserved)
{
	switch (oReason)
	{
	case DLL_PROCESS_ATTACH:
		// 初期化
		CoInitialize(NULL);
		g_hInst = static_cast<HINSTANCE>(oHModule);
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		// 後始末
		CoUninitialize();
		break;
	}
	return TRUE;
}
// ============================================================================
STDAPI DllRegisterServer()
{
	return AMovieDllRegisterServer2(TRUE);
}
// ----------------------------------------------------------------------------
STDAPI DllUnregisterServer()
{
	return AMovieDllRegisterServer2(FALSE);
}
// ============================================================================

