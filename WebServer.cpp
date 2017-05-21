// ============================================================================
//
// �O������L�[�ω��ʂ̎w�����󂯂邽�߂̃T�[�o�[
//
// ============================================================================

// ----------------------------------------------------------------------------
// �{���� CWinThread �̔h���Ŏ����������������AMFC �� Windows.h ��
// ���e��Ȃ��炵��
// MFC �� DirectShow �t�B���^�[�������@��������Ȃ��̂ŁAMFC ���g��Ȃ�����
// �Ƃ���
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
// �R���X�g���N�^�[�E�f�X�g���N�^�[
// ============================================================================

// ----------------------------------------------------------------------------
// �R���X�g���N�^�[
// ----------------------------------------------------------------------------
CWebServer::CWebServer(CEasyKeyChanger* oOwner)
{
	DebugWrite(L"CWebServer()");

	// ������
	mOwner = oOwner;
	mThreadHandle = NULL;
	mThreadId = 0;
	mExitRequested = false;

#ifdef DEBUGWRITE
	mInitialThis = this;
#endif
}

// ----------------------------------------------------------------------------
// �f�X�g���N�^�[
// ----------------------------------------------------------------------------
CWebServer::~CWebServer()
{
	DebugWrite(L"~CWebServer()");

	// �X���b�h�I����҂�
	mExitRequested = true;
	DWORD aExitCode = STILL_ACTIVE;
	while (aExitCode == STILL_ACTIVE) {
		Sleep(SLEEP_TIME);
		GetExitCodeThread(mThreadHandle, &aExitCode);
	}

	// �\�P�b�g��Еt��
	closesocket(mServerSocket);
	WSACleanup();

	DebugWrite(L"~CWebServer() �I��");
}

// ----------------------------------------------------------------------------
// ����֐�
// ----------------------------------------------------------------------------
void CWebServer::Run()
{
	mThreadHandle = CreateThread(NULL, 0, &ThreadFunc, this, 0, &mThreadId);
}

// ============================================================================
// private �֐�
// ============================================================================

// ----------------------------------------------------------------------------
// �N���C�A���g����̎�M���e����p�����[�^�[�l�����o��
// ----------------------------------------------------------------------------
string CWebServer::GetParamValue(const string& oContent, const string& oParamName)
{
	AssertWrite(!oContent.empty(), L"GetParamValue() content is empty.");

	// �p�����[�^�[��������
	string::size_type aBeginPos = oContent.find(oParamName + "=");
	if (aBeginPos == string::npos) {
		return string();
	}
	aBeginPos += oParamName.length() + 1;
	if (aBeginPos >= oContent.length()) {
		return string();
	}

	// �p�����[�^�[�l�̏I��������
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
// �T�[�o�[�\�P�b�g������
// ----------------------------------------------------------------------------
bool CWebServer::InitServerSocket()
{
	// Winsock ������
	WSADATA aWsaData;
	int aWinsockErr = WSAStartup(MAKEWORD(2, 2), &aWsaData);
	if (aWinsockErr != 0) {
		DebugWrite(L"InitServerSocket() Winsock err ���������s");
		return false;
	}

	// �T�[�o�[�\�P�b�g�쐬
	mServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (mServerSocket == INVALID_SOCKET) {
		DebugWrite(L"InitServerSocket() err �T�[�o�[�\�P�b�g�쐬���s: " + lexical_cast<wstring>(WSAGetLastError()));
		return false;
	}

	BOOL aReuseVal = TRUE;
	setsockopt(mServerSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&aReuseVal), sizeof(aReuseVal));

	sockaddr_in aAddr;
	aAddr.sin_family = AF_INET;
	aAddr.sin_port = htons(DEFAULT_PORT_INDEX);
	aAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (::bind(mServerSocket, reinterpret_cast<sockaddr*>(&aAddr), sizeof(aAddr)) != 0) {
		DebugWrite(L"InitServerSocket() bind err ���s" + lexical_cast<wstring>(WSAGetLastError()));
		return false;
	}

	// �m���u���b�L���O�ݒ�
	unsigned long aNbVal = 1;
	ioctlsocket(mServerSocket, FIONBIO, &aNbVal);

	if (listen(mServerSocket, SOMAXCONN) != 0) {
		DebugWrite(L"InitServerSocket() listen err ���s" + lexical_cast<wstring>(WSAGetLastError()));
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------
// �N���C�A���g����̐ڑ��ɉ���
// ----------------------------------------------------------------------------
void CWebServer::Response()
{
	//DebugWrite(L"Response()");
	AssertWrite(this == mInitialThis, L"Response() �C���X�^���X�ړ�");

	// ���׌y���ҋ@
	Sleep(SLEEP_TIME);
	//DebugWrite(L"Response() Go");

	// �N���C�A���g����̃f�[�^����M
	SOCKET aAcceptSocket = accept(mServerSocket, NULL, NULL);
	try {
		if (aAcceptSocket == INVALID_SOCKET) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				DebugWrite(L"Response() accept err: " + lexical_cast<wstring>(WSAGetLastError()));
			}
			throw - 1;
		}

		// ��M
		char aReceiveBuf[RECEIVE_BUF_LEN];
		memset(aReceiveBuf, 0, sizeof(aReceiveBuf));
		int aNumReceives = recv(aAcceptSocket, aReceiveBuf, sizeof(aReceiveBuf), 0);
		if (aNumReceives == 0) {
			DebugWrite(L"Response() recv err connection closed.");
			throw - 1;
		}
		else if (aNumReceives < 0) {
			DebugWrite(L"Response() recv err ���s: " + lexical_cast<wstring>(WSAGetLastError()));
			throw - 1;
		}
		DebugWrite(L"Response() recv done. len: " + lexical_cast<wstring>(aNumReceives));

		// ���
		string aReceiveStr = string(aReceiveBuf, aNumReceives);
		string aResponseBody;
		string aCrossVal = GetParamValue(aReceiveStr, PARAM_NAME_CROSS);
		string aCutVal = GetParamValue(aReceiveStr, PARAM_NAME_CUT);
		string aHelpVal = GetParamValue(aReceiveStr, PARAM_NAME_HELP);
		string aKeyVal = GetParamValue(aReceiveStr, PARAM_NAME_KEY);
		string aTokenVal = GetParamValue(aReceiveStr, PARAM_NAME_TOKEN);

		// �A����M���p�g�[�N���̊m�F
		bool aValidToken = true;
		if (!aTokenVal.empty()) {
			if (aTokenVal == mPrevToken) {
				aResponseBody += "Duplicated token.\r\n";
				aValidToken = false;
			}
			mPrevToken = aTokenVal;
		}

		if (aValidToken) {
			// �N���X�t�F�[�h��
			if (!aCrossVal.empty()) {
				int aNewCross = atoi(aCrossVal.c_str());
				if (mOwner->SetCrossTime(aNewCross)) {
					aResponseBody += "Crossfade time is changed: " + lexical_cast<string>(aNewCross) + " [ms]\r\n";
				}
			}

			// �؂�o����
			if (!aCutVal.empty()) {
				int aNewCut = atoi(aCutVal.c_str());
				if (mOwner->SetCutTime(aNewCut)) {
					aResponseBody += "Cut time is changed: " + lexical_cast<string>(aNewCut) + " [ms]\r\n";
				}
			}

			// �w���v
			if (!aHelpVal.empty()) {
				if (aHelpVal == PARAM_VALUE_HELP_VER) {
					aResponseBody += APP_NAME_EN + "  \r\n";
					aResponseBody += APP_BIT + "  \r\n";
					aResponseBody += APP_VER + "  \r\n";
					aResponseBody += APP_COPYRIGHT + "  \r\n";
				}
			}

			// �L�[�`�F���W
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

			// �L�[�`�F���W���ʂ̃��b�Z�[�W
			if (aKeyChanged) {
				aResponseBody += "key is changed.";
			}
			else {
				aResponseBody += "key is not changed.";
			}
			aResponseBody += "\r\nkey: " + lexical_cast<string>(mOwner->KeyShift()) + "\r\n";

		}

		// �ԐM
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
// static �֐�
// ============================================================================

// ----------------------------------------------------------------------------
// �X���b�h�֐�
// ----------------------------------------------------------------------------
DWORD WINAPI CWebServer::ThreadFunc(void* oParam)
{
	DebugWrite(L"ThreadFunc()");

	CWebServer* aWebServer = reinterpret_cast<CWebServer*>(oParam);

	// ����
	if (!aWebServer->InitServerSocket()) {
		return -1;
	}

	// �N���C�A���g�Ƃ̂����
	for (;;) {
		// ����
		aWebServer->Response();

		// �I���`�F�b�N
		if (aWebServer->mExitRequested) {
			break;
		}

		//DebugWrite(L"ThreadFunc() running...");
	}

	DebugWrite(L"ThreadFunc() Exit");
	return 0;
}

