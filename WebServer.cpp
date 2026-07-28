// ============================================================================
//
// 外部からキー変化量の指示を受けるためのサーバー
//
// ============================================================================

// ----------------------------------------------------------------------------
// 本当は CWinThread の派生で実装したかったが、MFC と Windows.h は
// 相容れないらしい
// MFC で DirectShow フィルターを作る方法が分からないので、MFC を使わない実装
// とする
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// stdafx
#include "stdafx.h"
// ----------------------------------------------------------------------------
// Unit
#include "WebServer.h"
// ----------------------------------------------------------------------------
// Project
#include "Common.h"
#include "DebugWrite.h"
#include "EasyKeyChanger.h"
// ----------------------------------------------------------------------------
// C++
#include <boost/lexical_cast.hpp>
// ----------------------------------------------------------------------------
using namespace boost;
// ----------------------------------------------------------------------------

// ============================================================================
// コンストラクター・デストラクター
// ============================================================================

// ----------------------------------------------------------------------------
// コンストラクター
// ----------------------------------------------------------------------------
CWebServer::CWebServer(CEasyKeyChanger* oOwner)
{
	DebugWrite(L"CWebServer()");

	// 初期化
	mOwner = oOwner;
	mThreadHandle = NULL;
	mThreadId = 0;
	mExitRequested = false;
	mServerSocket = INVALID_SOCKET;

#ifdef DEBUGWRITE
	mInitialThis = this;
#endif
}

// ----------------------------------------------------------------------------
// デストラクター
// ----------------------------------------------------------------------------
CWebServer::~CWebServer()
{
	DebugWrite(L"~CWebServer()");

	// スレッド終了を待つ
	mExitRequested = true;
	if (mThreadHandle != NULL) {
		DWORD aExitCode = STILL_ACTIVE;
		while (aExitCode == STILL_ACTIVE) {
			Sleep(SLEEP_TIME);
			GetExitCodeThread(mThreadHandle, &aExitCode);
		}
		CloseHandle(mThreadHandle);
	}

	// ソケット後片付け
	if (mServerSocket != INVALID_SOCKET) {
		closesocket(mServerSocket);
		WSACleanup();
	}

	DebugWrite(L"~CWebServer() 終了");
}

// ----------------------------------------------------------------------------
// 制御関数
// ----------------------------------------------------------------------------
void CWebServer::Run()
{
	mThreadHandle = CreateThread(NULL, 0, &ThreadFunc, this, 0, &mThreadId);
}

// ============================================================================
// private 関数
// ============================================================================

// ----------------------------------------------------------------------------
// クライアントからの受信内容からパラメーター値を取り出す
// ----------------------------------------------------------------------------
string CWebServer::GetParamValue(const string& oContent, const string& oParamName)
{
	AssertWrite(!oContent.empty(), L"GetParamValue() content is empty.");

	// パラメーター名を検索
	string::size_type aBeginPos = oContent.find(oParamName + "=");
	if (aBeginPos == string::npos) {
		return string();
	}
	aBeginPos += oParamName.length() + 1;
	if (aBeginPos >= oContent.length()) {
		return string();
	}

	// パラメーター値の終わりを検索
	string aParamValue;
	string::size_type aEndPos = oContent.find_first_of("& \r\n", aBeginPos);
	if (aEndPos == string::npos) {
		DebugWrite(L"GetParamValue() substr to end.");
		aParamValue = oContent.substr(aBeginPos);
	}
	else {
		DebugWrite(L"GetParamValue() substr len: " + lexical_cast<wstring>(aEndPos - aBeginPos));
		aParamValue = oContent.substr(aBeginPos, aEndPos - aBeginPos);
	}

	return aParamValue;
}

// ----------------------------------------------------------------------------
// サーバーソケット初期化
// ----------------------------------------------------------------------------
bool CWebServer::InitServerSocket()
{
	// Winsock 初期化
	WSADATA aWsaData;
	int aWinsockErr = WSAStartup(MAKEWORD(2, 2), &aWsaData);
	if (aWinsockErr != 0) {
		DebugWrite(L"InitServerSocket() Winsock err 初期化失敗");
		return false;
	}

	// サーバーソケット作成
	mServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (mServerSocket == INVALID_SOCKET) {
		DebugWrite(L"InitServerSocket() err サーバーソケット作成失敗: " + lexical_cast<wstring>(WSAGetLastError()));
		WSACleanup();
		return false;
	}

	// ポートの所有者が常に 1 つになるよう、排他的にバインドする
	// （複数のインスタンスが同時にバインドできてしまうと、どのインスタンスが
	// 　応答するか不定になるため）
	BOOL aExclusiveVal = TRUE;
	setsockopt(mServerSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&aExclusiveVal), sizeof(aExclusiveVal));

	sockaddr_in aAddr;
	aAddr.sin_family = AF_INET;
	aAddr.sin_port = htons(DEFAULT_PORT_INDEX);
	aAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (::bind(mServerSocket, reinterpret_cast<sockaddr*>(&aAddr), sizeof(aAddr)) != 0) {
		DebugWrite(L"InitServerSocket() bind err 失敗" + lexical_cast<wstring>(WSAGetLastError()));
		closesocket(mServerSocket);
		mServerSocket = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	// ノンブロッキング設定
	unsigned long aNbVal = 1;
	ioctlsocket(mServerSocket, FIONBIO, &aNbVal);

	if (listen(mServerSocket, SOMAXCONN) != 0) {
		DebugWrite(L"InitServerSocket() listen err 失敗" + lexical_cast<wstring>(WSAGetLastError()));
		closesocket(mServerSocket);
		mServerSocket = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------
// クライアントからの接続に応答
// ----------------------------------------------------------------------------
void CWebServer::Response()
{
	//DebugWrite(L"Response()");
	AssertWrite(this == mInitialThis, L"Response() インスタンス移動");

	// 負荷軽減待機
	Sleep(SLEEP_TIME);
	//DebugWrite(L"Response() Go");

	// クライアントからのデータを受信
	SOCKET aAcceptSocket = accept(mServerSocket, NULL, NULL);
	try {
		if (aAcceptSocket == INVALID_SOCKET) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				DebugWrite(L"Response() accept err: " + lexical_cast<wstring>(WSAGetLastError()));
			}
			throw - 1;
		}

		// 受信
		// accept したソケットはサーバーソケットのノンブロッキング設定を引き継ぐため、
		// リクエストが届く前に recv して取りこぼさないよう、ブロッキングに戻したうえで
		// タイムアウトを設定する
		unsigned long aNbVal = 0;
		ioctlsocket(aAcceptSocket, FIONBIO, &aNbVal);
		DWORD aTimeOut = RECEIVE_TIME_OUT;
		setsockopt(aAcceptSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&aTimeOut), sizeof(aTimeOut));

		char aReceiveBuf[RECEIVE_BUF_LEN];
		memset(aReceiveBuf, 0, sizeof(aReceiveBuf));
		int aNumReceives = recv(aAcceptSocket, aReceiveBuf, sizeof(aReceiveBuf), 0);
		if (aNumReceives == 0) {
			DebugWrite(L"Response() recv err connection closed.");
			throw - 1;
		}
		else if (aNumReceives < 0) {
			DebugWrite(L"Response() recv err 失敗: " + lexical_cast<wstring>(WSAGetLastError()));
			throw - 1;
		}
		DebugWrite(L"Response() recv done. len: " + lexical_cast<wstring>(aNumReceives));

		// 解析
		string aReceiveStr = string(aReceiveBuf, aNumReceives);
		string aResponseBody;
		string aCrossVal = GetParamValue(aReceiveStr, PARAM_NAME_CROSS);
		string aCutVal = GetParamValue(aReceiveStr, PARAM_NAME_CUT);
		string aHelpVal = GetParamValue(aReceiveStr, PARAM_NAME_HELP);
		string aKeyVal = GetParamValue(aReceiveStr, PARAM_NAME_KEY);
		string aTokenVal = GetParamValue(aReceiveStr, PARAM_NAME_TOKEN);

		// 連続受信回避用トークンの確認
		bool aValidToken = true;
		if (!aTokenVal.empty()) {
			if (aTokenVal == mPrevToken) {
				aResponseBody += "Duplicated token.\r\n";
				aValidToken = false;
			}
			mPrevToken = aTokenVal;
		}

		if (aValidToken) {
			// クロスフェード幅
			if (!aCrossVal.empty()) {
				int aNewCross = atoi(aCrossVal.c_str());
				if (mOwner->SetCrossTime(aNewCross)) {
					aResponseBody += "Crossfade time is changed: " + lexical_cast<string>(aNewCross) + " [ms]\r\n";
				}
			}

			// 切り出し幅
			if (!aCutVal.empty()) {
				int aNewCut = atoi(aCutVal.c_str());
				if (mOwner->SetCutTime(aNewCut)) {
					aResponseBody += "Cut time is changed: " + lexical_cast<string>(aNewCut) + " [ms]\r\n";
				}
			}

			// ヘルプ
			if (!aHelpVal.empty()) {
				if (aHelpVal == PARAM_VALUE_HELP_VER) {
					aResponseBody += APP_NAME_EN + "  \r\n";
					aResponseBody += APP_BIT + "  \r\n";
					aResponseBody += APP_VER + "  \r\n";
					aResponseBody += APP_COPYRIGHT + "  \r\n";
				}
			}

			// キーチェンジ
			bool aKeyChanged = false;
			if (!aKeyVal.empty()) {
				if (aKeyVal == PARAM_VALUE_KEY_UP) {
					aKeyChanged = mOwner->SetKeyShift(mOwner->KeyShift() + 1);
				}
				else if (aKeyVal == PARAM_VALUE_KEY_DOWN) {
					aKeyChanged = mOwner->SetKeyShift(mOwner->KeyShift() - 1);
				}
				else {
					if (isdigit(aKeyVal[0]) || aKeyVal[0] == '-') {
						int aNewKey = atoi(aKeyVal.c_str());
						DebugWrite(L"Response() direct key: " + lexical_cast<wstring>(aNewKey));
						aKeyChanged = mOwner->SetKeyShift(aNewKey);
					}
				}
			}

			// 動作状況（応答しているフィルターの状態が分かるようにする）
			// コントローラーページは "key is" の前までを情報として表示し、"key: " 以降を
			// キーの数値として解釈するため、これらより前に出力する
			aResponseBody += "transformable: ";
			aResponseBody += mOwner->Transformable() ? "yes" : "no";
			aResponseBody += "\r\n";
			if (!mOwner->Transformable()) {
				aResponseBody += "reason: " + WStringToUtf8String(mOwner->NoTransformReason()) + "\r\n";
			}
			aResponseBody += "input: " + WStringToUtf8String(mOwner->InputFormatSummary()) + "\r\n";
			aResponseBody += "instance: pid " + lexical_cast<string>(GetCurrentProcessId()) + "\r\n";

			// キーチェンジ結果のメッセージ
			if (aKeyChanged) {
				aResponseBody += "key is changed.";
			}
			else {
				aResponseBody += "key is not changed.";
			}
			aResponseBody += "\r\nkey: " + lexical_cast<string>(mOwner->KeyShift()) + "\r\n";

		}

		// 返信
		string aResponse;
		aResponse = RESPONSE_TEMPLATE_1 + lexical_cast<string>(aResponseBody.length()) + RESPONSE_TEMPLATE_2 + aResponseBody;
		DebugWrite(L"Response() response len: " + lexical_cast<wstring>(aResponse.length()));
		send(aAcceptSocket, aResponse.c_str(), static_cast<int>(aResponse.length()), 0);

		DebugWrite(L"Response() done");
	}
	catch (...) {

	}

	closesocket(aAcceptSocket);
}


// ============================================================================
// static 関数
// ============================================================================

// ----------------------------------------------------------------------------
// スレッド関数
// ----------------------------------------------------------------------------
DWORD WINAPI CWebServer::ThreadFunc(void* oParam)
{
	DebugWrite(L"ThreadFunc()");

	CWebServer* aWebServer = reinterpret_cast<CWebServer*>(oParam);

	// 準備
	// 他のインスタンスがポートを使用中の場合などは、解放を待って再試行する
	while (!aWebServer->InitServerSocket()) {
		for (int i = 0; i < aWebServer->RETRY_WAIT_COUNT; i++) {
			if (aWebServer->mExitRequested) {
				DebugWrite(L"ThreadFunc() Exit (init not done)");
				return 0;
			}
			Sleep(aWebServer->SLEEP_TIME);
		}
		DebugWrite(L"ThreadFunc() init retry");
	}

	// クライアントとのやり取り
	for (;;) {
		// 応答
		aWebServer->Response();

		// 終了チェック
		if (aWebServer->mExitRequested) {
			break;
		}

		//DebugWrite(L"ThreadFunc() running...");
	}

	DebugWrite(L"ThreadFunc() Exit");
	return 0;
}

