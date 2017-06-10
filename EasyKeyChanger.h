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
// DllMain.cpp からも参照される定数
// ============================================================================

// ----------------------------------------------------------------------------
// フィルター関連
// ----------------------------------------------------------------------------

// フィルター名（wchar_t）
#ifdef _M_X64
#define FILTER_NAME	L"Easy Key Changer x64 (64bit)"
#else
#define FILTER_NAME	L"Easy Key Changer x86 (32bit)"
#endif

// フィルタークラス ID
// {28948BE5-05CB-4386-80FF-1805EF781731}
static const GUID CLSID_EasyKeyChanger =
{ 0x28948be5, 0x5cb, 0x4386,{ 0x80, 0xff, 0x18, 0x5, 0xef, 0x78, 0x17, 0x31 } };

// ============================================================================
// クラス宣言
// ============================================================================

class CEasyKeyChanger : public CTransformFilter
{
public:
	// ============================================================================
	// public 定数
	// ============================================================================

	// mCrossTime 範囲
	const int CROSS_TIME_MIN = 0;
	const int CROSS_TIME_MAX = 120;

	// mCutTime 範囲
	const int CUT_TIME_MIN = 10;
	const int CUT_TIME_MAX = 250;

	// mKeyShift 範囲
	static const int KEY_SHIFT_MIN = -12;
	static const int KEY_SHIFT_MAX = +12;

	// ============================================================================
	// public 関数
	// ============================================================================

	// ----------------------------------------------------------------------------
	// コンストラクター・デストラクター
	// ----------------------------------------------------------------------------
	CEasyKeyChanger(TCHAR* oName, LPUNKNOWN oUnknown, HRESULT* oHResult);
	virtual ~CEasyKeyChanger();

	// ----------------------------------------------------------------------------
	// IUnknown から継承
	// ----------------------------------------------------------------------------
	DECLARE_IUNKNOWN;

	// ----------------------------------------------------------------------------
	// CUnknown から継承
	// ----------------------------------------------------------------------------
	virtual STDMETHODIMP NonDelegatingQueryInterface(REFIID oRIid, void** oInterface) override;

	// ----------------------------------------------------------------------------
	// CTransformFilter から継承
	// ----------------------------------------------------------------------------
	virtual	HRESULT CheckInputType(const CMediaType* oMtIn) override;
	virtual	HRESULT CheckTransform(const CMediaType* oMtIn, const CMediaType* oMtOut) override;
	virtual	HRESULT CompleteConnect(PIN_DIRECTION oDirection, IPin* oReceivePin) override;
	virtual HRESULT DecideBufferSize(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties) override;
	virtual	HRESULT GetMediaType(int oPosition, CMediaType* oMediaType) override;
	virtual HRESULT Receive(IMediaSample* oIn) override;
	virtual HRESULT Transform(IMediaSample* oIn, IMediaSample *oOut) override;

	// ----------------------------------------------------------------------------
	// アクセサー
	// ----------------------------------------------------------------------------
	bool SetCrossTime(int oCrossTime);

	bool SetCutTime(int oCutTime);

	int KeyShift() const;
	bool SetKeyShift(int oKeyShift);

	// ----------------------------------------------------------------------------
	// static 関数
	// ----------------------------------------------------------------------------
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN oUnknown, HRESULT* oHResult);

private:
	typedef CTransformFilter BASE;

	// ============================================================================
	// private 定数
	// ============================================================================

	// ステレオのチャンネル数
	const int NUM_CHANNEL_2 = 2;

	// ビット深度 16
	const int BITS_PER_SAMPLE_16 = 16;

	// 変換用バッファの個数
	const int NUM_TRANSFORM_BUFFERS = 2;

	// 変換しない場合の Transform() 用バッファサイズ
	const int NO_TRANSFORM_BUF_SIZE = 1024 * 50;

	// 出力ピンのバッファ長 [1/s]
	const int OUTPUT_BUFFER_TIME_DIV = 10;

	// ============================================================================
	// private 変数
	// ============================================================================

	// 出力メディア情報（アロケートされたものを保持）
	CMediaType* mOutputMedia;

	// 出力ピンに送信できるフレーム数の最大値
	int mMaxOutputFrames;

	// 変換可能かどうか
	bool mTransformable;

	// 変換できない理由
	wstring mNoTransformReason;

	// キーシフト量（KEY_SHIFT_MIN ～ KEY_SHIFT_MAX）
	volatile int mKeyShift;

	// キーごとの音声データの切り出し幅 [s]
	int mCutTimeTableDown[-KEY_SHIFT_MIN + 1];
	int mCutTimeTableUp[KEY_SHIFT_MAX + 1];

	// キーごとのクロスフェードの幅 [s]
	int mCrossTimeTableDown[-KEY_SHIFT_MIN + 1];
	int mCrossTimeTableUp[KEY_SHIFT_MAX + 1];

	// 前回変換時のキーシフト量
	int mPrevKeyShift;

	// 前回変換時の切り出し幅 [s]
	int mPrevCutTime;

	// 前回変換時のクロスフェード幅 [s]
	int mPrevCrossTime;

	// 出力メディアの WAVE フォーマット
	WAVEFORMATEX mWaveFormatOut;

	// 指示待ち用サーバー
	CWebServer* mWebServer;

#ifdef DEBUGWRITE
	CEasyKeyChanger* mInitialThis;
#endif

	// ----------------------------------------------------------------------------
	// 変換処理用
	// ----------------------------------------------------------------------------

	// ソースバッファ：元の音声データを変換に必要な長さ（はみ出し保険含む）だけ保持（1 フレームで 1 要素）
	vector<int> mSrcL;
	vector<int> mSrcR;

	// 最初にソースバッファに音声を書き込む位置
	int mSrcAddBasePos;

	// ソースバッファに音声を書き込む位置
	int mSrcAddPos;

	// ピッチの拡大縮小率
	double mScale;

	// 切り出し幅設定 [ms]
	volatile int mCutTime;

	// 切り出し幅 [Frame]
	volatile int mCutFrames;

	// シフト長 [Frame]
	int mShiftFrames;

	// 伸張後のデータを格納するバッファ
	vector<double> mStrechL;
	vector<double> mStrechR;

	// 間引き後のデータを格納するバッファ
	vector<double> mPartialL;
	vector<double> mPartialR;

	// 最終形を（int で）格納するバッファ
	vector<int> mDestL;
	vector<int> mDestR;

	// クロスフェード幅設定 [ms]
	int mCrossTime;

	// クロスフェード用窓関数の値を格納するバッファ
	vector<double> mWin;

	// クロスフェード用の端切れを次回に持ち越すバッファ
	vector<double> mCrossL;
	vector<double> mCrossR;

	// ============================================================================
	// private 関数
	// ============================================================================

	// 取り扱えるメディアタイプかどうか確認
	HRESULT CheckTypeCore(const CMediaType* oMediaType) const;

	// メディアの情報をコピー
	HRESULT CopyHeader(IMediaSample* oIn, IMediaSample* oOut);

	// 不正な音声情報を修正
	bool FixBadWaveFormat(WAVEFORMATEX* oWaveFormat);

	// 切り出し幅テーブル、クロスフェードテーブルの初期化
	void InitTimeTable();

	// 間引きバッファ→最終バッファ
	void PartialToDest(vector<double>* oPartial, int oDoneFrames, int oThisTimeFrames, vector<int>* oDest);

	// 出力ピンのバッファを設定
	HRESULT SetupOutputBuffer(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties);

	// 出力メディア情報を設定
	HRESULT SetupOutputMedia(const CMediaType& oMtIn);

	// 変換用の設定（設定が変わるもの）
	void SetupTransform(int oNewKey, int oNewCutTime, int oNewCrossTime);

	// 変換用の設定（最初に設定して以降変わらないもの）
	void SetupTransformPre();

	// ソースバッファを前方にシフト
	void ShiftSrc();

	// ソースバッファ→伸張バッファへコピー
	void SrcToStrech(vector<int>* oSrc, vector<double>* oStrech, vector<double>* oCross);

	// 伸張バッファ→間引きバッファに集約
	void StrechToPartial(vector<double>* oStrech, vector<double>* oPartial);

	// 1 切り出し幅分（未満のこともある）を変換
	HRESULT TransformOneCut(int oDoneFrames, int oThisTimeFrames);

	// フィルター変換（キーチェンジ）
	HRESULT TransformTask(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen);

	// フィルター変換（変換せずにパススルー）
	HRESULT TransformThrough(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen);

};
// ============================================================================


