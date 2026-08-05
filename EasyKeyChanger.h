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

	// 変換可能かどうか
	bool Transformable() const;

	// 変換できない理由
	wstring NoTransformReason();

	// 現在の入力フォーマットの要約（診断用）
	wstring InputFormatSummary();

	// ----------------------------------------------------------------------------
	// static 関数
	// ----------------------------------------------------------------------------
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN oUnknown, HRESULT* oHResult);

private:
	typedef CTransformFilter BASE;

	// ============================================================================
	// private 定数
	// ============================================================================

	// 対応する最大チャンネル数
	const int MAX_NUM_CHANNELS = 8;

	// 変換用バッファの個数
	const int NUM_TRANSFORM_BUFFERS = 2;

	// 対応する最大サンプリングレート（出力ピンのバッファ確保用）
	// 再生中の音声トラック変更でサンプリングレートが高くなった場合でも
	// 受け入れられるようにするためのもの
	const DWORD MAX_SAMPLES_PER_SEC = 384000;

	// 対応する最小サンプリングレート
	// （極端に低いレートでは切り出し幅が 0 フレームとなり変換処理が破綻する）
	const DWORD MIN_SAMPLES_PER_SEC = 4000;

	// 変換しない場合の Transform() 用バッファサイズ
	// （再生中の音声トラック変更で高サンプリングレートのトラックに変更された場合でも
	// 　受け入れられるよう、0.5 秒 × 最大サンプリングレート × ステレオ 16 ビット分を
	// 　確保しておく）
	const int NO_TRANSFORM_BUF_SIZE = static_cast<int>(MAX_SAMPLES_PER_SEC / 2) * 2 * 16 / 8;

	// 出力ピンのバッファ長 [1/s]
	const int OUTPUT_BUFFER_TIME_DIV = 10;

	// ============================================================================
	// private 変数
	// ============================================================================

	// 出力メディア情報（アロケートされたものを保持）
	CMediaType* mOutputMedia;

	// mOutputMedia 更新・参照用のロック
	// （ストリーミングスレッドが長時間保持する m_csReceive とは別に、
	// 　短時間しか保持しない専用ロックとすることで、GetMediaType() を
	// 　呼ぶ UI スレッド等をブロックしないようにする）
	CCritSec mOutputMediaLock;

	// 再生中に mOutputMedia が変更されたかどうか（変更時のみサンプルに添付する）
	bool mOutputMediaChanged;

	// 現在の入力メディア情報（再生中の音声トラック変更の検出用）
	CMediaType mInputMedia;

	// 出力ピンに送信できるフレーム数の最大値
	int mMaxOutputFrames;

	// 変換可能かどうか
	bool mTransformable;

	// 変換できない理由
	// （CWebServer スレッドからも読まれるため、mOutputMediaLock で保護する）
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

	// 変換対象フォーマットの情報（mTransformable が true の時のみ有効）
	// チャンネル数
	int mNumChannels;

	// 1 サンプルあたりのバイト数（2: 16bit、3: 24bit、4: 32bit/Float）
	int mBytesPerSample;

	// サンプルが 32bit Float かどうか（false は整数）
	bool mSampleIsFloat;

	// 指示待ち用サーバー
	CWebServer* mWebServer;

#ifdef DEBUGWRITE
	CEasyKeyChanger* mInitialThis;
#endif

	// ----------------------------------------------------------------------------
	// 変換処理用
	// ----------------------------------------------------------------------------

	// ソースバッファ：元の音声データを変換に必要な長さ（はみ出し保険含む）だけ保持
	// （チャンネルごとに 1 本、1 フレームで 1 要素）
	vector<vector<double>> mSrc;

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

	// 伸張後のデータを格納するバッファ（チャンネルごとに 1 本）
	vector<vector<double>> mStrech;

	// 間引き後のデータを格納するバッファ（チャンネルごとに 1 本）
	vector<vector<double>> mPartial;

	// 最終データを格納するバッファ（チャンネルごとに 1 本）
	vector<vector<double>> mDest;

	// クロスフェード幅設定 [ms]
	int mCrossTime;

	// クロスフェード用窓関数の値を格納するバッファ
	vector<double> mWin;

	// クロスフェード幅 [Frame]
	int mCrossFrames;

	// クロスフェード用の端切れを次回に持ち越すバッファ（チャンネルごとに 1 本）
	vector<vector<double>> mCross;

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
	void PartialToDest(vector<double>* oPartial, int oDoneFrames, int oThisTimeFrames, vector<double>* oDest);

	// 入力バッファから 1 サンプルを読み込んで double で返す
	double ReadSampleValue(const BYTE* oPtr) const;

	// double 値を 1 サンプルとして出力バッファに書き込む（形式ごとの範囲にクランプ）
	void WriteSampleValue(BYTE* oPtr, double oValue) const;

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
	void SrcToStrech(vector<double>* oSrc, vector<double>* oStrech, vector<double>* oCross);

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


