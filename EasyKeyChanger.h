// ----------------------------------------------------------------------------
#pragma once
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Windows
#include <Streams.h>
#include <Transfrm.h>
// ----------------------------------------------------------------------------
// Project
#include "WebServer.h"
// ----------------------------------------------------------------------------
// C++
#include <map>
#include <string>
#include <vector>
// ----------------------------------------------------------------------------
using namespace std;
// ----------------------------------------------------------------------------

// ============================================================================
// DllMain.cpp ������Q�Ƃ����萔
// ============================================================================

// ----------------------------------------------------------------------------
// �t�B���^�[�֘A
// ----------------------------------------------------------------------------

// �t�B���^�[���iwchar_t�j
#ifdef _M_X64
#define FILTER_NAME	L"Easy Key Changer x64 (64bit)"
#else
#define FILTER_NAME	L"Easy Key Changer x86 (32bit)"
#endif

// �t�B���^�[�N���X ID
// {28948BE5-05CB-4386-80FF-1805EF781731}
static const GUID CLSID_EasyKeyChanger =
{ 0x28948be5, 0x5cb, 0x4386,{ 0x80, 0xff, 0x18, 0x5, 0xef, 0x78, 0x17, 0x31 } };

// ============================================================================
// �N���X�錾
// ============================================================================

class CEasyKeyChanger : public CTransformFilter
{
public:
	// ============================================================================
	// public �萔
	// ============================================================================

	// mCrossTime �͈�
	const int CROSS_TIME_MIN = 0;
	const int CROSS_TIME_MAX = 120;

	// mCutTime �͈�
	const int CUT_TIME_MIN = 10;
	const int CUT_TIME_MAX = 250;

	// mKeyShift �͈�
	static const int KEY_SHIFT_MIN = -12;
	static const int KEY_SHIFT_MAX = +12;

	// ============================================================================
	// public �֐�
	// ============================================================================

	// ----------------------------------------------------------------------------
	// �R���X�g���N�^�[�E�f�X�g���N�^�[
	// ----------------------------------------------------------------------------
	CEasyKeyChanger(TCHAR* oName, LPUNKNOWN oUnknown, HRESULT* oHResult);
	virtual ~CEasyKeyChanger();

	// ----------------------------------------------------------------------------
	// IUnknown ����p��
	// ----------------------------------------------------------------------------
	DECLARE_IUNKNOWN;

	// ----------------------------------------------------------------------------
	// CUnknown ����p��
	// ----------------------------------------------------------------------------
	virtual STDMETHODIMP NonDelegatingQueryInterface(REFIID oRIid, void** oInterface) override;

	// ----------------------------------------------------------------------------
	// CTransformFilter ����p��
	// ----------------------------------------------------------------------------
	virtual	HRESULT CheckInputType(const CMediaType* oMtIn) override;
	virtual	HRESULT CheckTransform(const CMediaType* oMtIn, const CMediaType* oMtOut) override;
	virtual	HRESULT CompleteConnect(PIN_DIRECTION oDirection, IPin* oReceivePin) override;
	virtual HRESULT DecideBufferSize(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties) override;
	virtual	HRESULT GetMediaType(int oPosition, CMediaType* oMediaType) override;
	virtual HRESULT Receive(IMediaSample* oIn) override;
	virtual HRESULT Transform(IMediaSample* oIn, IMediaSample *oOut) override;

	// ----------------------------------------------------------------------------
	// �A�N�Z�T�[
	// ----------------------------------------------------------------------------
	bool SetCrossTime(int oCrossTime);

	bool SetCutTime(int oCutTime);

	int KeyShift() const;
	bool SetKeyShift(int oKeyShift);

	// ----------------------------------------------------------------------------
	// static �֐�
	// ----------------------------------------------------------------------------
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN oUnknown, HRESULT* oHResult);

private:
	typedef CTransformFilter BASE;

	// ============================================================================
	// private �萔
	// ============================================================================

	// �X�e���I�̃`�����l����
	const int NUM_CHANNEL_2 = 2;

	// �r�b�g�[�x 16
	const int BITS_PER_SAMPLE_16 = 16;

	// �ϊ��p�o�b�t�@�̌�
	const int NUM_TRANSFORM_BUFFERS = 2;

	// �ϊ����Ȃ��ꍇ�� Transform() �p�o�b�t�@�T�C�Y
	const int NO_TRANSFORM_BUF_SIZE = 1024 * 50;

	// �o�̓s���̃o�b�t�@�� [1/s]
	const int OUTPUT_BUFFER_TIME_DIV = 10;

	// ============================================================================
	// private �ϐ�
	// ============================================================================

	// �o�̓��f�B�A���i�A���P�[�g���ꂽ���̂�ێ��j
	CMediaType* mOutputMedia;

	// �o�̓s���ɑ��M�ł���t���[�����̍ő�l
	int mMaxOutputFrames;

	// �ϊ��\���ǂ���
	bool mTransformable;

	// �ϊ��ł��Ȃ����R
	wstring mNoTransformReason;

	// �L�[�V�t�g�ʁiKEY_SHIFT_MIN �` KEY_SHIFT_MAX�j
	volatile int mKeyShift;

	// �L�[���Ƃ̉����f�[�^�̐؂�o���� [s]
	int mCutTimeTableDown[-KEY_SHIFT_MIN + 1];
	int mCutTimeTableUp[KEY_SHIFT_MAX + 1];

	// �L�[���Ƃ̃N���X�t�F�[�h�̕� [s]
	int mCrossTimeTableDown[-KEY_SHIFT_MIN + 1];
	int mCrossTimeTableUp[KEY_SHIFT_MAX + 1];

	// �O��ϊ����̃L�[�V�t�g��
	int mPrevKeyShift;

	// �O��ϊ����̐؂�o���� [s]
	int mPrevCutTime;

	// �O��ϊ����̃N���X�t�F�[�h�� [s]
	int mPrevCrossTime;

	// �o�̓��f�B�A�� WAVE �t�H�[�}�b�g
	WAVEFORMATEX mWaveFormatOut;

	// �w���҂��p�T�[�o�[
	CWebServer* mWebServer;

#ifdef DEBUGWRITE
	CEasyKeyChanger* mInitialThis;
#endif

	// ----------------------------------------------------------------------------
	// �ϊ������p
	// ----------------------------------------------------------------------------

	// �\�[�X�o�b�t�@�F���̉����f�[�^��ϊ��ɕK�v�Ȓ����i�͂ݏo���ی��܂ށj�����ێ��i1 �t���[���� 1 �v�f�j
	vector<int> mSrcL;
	vector<int> mSrcR;

	// �ŏ��Ƀ\�[�X�o�b�t�@�ɉ������������ވʒu
	int mSrcAddBasePos;

	// �\�[�X�o�b�t�@�ɉ������������ވʒu
	int mSrcAddPos;

	// �s�b�`�̊g��k����
	double mScale;

	// �؂�o�����ݒ� [ms]
	volatile int mCutTime;

	// �؂�o���� [Frame]
	volatile int mCutFrames;

	// �V�t�g�� [Frame]
	int mShiftFrames;

	// �L����̃f�[�^���i�[����o�b�t�@
	vector<double> mStrechL;
	vector<double> mStrechR;

	// �Ԉ�����̃f�[�^���i�[����o�b�t�@
	vector<double> mPartialL;
	vector<double> mPartialR;

	// �ŏI�`���iint �Łj�i�[����o�b�t�@
	vector<int> mDestL;
	vector<int> mDestR;

	// �N���X�t�F�[�h���ݒ� [ms]
	int mCrossTime;

	// �N���X�t�F�[�h�p���֐��̒l���i�[����o�b�t�@
	vector<double> mWin;

	// �N���X�t�F�[�h�p�̒[�؂������Ɏ����z���o�b�t�@
	vector<double> mCrossL;
	vector<double> mCrossR;

	// ============================================================================
	// private �֐�
	// ============================================================================

	// ��舵���郁�f�B�A�^�C�v���ǂ����m�F
	HRESULT CheckTypeCore(const CMediaType* oMediaType) const;

	// ���f�B�A�̏����R�s�[
	HRESULT CopyHeader(IMediaSample* oIn, IMediaSample* oOut);

	// �s���ȉ��������C��
	bool FixBadWaveFormat(WAVEFORMATEX* oWaveFormat);

	// �؂�o�����e�[�u���A�N���X�t�F�[�h�e�[�u���̏�����
	void InitTimeTable();

	// �Ԉ����o�b�t�@���ŏI�o�b�t�@
	void PartialToDest(vector<double>* oPartial, int oDoneFrames, int oThisTimeFrames, vector<int>* oDest);

	// �o�̓s���̃o�b�t�@��ݒ�
	HRESULT SetupOutputBuffer(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties);

	// �o�̓��f�B�A����ݒ�
	HRESULT SetupOutputMedia(const CMediaType& oMtIn);

	// �ϊ��p�̐ݒ�i�ݒ肪�ς����́j
	void SetupTransform(int oNewKey, int oNewCutTime, int oNewCrossTime);

	// �ϊ��p�̐ݒ�i�ŏ��ɐݒ肵�Ĉȍ~�ς��Ȃ����́j
	void SetupTransformPre();

	// �\�[�X�o�b�t�@��O���ɃV�t�g
	void ShiftSrc();

	// �\�[�X�o�b�t�@���L���o�b�t�@�փR�s�[
	void SrcToStrech(vector<int>* oSrc, vector<double>* oStrech, vector<double>* oCross);

	// �L���o�b�t�@���Ԉ����o�b�t�@�ɏW��
	void StrechToPartial(vector<double>* oStrech, vector<double>* oPartial);

	// 1 �؂�o�������i�����̂��Ƃ�����j��ϊ�
	HRESULT TransformOneCut(int oDoneFrames, int oThisTimeFrames);

	// �t�B���^�[�ϊ��i�L�[�`�F���W�j
	HRESULT TransformTask(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen);

	// �t�B���^�[�ϊ��i�ϊ������Ƀp�X�X���[�j
	HRESULT TransformThrough(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen);

};
// ============================================================================


