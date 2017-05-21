// ============================================================================
// 
// �ȈՃL�[�`�F���W���[�{�́iDirectShow �t�B���^�[�j
// 
// ============================================================================

// ----------------------------------------------------------------------------
// �V�X�e���ɑ΂��ẮA���W���[�^�C�v���I�[�f�B�I�̂��̂͂��ׂđΉ��Ƃ��ĕԂ�
// ���ۂɂ͔�Ή��̂��̂́ATransform() �ŃX���[����
// ����ɂ��A��Ή����ɂ����[�U�[�ւ̉������\�ƂȂ�
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// �V�X�e������Ăт�����鏇�Ԃ́A�ȉ��̂悤�ɂȂ��Ă���͗l
//   CheckInputType()
//   CompleteConnect() �����͑�
//   GetMediaType() ��������
//   CheckTransform() ��������
//   CompleteConnect() ���o�͑�
//   DecideBufferSize()
//   Transform() ��������
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// stdafx
#include "stdafx.h"
// ----------------------------------------------------------------------------
#define _USE_MATH_DEFINES
// ----------------------------------------------------------------------------
// Unit
#include "EasyKeyChanger.h"
// ----------------------------------------------------------------------------
// C++
#include <cmath>
#include <boost/lexical_cast.hpp>
// ----------------------------------------------------------------------------
// Project
#include "Common.h"
#include "DebugWrite.h"
// ----------------------------------------------------------------------------
using namespace boost;
using namespace std;
// ----------------------------------------------------------------------------

// ============================================================================
// �R���X�g���N�^�[�E�f�X�g���N�^�[
// ============================================================================

// ----------------------------------------------------------------------------
// �R���X�g���N�^�[
// ----------------------------------------------------------------------------
CEasyKeyChanger::CEasyKeyChanger(TCHAR* oName, LPUNKNOWN oUnknown, HRESULT* oHResult)
	: BASE(oName, oUnknown, CLSID_EasyKeyChanger)
{
#ifdef DEBUGWRITE
	DebugWrite(L"CEasyKeyChanger() ==================== CEasyKeyChanger Ver 1a START ====================");
	DebugWrite(L"CEasyKeyChanger() DEBUGWRITE mode");
#endif

	// ������
	mOutputMedia = NULL;
	mMaxOutputFrames = 0;
	mTransformable = false;
	mKeyShift = 0;
	InitTimeTable();
	mPrevKeyShift = 0;
	mPrevCutTime = 0;
	mPrevCrossTime = 0;
	ZeroMemory(&mWaveFormat, sizeof(mWaveFormat));
	mWebServer = NULL;

	// �ϊ������p�̏�����
	mSrcAddBasePos = 0;
	mSrcAddPos = 0;
	mScale = 1.0;
	mCutTime = 0;
	mCutFrames = 0;
	mShiftFrames = 0;
	mCrossTime = 0;

#ifdef DEBUGWRITE
	mInitialThis = this;
#endif

	DebugWrite(L"CEasyKeyChanger() OK");
}

// ----------------------------------------------------------------------------
// �f�X�g���N�^�[
// ----------------------------------------------------------------------------
CEasyKeyChanger::~CEasyKeyChanger()
{
	DebugWrite(L"~CEasyKeyChanger()");

	delete mOutputMedia;
	delete mWebServer;

	DebugWrite(L"~CEasyKeyChanger() -------------------- END --------------------");
}

// ============================================================================
// CUnknown ����p��
// ============================================================================

// ----------------------------------------------------------------------------
// �p�����Ă���C���^�[�t�F�[�X��Ԃ�
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::NonDelegatingQueryInterface(REFIID oRIid, void** oInterface)
{
	return BASE::NonDelegatingQueryInterface(oRIid, oInterface);
}

// ============================================================================
// CTransformFilter ����p��
// ============================================================================

// ----------------------------------------------------------------------------
// ���̓��f�B�A�^�C�v���󂯕t���邩�m�F
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CheckInputType(const CMediaType* oMtIn)
{
	DebugWrite(L"CheckInputType() major type: " + GuidToWString(oMtIn->Type()));

	HRESULT aHResult = CheckTypeCore(oMtIn);
	if (FAILED(aHResult)) {
		return aHResult;
	}

	DebugWrite(L"CheckInputType() OK");
	return S_OK;
}

// ----------------------------------------------------------------------------
// �ϊ����f�B�A�^�C�v���󂯕t���邩�m�F
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CheckTransform(const CMediaType* oMtIn, const CMediaType* oMtOut)
{
	DebugWrite(L"CheckTransform() in major type: " + GuidToWString(oMtIn->Type()));
	DebugWrite(L"CheckTransform() out major type: " + GuidToWString(oMtOut->Type()));

	HRESULT aHResult;

	aHResult = CheckTypeCore(oMtIn);
	if (FAILED(aHResult)) {
		return aHResult;
	}
	aHResult = CheckTypeCore(oMtOut);
	if (FAILED(aHResult)) {
		return aHResult;
	}

	// �����܂œ��B�������͎̂󂯕t���邪�A���ۂɕϊ��\���ǂ����͕ʖ��
	// �ȉ��ŁA�ϊ��\���ǂ����̊m�F������
	try {
		// �T�u�^�C�v�̊m�F
		DebugWrite(L"CheckTransform() sub type: " + GuidToWString(oMtIn->Subtype()));
		if (*oMtIn->Subtype() != MEDIASUBTYPE_PCM) {
			throw L"�����f�[�^�����j�A PCM �ł͂���܂���B";
		}

		// �t�H�[�}�b�g�^�C�v�̊m�F
		DebugWrite(L"CheckTransform() format type: " + GuidToWString(oMtIn->FormatType()));
		if (*oMtIn->FormatType() != FORMAT_WaveFormatEx) {
			throw L"�����f�[�^�̃t�H�[�}�b�g�� WAVEFORMATEX �ł͂���܂���B";
		}

		// WAVE �t�H�[�}�b�g�̊m�F
		mWaveFormat = *reinterpret_cast<WAVEFORMATEX*>(oMtIn->Format());
		if (mWaveFormat.nChannels != NUM_CHANNEL_2) {
			throw L"�����f�[�^�̃`�����l�������X�e���I�ł͂���܂���B";
		}
		if (mWaveFormat.wBitsPerSample != BITS_PER_SAMPLE_16) {
			throw L"�����f�[�^�̃r�b�g�[�x�� 16 �ł͂���܂���B";
		}

		// �ϊ��\
		mTransformable = true;
	}
	catch (const wchar_t* oReason) {
		mNoTransformReason = oReason;
	}
	catch (...) {
		mNoTransformReason = L"�����͕s���ł��B";
	}

#ifdef DEBUGWRITE
	if (!mTransformable) {
		DebugWrite(L"CheckTransform() �ϊ��s�\: " + mNoTransformReason);
	}
#endif

	DebugWrite(L"CheckTransform() OK");
	return S_OK;
}

// ----------------------------------------------------------------------------
// ���̓s���E�o�̓s�����ꂼ��̐ڑ��������������ɌĂ΂��
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CompleteConnect(PIN_DIRECTION oDirection, IPin* oReceivePin)
{
	DebugWrite(L"CompleteConnect()");

	// PINDIR_INPUT �̏ꍇ�ȊO�͂�邱�Ɩ���
	if (oDirection != PINDIR_INPUT) {
		DebugWrite(L"CompleteConnect() return: not PINDIR_INPUT");
		return S_OK;
	}

	// �o�̓��f�B�A��ݒ�
	SetupOutputMedia(m_pInput->CurrentMediaType());

	DebugWrite(L"CompleteConnect() OK: INPUT");
	return S_OK;
}

// ----------------------------------------------------------------------------
// �o�̓s���̃A���P�[�^�ɕK�v�ȃo�b�t�@�T�C�Y��m�点��
// CompleteConnect() �̌�ɌĂ΂�邱�Ƃ�O��Ƃ��Ă���
// �ϊ��s�\�̏ꍇ�́A�p�X�X���[�p�̏o�̓o�b�t�@�ݒ�̂ݍs��
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::DecideBufferSize(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties)
{
	DebugWrite(L"DecideBufferSize()");

	// �s���̐ڑ����m�F
	if (!m_pInput->IsConnected()) {
		return E_UNEXPECTED;
	}
	if (!m_pOutput->IsConnected()) {
		return E_UNEXPECTED;
	}

	HRESULT aHResult = S_OK;

	// �o�̓s���̃o�b�t�@
	if (SUCCEEDED(aHResult)) {
		aHResult = SetupOutputBuffer(oAlloc, oProperties);
	}

	// �ϊ��s�\�̏ꍇ�͂����ŏI��
	if (!mTransformable) {
		return aHResult;
	}

	// �ϊ��p�̃o�b�t�@
	if (SUCCEEDED(aHResult)) {
		SetupTransformPre();
	}

	// �T�[�o�[�\�z
	if (SUCCEEDED(aHResult)) {
		mWebServer = new CWebServer(this);
		mWebServer->Run();
	}

#ifdef DEBUGWRITE
	if (SUCCEEDED(aHResult)) {
		DebugWrite(L"DecideBufferSize() OK");
	}
#endif

	return aHResult;
}

// ----------------------------------------------------------------------------
// �o�̓s���̃��f�B�A��Ԃ�
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::GetMediaType(int oPosition, CMediaType* oMediaType)
{
	DebugWrite(L"GetMediaType()");

	// ���̓s�����ڑ�����Ă��Ȃ��ꍇ�̓G���[
	if (!m_pInput->IsConnected()) {
		return E_UNEXPECTED;
	}

	// ���Ԗڂ̏o�̓s���̃��f�B�A��Ԃ��̂��i0 �Ԗڂ̂ݕԂ���j
	if (oPosition < 0) {
		return E_INVALIDARG;
	}
	if (oPosition > 0) {
		return VFW_S_NO_MORE_ITEMS;
	}

	// �쐬�ς݂̏o�̓��f�B�A�^�C�v�����̂܂ܕԂ�
	if (mOutputMedia == NULL) {
		DebugWrite(L"GetMediaType() mOutputMedia NULL");
		return E_UNEXPECTED;
	}
	*oMediaType = *mOutputMedia;

	DebugWriteMediaType(L"GetMediaType() OK", oMediaType);
	return S_OK;
}

// ----------------------------------------------------------------------------
// �㗬����f�[�^����M����
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::Receive(IMediaSample* oIn)
{
	return BASE::Receive(oIn);
}

// ----------------------------------------------------------------------------
// �t�B���^�[����
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::Transform(IMediaSample* oIn, IMediaSample *oOut)
{
	AssertWrite(this == mInitialThis, L"Transform() �C���X�^���X�ړ�");

	HRESULT aHResult = S_OK;

	// �w�b�_�[
	if (SUCCEEDED(aHResult)) {
		aHResult = CopyHeader(oIn, oOut);
	}

	// ���g�ϊ��̏���
	BYTE* aInBuf = NULL;
	BYTE* aOutBuf = NULL;
	long aInLength;

	if (SUCCEEDED(aHResult)) {
		aHResult = oIn->GetPointer(&aInBuf);
	}

	if (SUCCEEDED(aHResult)) {
		aHResult = oOut->GetPointer(&aOutBuf);
	}

	if (SUCCEEDED(aHResult)) {
		aInLength = oIn->GetActualDataLength();
	}
	//DebugWrite(L"Transform() aInLength [byte]: " + lexical_cast<wstring>(aInLength));

	if (SUCCEEDED(aHResult)) {
		aHResult = oOut->SetActualDataLength(aInLength);
	}

	// ���g�ϊ�
	if (SUCCEEDED(aHResult)) {
		// CWebServer �X���b�h�ɂ���ď�����������\����������̂́A����ϊ��p�̒l���L���b�V�����Ă���
		int aCachedKeyShift = mKeyShift;

		// �ϊ�
		if (mTransformable && (aCachedKeyShift != 0)) {
			// �L�[���ς�����ꍇ�́A�؂�o�����Ȃǂ��Đݒ�
			if (aCachedKeyShift != mPrevKeyShift) {
				mCutTime = aCachedKeyShift > 0 ? mCutTimeTableUp[aCachedKeyShift] : mCutTimeTableDown[-aCachedKeyShift];
				mCrossTime = aCachedKeyShift > 0 ? mCrossTimeTableUp[aCachedKeyShift] : mCrossTimeTableDown[-aCachedKeyShift];
			}

			// �����ɂ́A�Đݒ蒆�� CWebServer �X���b�h�ɂ���� mCutTime ���ύX�����̂�h���ׂ��ł��邪�A�ʓ|�������̂Ŗ���
			int aCachedCutTime = mCutTime;
			int aCachedCrossTime = mCrossTime;
			if (aCachedKeyShift != mPrevKeyShift || aCachedCutTime != mPrevCutTime || aCachedCrossTime != mPrevCrossTime) {
				SetupTransform(aCachedKeyShift, aCachedCutTime, aCachedCrossTime);
			}
			aHResult = TransformTask(aInBuf, aOutBuf, aInLength);
			mPrevKeyShift = aCachedKeyShift;
			mPrevCutTime = aCachedCutTime;
			mPrevCrossTime = aCachedCrossTime;
		}
		else {
			aHResult = TransformThrough(aInBuf, aOutBuf, aInLength);
		}
	}

	return aHResult;
}

// ============================================================================
// �A�N�Z�T�[
// ============================================================================

// ----------------------------------------------------------------------------
// mCrossTime ��������
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::SetCrossTime(int oCrossTime)
{
	if (oCrossTime < CROSS_TIME_MIN) {
		return false;
	}
	if (oCrossTime > CROSS_TIME_MAX) {
		return false;
	}
	if (oCrossTime == mCrossTime) {
		return false;
	}

	mCrossTime = oCrossTime;
	return true;
}

// ----------------------------------------------------------------------------
// mCutTime ��������
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::SetCutTime(int oCutTime)
{
	if (oCutTime < CUT_TIME_MIN) {
		return false;
	}
	if (oCutTime > CUT_TIME_MAX) {
		return false;
	}
	if (oCutTime == mCutTime) {
		return false;
	}

	mCutTime = oCutTime;
	return true;
}

// ----------------------------------------------------------------------------
// mKeyShift �ǂݏo��
// ----------------------------------------------------------------------------
int CEasyKeyChanger::KeyShift() const
{
	return mKeyShift;
}

// ----------------------------------------------------------------------------
// mKeyShift ��������
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::SetKeyShift(int oKeyShift)
{
	if (oKeyShift < KEY_SHIFT_MIN) {
		return false;
	}
	if (oKeyShift > KEY_SHIFT_MAX) {
		return false;
	}
	if (oKeyShift == mKeyShift) {
		return false;
	}

	mKeyShift = oKeyShift;
	return true;
}

// ============================================================================
// static �֐�
// ============================================================================

// ----------------------------------------------------------------------------
// �C���X�^���X�쐬�i�t�@�N�g���[�e���v���[�g�p�j
// ----------------------------------------------------------------------------
CUnknown* CEasyKeyChanger::CreateInstance(LPUNKNOWN oUnknown, HRESULT* oHResult)
{
	CEasyKeyChanger* aNewInstance = new CEasyKeyChanger(FILTER_NAME, oUnknown, oHResult);
	if (aNewInstance == NULL)
	{
		*oHResult = E_OUTOFMEMORY;
	}
	return aNewInstance;
}

// ============================================================================
// private �֐�
// ============================================================================

// ----------------------------------------------------------------------------
// ���ۂɃf�[�^����舵���邩�͕ʂƂ��āA�V�X�e���ɑ΂��Ď�舵���̈ӎv�̗L����
// �Ԃ��B�p�X�X���[�ɂȂ���̂��Ƃ��Ă��A�I�[�f�B�I�Ȃ��舵���̈ӎv��\���B
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CheckTypeCore(const CMediaType* oMediaType) const
{
	// ���W���[�^�C�v���I�[�f�B�I��
	if (*oMediaType->Type() != MEDIATYPE_Audio) {
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	return S_OK;
}

// ----------------------------------------------------------------------------
// ���f�B�A�̏����R�s�[
// ���ꂼ��̍�Ƃ����s���Ă��A�㑱�̍�Ƃ��Ȃ�ׂ��s��
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CopyHeader(IMediaSample* oIn, IMediaSample* oOut)
{
	HRESULT aHResult;

	// �X�g���[���^�C��
	REFERENCE_TIME aTimeStart, aTimeEnd;
	aHResult = oIn->GetTime(&aTimeStart, &aTimeEnd);
	if (SUCCEEDED(aHResult)) {
		aHResult = oOut->SetTime(&aTimeStart, &aTimeEnd);
	}

	// ���f�B�A�^�C��
	LONGLONG aMediaStart, aMediaEnd;
	aHResult = oIn->GetMediaTime(&aMediaStart, &aMediaEnd);
	if (SUCCEEDED(aHResult)) {
		aHResult = oOut->SetMediaTime(&aMediaStart, &aMediaEnd);
	}

	// �����|�C���g
	aHResult = oIn->IsSyncPoint();
	if (aHResult == S_OK) {
		aHResult = oOut->SetSyncPoint(true);
	}
	else if (aHResult == S_FALSE) {
		aHResult = oOut->SetSyncPoint(false);
	}

	// ���f�B�A�^�C�v
	oOut->SetMediaType(mOutputMedia);

	// �A�������
	aHResult = oIn->IsDiscontinuity();
	if (aHResult == S_OK) {
		aHResult = oOut->SetDiscontinuity(true);
	}
	else if (aHResult == S_FALSE) {
		aHResult = oOut->SetDiscontinuity(false);
	}

	return S_OK;
}

// ----------------------------------------------------------------------------
// �؂�o�����e�[�u���A�N���X�t�F�[�h���e�[�u���̏�����
// ----------------------------------------------------------------------------
void CEasyKeyChanger::InitTimeTable()
{
	// �y�S�ʓI�ȌX���z
	// ���؂�o������     �����������炩�A�~�㑫�炸�A�~�����x����
	//                    �Z�������n�L�n�L�A�������x�����A�~�����g�ł�
	// ���N���X�t�F�[�h�� ���������m�C�Y�����Ȃ��A�~�g�łi���Ƀ����O�g�[���ɂ����āj
	//                    �Z�������g�ł��Ȃ��A�~�v�`�v�`�m�C�Y

	// �y�ݒ���j�z
	// �L�[ -1 �̎��A�؂�o������ 120�`240 ���x�ŉ����ǂ��Ȃ邪�A240 �ł͔����x�����傫�����߁A
	// �؂�o������ 120 �Ƃ���B�N���X�t�F�[�h���� 15 ���ǂ��B������f�t�H���g�l�Ƃ���B
	// �L�[ -12 �ɂ����āA�f�t�H���g�l���ƃ{�[�J�����㑫�炸�ɂȂ�B
	// �؂�o���� 30�A�N���X�t�F�[�h 5 ���x���ǂ��B
	// -1�`-12 �̊Ԃ̒l�́A���Ԓl�Ƃ���B�L�[ -6 �ӂ肩��{�[�J���̐㑫�炸�����ڗ����Ƃ�
	// �l�����Ē��Ԓl��ݒ肷��B
	// �L�[���グ��ق��́A������ق��Ƃ͋t�̒������K�v�����A�����x���̖�肩��A�؂�o������
	// �f�t�H���g�l���������ł��Ȃ����߁A�؂�o�����̓f�t�H���g�l�Ƃ���B
	// �N���X�t�F�[�h���͂��قǉ����ɉe�����Ȃ��悤�ɕ������邪�A�������Ă��g�ł����͏��Ȃ�
	// �̂ŁA�傫�����Ă����B

	// �y�L�[�����F�؂�o�����z
	mCutTimeTableDown[0] = 0;
	mCutTimeTableDown[1] = 120;
	mCutTimeTableDown[2] = 120;
	mCutTimeTableDown[3] = 120;
	mCutTimeTableDown[4] = 110;
	mCutTimeTableDown[5] = 100;
	mCutTimeTableDown[6] = 90;
	mCutTimeTableDown[7] = 80;
	mCutTimeTableDown[8] = 70;
	mCutTimeTableDown[9] = 60;
	mCutTimeTableDown[10] = 50;
	mCutTimeTableDown[11] = 40;
	mCutTimeTableDown[12] = 30;

	// �y�L�[�����F�N���X�t�F�[�h���z
	mCrossTimeTableDown[0] = 0;
	mCrossTimeTableDown[1] = 15;
	mCrossTimeTableDown[2] = 15;
	mCrossTimeTableDown[3] = 15;
	mCrossTimeTableDown[4] = 15;
	mCrossTimeTableDown[5] = 15;
	mCrossTimeTableDown[6] = 15;
	mCrossTimeTableDown[7] = 15;
	mCrossTimeTableDown[8] = 13;
	mCrossTimeTableDown[9] = 11;
	mCrossTimeTableDown[10] = 9;
	mCrossTimeTableDown[11] = 7;
	mCrossTimeTableDown[12] = 5;

	// �y�L�[�グ�F�؂�o�����z
	mCutTimeTableUp[0] = 0;
	mCutTimeTableUp[1] = 120;
	mCutTimeTableUp[2] = 120;
	mCutTimeTableUp[3] = 120;
	mCutTimeTableUp[4] = 120;
	mCutTimeTableUp[5] = 120;
	mCutTimeTableUp[6] = 120;
	mCutTimeTableUp[7] = 120;
	mCutTimeTableUp[8] = 120;
	mCutTimeTableUp[9] = 120;
	mCutTimeTableUp[10] = 120;
	mCutTimeTableUp[11] = 120;
	mCutTimeTableUp[12] = 120;

	// �y�L�[�グ�F�N���X�t�F�[�h���z
	mCrossTimeTableUp[0] = 0;
	mCrossTimeTableUp[1] = 15;
	mCrossTimeTableUp[2] = 15;
	mCrossTimeTableUp[3] = 15;
	mCrossTimeTableUp[4] = 15;
	mCrossTimeTableUp[5] = 15;
	mCrossTimeTableUp[6] = 15;
	mCrossTimeTableUp[7] = 15;
	mCrossTimeTableUp[8] = 20;
	mCrossTimeTableUp[9] = 30;
	mCrossTimeTableUp[10] = 40;
	mCrossTimeTableUp[11] = 50;
	mCrossTimeTableUp[12] = 60;

}

// ----------------------------------------------------------------------------
// �Ԉ����o�b�t�@���ŏI�o�b�t�@
// ----------------------------------------------------------------------------
void CEasyKeyChanger::PartialToDest(vector<double>* oPartial, int oDoneFrames, int oThisTimeFrames, vector<int>* oDest)
{
	AssertWrite((mSrcAddPos - mSrcAddBasePos) + oThisTimeFrames <= static_cast<int>(oPartial->size()),
		L"PartialToDest() oPartial index over: " + lexical_cast<wstring>((mSrcAddPos - mSrcAddBasePos) + oThisTimeFrames));
	AssertWrite(oDoneFrames + oThisTimeFrames <= static_cast<int>(oDest->size()),
		L"PartialToDest() oDest index over: " + lexical_cast<wstring>(oDoneFrames + oThisTimeFrames));

	for (int i = 0; i < oThisTimeFrames; i++) {
		int aPartialIndex = (mSrcAddPos - mSrcAddBasePos) + i;
		int aDestIndex = oDoneFrames + i;

		if ((*oPartial)[aPartialIndex] > 32767.0) {
			(*oDest)[aDestIndex] = 32767;
		}
		else if ((*oPartial)[aPartialIndex] < -32768.0) {
			(*oDest)[aDestIndex] = -32768;
		}
		else {
			(*oDest)[aDestIndex] = static_cast<int>((*oPartial)[aPartialIndex]);
		}

	}

#ifdef DEBUGWRITEz
	wstring aData;
	for (int i = 0; i < oThisTimeFrames; i++) {
		aData += lexical_cast<wstring>((*oDest)[oDoneFrames + i]) + L",";
	}
	DebugWrite(L"PartialToDest() : " + aData);
#endif

}

// ----------------------------------------------------------------------------
// �o�̓s���̃o�b�t�@��ݒ�
// �����Őݒ肵���o�b�t�@�T�C�Y�ƁATransform() �֑����Ă���o�b�t�@�T�C�Y��
// ���֌W�̖͗l
// �����Őݒ肵���o�b�t�@�T�C�Y���傫�ȃo�b�t�@�������ɗ����ƃG���[�ɂȂ�̂ŁA
// �����ł͏����傫�߂̃o�b�t�@�T�C�Y��ݒ肵�Ă���
// ������A�����Őݒ肵���o�b�t�@�T�C�Y���ATransform() �Ŏ󂯓���\��
// �o�b�t�@�T�C�Y�̍ő�l�ƂȂ�
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::SetupOutputBuffer(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties)
{
	DebugWrite(L"SetupOutputBuffer()");

	// �y�[�W��
	oProperties->cBuffers = NUM_TRANSFORM_BUFFERS;

	// �o�b�t�@�T�C�Y
	if (mTransformable) {
		// 0.5 �b���̃o�b�t�@���m��
		WAVEFORMATEX* aWaveEx = reinterpret_cast<WAVEFORMATEX*>(mOutputMedia->Format());
		mMaxOutputFrames = static_cast<int>(0.5 * aWaveEx->nSamplesPerSec);
		oProperties->cbBuffer = mMaxOutputFrames * aWaveEx->nChannels * aWaveEx->wBitsPerSample / 8;
	}
	else {
		oProperties->cbBuffer = NO_TRANSFORM_BUF_SIZE;
	}

	// �m��
	ALLOCATOR_PROPERTIES aActual;
	HRESULT aHResult = S_OK;
	if (SUCCEEDED(aHResult)) {
		aHResult = oAlloc->SetProperties(oProperties, &aActual);
	}

	// �m�ۂ��ꂽ���̃`�F�b�N
	if (SUCCEEDED(aHResult)) {
		if (aActual.cBuffers < oProperties->cBuffers || aActual.cbBuffer < oProperties->cbBuffer) {
			aHResult = E_FAIL;
		}
	}

#ifdef DEBUGWRITE
	if (SUCCEEDED(aHResult)) {
		DebugWrite(L"SetupOutputBuffer() OK aActual.cBuffers: " + lexical_cast<wstring>(aActual.cBuffers));
		DebugWrite(L"SetupOutputBuffer() OK aActual.cbBuffer: " + lexical_cast<wstring>(aActual.cbBuffer));
	}
#endif

	return aHResult;
}

// ----------------------------------------------------------------------------
// �o�̓��f�B�A�^�C�v���쐬
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::SetupOutputMedia(const CMediaType& oMtIn)
{
	mOutputMedia = new CMediaType(oMtIn);

	// �o�͌`���ϊ�
	mOutputMedia->SetSubtype(&MEDIASUBTYPE_PCM);

	return S_OK;
}

// ----------------------------------------------------------------------------
// �ϊ��p�̐ݒ�i�L�[�Ȃǂ��ς�邲�Ƃɐݒ肪�ς����́j
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SetupTransform(int oNewKey, int oNewCutTime, int oNewCrossTime)
{
	// �؂�o���� [Frame] �����߂�
	mCutFrames = static_cast<int>(round(oNewCutTime / 1000.0 * mWaveFormat.nSamplesPerSec));
	DebugWrite(L"SetupTransformPre() mCutFrames: " + lexical_cast<wstring>(mCutFrames));

	// �s�b�`�̊g��k�����i�I�N������0.5�A�I�N�グ��2�A1 �L�[�グ��1.059�j
	mScale = pow(2, static_cast<double>(oNewKey) / 12);
	//DebugWrite(L"SetupTransform() mScale: " + lexical_cast<wstring>(mScale));

	// �V�t�g�� [Frame] �����߂�
	mShiftFrames = static_cast<int>(round(oNewCutTime / 1000.0 * mWaveFormat.nSamplesPerSec / mScale));

	// ��x�̕ϊ��� mCutFrames ���̉����f�[�^���g���̂ŁA�����ɉ����f�[�^��ǉ�����ʒu�� mCutFrames �ȍ~�Ƃ���
	mSrcAddBasePos = mSrcAddPos = mCutFrames;

	// �N���X�t�F�[�h�� [Frame]
	int aCrossFrames = static_cast<int>(round(oNewCrossTime / 1000.0 * mWaveFormat.nSamplesPerSec));
	DebugWrite(L"SetupTransformCrossTime() aCrossFrames: " + lexical_cast<wstring>(aCrossFrames));

	// �N���X�t�F�[�h�o�b�t�@
	mCrossL.resize(aCrossFrames);
	mCrossR.resize(aCrossFrames);

	// �N���X�t�F�[�h�p���֐��̐ݒ�
	mWin.resize(aCrossFrames * 2);

	for (int i = 0; i < aCrossFrames * 2; i++) {
		mWin[i] = 0.54 - 0.46 * cos(2 * M_PI * i / (aCrossFrames * 2));	// �n�~���O��
																		//mWin[i] = 0.5 - 0.5 * cos(2 * M_PI * i / (aCrossFrames * 2));	// �n����
#if 0
																		// �O�p�g
		if (i < aCrossFrames) {
			mWin[i] = static_cast<double>(i) / aCrossFrames;
		}
		else {
			mWin[i] = static_cast<double>(2 * aCrossFrames - i) / aCrossFrames;
		}
#endif
	}

#ifdef DEBUGWRITEz
	wstring aWin;
	for (int i = 0; i < static_cast<int>(mWin.size()); i++) {
		aWin += lexical_cast<wstring>(mWin[i]) + L"\n";
	}
	DebugWrite(L"SetupTransform() win len: " + lexical_cast<wstring>(mWin.size()) + L":\n" + aWin);
#endif

}

// ----------------------------------------------------------------------------
// �ϊ��p�̐ݒ�i�ŏ��ɐݒ肵�Ĉȍ~�ς��Ȃ����́j
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SetupTransformPre()
{
	// ���̉����f�[�^��ێ����钷�� [Frame] �����߂�
	// �{���I�ɂ� Transform() �ł̎�M�ʂɈˑ����邪�ATransform() ���܂����Œl��ێ�����
	// �K�v�����邽�߁ATransform() �̓x�ɒ�����ς���킯�ɂ͂����Ȃ�
	// �����ŁA�\�߁AmMaxOutputFrames ����ɃA���P�[�g���Ă���
	// ��M�ʂ� mMaxOutputFrames �������ꍇ�A���� 2 �����m�ۂ��Ă����΁A
	// �Ȃ�ƂȂ������̂ł͂Ȃ����A���x�̍���
	int aSrcLen = mMaxOutputFrames * 2;
	DebugWrite(L"SetupTransformPre() aSrcLen: " + lexical_cast<wstring>(aSrcLen));
	mSrcL.resize(aSrcLen);
	mSrcR.resize(aSrcLen);

	// �L����̃f�[�^���i�[����o�b�t�@
	// �s�b�`�� 2 �{�̎��A�\�[�X�� 2 �{�̒������K�v�ɂȂ�̂��ő�l
	int aStrechLen = aSrcLen * 2;
	DebugWrite(L"SetupTransformPre() aStrechLen: " + lexical_cast<wstring>(aStrechLen));
	mStrechL.resize(aStrechLen);
	mStrechR.resize(aStrechLen);

	// �Ԉ�����̃f�[�^���i�[����o�b�t�@
	// �\�[�X�Ɠ����������ő�l
	mPartialL.resize(aSrcLen);
	mPartialR.resize(aSrcLen);

}

// ----------------------------------------------------------------------------
// �\�[�X�o�b�t�@��O���� mShiftFrames �V�t�g
// ----------------------------------------------------------------------------
void CEasyKeyChanger::ShiftSrc()
{
#if 0
	DebugWrite(L"ShiftSrc() mSrcAddBasePos: " + lexical_cast<wstring>(mSrcAddBasePos));
	DebugWrite(L"ShiftSrc() mSrcAddPos: " + lexical_cast<wstring>(mSrcAddPos));
	DebugWrite(L"ShiftSrc() mShiftFrames: " + lexical_cast<wstring>(mShiftFrames));
	DebugWrite(L"ShiftSrc() mShiftFrames * 2 (len): " + lexical_cast<wstring>(mShiftFrames * 2));
	DebugWrite(L"ShiftSrc() last: " + lexical_cast<wstring>(mSrcAddBasePos + mShiftFrames * 2));
	DebugWrite(L"ShiftSrc() mShiftFrames + mCutFrames + static_cast<int>(mCrossL.size()): " + lexical_cast<wstring>(mShiftFrames + mCutFrames + static_cast<int>(mCrossL.size())));
#endif

	// �����N���X�t�F�[�h�̕ۑ�
	AssertWrite(mCutFrames + static_cast<int>(mCrossL.size()) <= static_cast<int>(mSrcL.size()),
		L"SrcToStrech() tail cross oSrc index over: " + lexical_cast<wstring>(mCutFrames + static_cast<int>(mCrossL.size())));
	AssertWrite(static_cast<int>(mCrossL.size() + mCrossL.size()) <= static_cast<int>(mWin.size()),
		L"SrcToStrech() tail cross mWin index over: " + lexical_cast<wstring>(static_cast<int>(mCrossL.size() + mCrossL.size())));
	for (int i = 0; i < static_cast<int>(mCrossL.size()); i++) {
		mCrossL[i] = mSrcL[mCutFrames + i] * mWin[static_cast<int>(mCrossL.size()) + i];
		mCrossR[i] = mSrcR[mCutFrames + i] * mWin[static_cast<int>(mCrossL.size()) + i];
	}

	// �V�t�g
	for (int i = 0; i < mCutFrames; i++) {
		mSrcL[i] = mSrcL[i + mShiftFrames];
		mSrcR[i] = mSrcR[i + mShiftFrames];
	}
	mSrcAddPos = mSrcAddBasePos;

	// �L��
	SrcToStrech(&mSrcL, &mStrechL, &mCrossL);
	SrcToStrech(&mSrcR, &mStrechR, &mCrossR);

	// �Ԉ���
	StrechToPartial(&mStrechL, &mPartialL);
	StrechToPartial(&mStrechR, &mPartialR);

#ifdef DEBUGWRITEz
	wstring src0;
	for (int i = 0; i < mShiftFrames + mCutFrames + static_cast<int>(mCrossL.size()); i++) {
		src0 += lexical_cast<wstring>(mSrcL[i]) + L",";
	}
	DebugWrite(L"ShiftSrc() src0: " + src0);
#endif

}

// ----------------------------------------------------------------------------
// �\�[�X�o�b�t�@���L���o�b�t�@�փR�s�[
// ��� mCutFrames�{�N���X�t�F�[�h���R�s�[����
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SrcToStrech(vector<int>* oSrc, vector<double>* oStrech, vector<double>* oCross)
{
	// �擪�N���X�t�F�[�h�i�����N���X�t�F�[�h�Ƃ̍����j
	AssertWrite(oCross->size() <= oStrech->size(),
		L"SrcToStrech() head cross oStrech index over: " + lexical_cast<wstring>(oCross->size()));
	AssertWrite(oCross->size() <= oSrc->size(),
		L"SrcToStrech() head cross oSrc index over: " + lexical_cast<wstring>(oCross->size()));
	AssertWrite(oCross->size() <= mWin.size(),
		L"SrcToStrech() head cross mWin index over: " + lexical_cast<wstring>(oCross->size()));
	for (int i = 0; i < static_cast<int>(oCross->size()); i++) {
		(*oStrech)[i] = (*oCross)[i] + (*oSrc)[i] * mWin[i];
	}

	// ����
	AssertWrite(mCutFrames <= static_cast<int>(oStrech->size()),
		L"SrcToStrech() mid oStrech index over: " + lexical_cast<wstring>(mCutFrames));
	AssertWrite(mCutFrames <= static_cast<int>(oSrc->size()),
		L"SrcToStrech() mid oSrc index over: " + lexical_cast<wstring>(mCutFrames));
	for (int i = static_cast<int>(oCross->size()); i < mCutFrames; i++) {
		(*oStrech)[i] = (*oSrc)[i];
	}
}

// ----------------------------------------------------------------------------
// �L���o�b�t�@���Ԉ����o�b�t�@�ɏW��
// ----------------------------------------------------------------------------
void CEasyKeyChanger::StrechToPartial(vector<double>* oStrech, vector<double>* oPartial)
{
	AssertWrite(mShiftFrames <= static_cast<int>(oPartial->size()),
		L"StrechToPartial() oPartial index over: " + lexical_cast<wstring>(mShiftFrames));
	AssertWrite(static_cast<int>(mShiftFrames * mScale) <= static_cast<int>(oStrech->size()),
		L"StrechToPartial() oStrech index over: " + lexical_cast<wstring>(static_cast<int>(mShiftFrames * mScale)));
	for (int i = 0; i < mShiftFrames; i++) {
		int aStrechIndex = static_cast<int>(i * mScale);
		(*oPartial)[i] = (*oStrech)[aStrechIndex];
	}
}

// ----------------------------------------------------------------------------
// 1 �؂�o�������i�����̂��Ƃ�����j��ϊ����� mDestL, mDestR �Ɋi�[����
// �K�v�ȕ����̂݃f�X�e�B�l�[�V�����ɃR�s�[
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformOneCut(int oDoneFrames, int oThisTimeFrames)
{
	// �ŏI�o�b�t�@��
	PartialToDest(&mPartialL, oDoneFrames, oThisTimeFrames, &mDestL);
	PartialToDest(&mPartialR, oDoneFrames, oThisTimeFrames, &mDestR);

	return S_OK;
}

// ----------------------------------------------------------------------------
// �t�B���^�[�ϊ��i�L�[�`�F���W�j
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformTask(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen)
{
	DebugWrite(L"TransformTask()");
#ifdef DEBUGWRITE
	// �ϊ��p�t�H�[�}���X����
	// ��������Fflower of sorrow 0:10��0:40 �̖� 30 �b�ԃL�[ 1 ����
	// Ver 1.03: 1,310 ��Aave 407 us
	// Ver 1.04: 1,305 ��Aave 224 us
	// Ver 1.05: 1,301 ��Aave 134 us
	static LONGLONG saTotalDealTime = 0;
	static long saTotalDealCount = 0;
	LARGE_INTEGER aBeginCounter;
	QueryPerformanceCounter(&aBeginCounter);
#endif

	// �����ς݂̗� [Frame]
	int aDoneFrames = 0;

	// �T�C�Y�v�Z
	int aBlockSize = (mWaveFormat.wBitsPerSample / 8) * mWaveFormat.nChannels;
	int aTotalFrames = oBufLen / aBlockSize;
	//DebugWrite(L"TransformTask() ���� aTotalFrames: " + lexical_cast<wstring>(aTotalFrames));

	// �o�b�t�@
	mDestL.resize(aTotalFrames);
	mDestR.resize(aTotalFrames);
	short aShort;

	// ���̉����f�[�^���\�[�X�o�b�t�@�ɒǉ����Ȃ���A�s�x�ϊ�
	// mSrcAddBasePos ����ǉ�����̂��ʗႾ���A�r������ǉ��A�Ƃ����̂����蓾��
	while (aDoneFrames < aTotalFrames) {
		// ����̃��[�v�Œǉ�����ʂ����肷��
		int aSpaceFrames = mShiftFrames - (mSrcAddPos - mSrcAddBasePos);
		int aThisTimeFrames;
		if (aTotalFrames - aDoneFrames >= aSpaceFrames) {
			aThisTimeFrames = aSpaceFrames;
		}
		else {
			aThisTimeFrames = aTotalFrames - aDoneFrames;
		}
#if 0
		DebugWrite(L"TransformTask() aDoneFrames: " + lexical_cast<wstring>(aDoneFrames));
		DebugWrite(L"TransformTask() aTotalFrames: " + lexical_cast<wstring>(aTotalFrames));
		DebugWrite(L"TransformTask() mSrcAddPos: " + lexical_cast<wstring>(mSrcAddPos));
		DebugWrite(L"TransformTask() aThisTimeFrames: " + lexical_cast<wstring>(aThisTimeFrames));
#endif

		// ���̉����f�[�^���\�[�X�o�b�t�@�ɒǉ�
		AssertWrite((aDoneFrames + aThisTimeFrames - 1) * aBlockSize + (mWaveFormat.wBitsPerSample / 8) < oBufLen,
			L"TransformTask() input oInBuf index over: " + lexical_cast<wstring>((aDoneFrames + aThisTimeFrames - 1) * aBlockSize + (mWaveFormat.wBitsPerSample / 8)));
		AssertWrite(mSrcAddPos + aThisTimeFrames <= static_cast<int>(mSrcL.size()),
			L"TransformTask() input mSrcL index over: " + lexical_cast<wstring>(mSrcAddPos + aThisTimeFrames));
		for (int i = 0; i < aThisTimeFrames; i++) {
			// L
			CopyMemory(&aShort, oInBuf + (aDoneFrames + i) * aBlockSize, 2);
			mSrcL[mSrcAddPos + i] = aShort;

			// R
			CopyMemory(&aShort, oInBuf + (aDoneFrames + i) * aBlockSize + (mWaveFormat.wBitsPerSample / 8), 2);
			mSrcR[mSrcAddPos + i] = aShort;
		}

#ifdef DEBUGWRITEz
		wstring src0;
		for (int i = 0; i < mShiftFrames + mCutFrames + static_cast<int>(mCrossL.size()); i++) {
			src0 += lexical_cast<wstring>(mSrcL[i]) + L",";
		}
		DebugWrite(L"TransformTask() src0: " + src0);
#endif

		// �ϊ�
		TransformOneCut(aDoneFrames, aThisTimeFrames);

		// �ʒu�̕ύX
		aDoneFrames += aThisTimeFrames;
		mSrcAddPos += aThisTimeFrames;

		if (mSrcAddPos >= mSrcAddBasePos + mShiftFrames) {
			ShiftSrc();
		}
	}

	// �o��
	AssertWrite(aTotalFrames <= static_cast<int>(mDestL.size()),
		L"TransformTask() output mDestL index over: " + lexical_cast<wstring>(aTotalFrames));
	AssertWrite((aTotalFrames - 1) * aBlockSize + (mWaveFormat.wBitsPerSample / 8) < oBufLen,
		L"TransformTask() output oOutBuf index over: " + lexical_cast<wstring>((aTotalFrames - 1) * aBlockSize + (mWaveFormat.wBitsPerSample / 8)));
	for (int i = 0; i < aTotalFrames; i++) {
		// L
		aShort = mDestL[i];
		CopyMemory(oOutBuf + i * aBlockSize, &aShort, 2);

		// R
		aShort = mDestR[i];
		CopyMemory(oOutBuf + i * aBlockSize + (mWaveFormat.wBitsPerSample / 8), &aShort, 2);
	}

#ifdef DEBUGWRITE
	LARGE_INTEGER aEndCounter;
	QueryPerformanceCounter(&aEndCounter);
	saTotalDealTime += aEndCounter.QuadPart - aBeginCounter.QuadPart;
	saTotalDealCount++;
	DebugWrite(L"TransformTask() deal count: " + lexical_cast<wstring>(saTotalDealCount) + L", ave time [us]: " + lexical_cast<wstring>(saTotalDealTime / saTotalDealCount));
#endif
	DebugWrite(L"TransformTask() OK");

	return S_OK;
}

// ----------------------------------------------------------------------------
// �t�B���^�[�ϊ��i�ϊ������Ƀp�X�X���[�j
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformThrough(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen)
{
	DebugWrite(L"TransformThrough()");

	CopyMemory(oOutBuf, oInBuf, oBufLen);

	return S_OK;
}





