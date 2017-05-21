// ============================================================================
// 
// 簡易キーチェンジャー本体（DirectShow フィルター）
// 
// ============================================================================

// ----------------------------------------------------------------------------
// システムに対しては、メジャータイプがオーディオのものはすべて対応として返す
// 実際には非対応のものは、Transform() でスルーする
// これにより、非対応時にもユーザーへの応答が可能となる
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// システムから呼びだされる順番は、以下のようになっている模様
//   CheckInputType()
//   CompleteConnect() ※入力側
//   GetMediaType() ※複数回
//   CheckTransform() ※複数回
//   CompleteConnect() ※出力側
//   DecideBufferSize()
//   Transform() ※複数回
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
// コンストラクター・デストラクター
// ============================================================================

// ----------------------------------------------------------------------------
// コンストラクター
// ----------------------------------------------------------------------------
CEasyKeyChanger::CEasyKeyChanger(TCHAR* oName, LPUNKNOWN oUnknown, HRESULT* oHResult)
	: BASE(oName, oUnknown, CLSID_EasyKeyChanger)
{
#ifdef DEBUGWRITE
	DebugWrite(L"CEasyKeyChanger() ==================== CEasyKeyChanger Ver 1a START ====================");
	DebugWrite(L"CEasyKeyChanger() DEBUGWRITE mode");
#endif

	// 初期化
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

	// 変換処理用の初期化
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
// デストラクター
// ----------------------------------------------------------------------------
CEasyKeyChanger::~CEasyKeyChanger()
{
	DebugWrite(L"~CEasyKeyChanger()");

	delete mOutputMedia;
	delete mWebServer;

	DebugWrite(L"~CEasyKeyChanger() -------------------- END --------------------");
}

// ============================================================================
// CUnknown から継承
// ============================================================================

// ----------------------------------------------------------------------------
// 継承しているインターフェースを返す
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::NonDelegatingQueryInterface(REFIID oRIid, void** oInterface)
{
	return BASE::NonDelegatingQueryInterface(oRIid, oInterface);
}

// ============================================================================
// CTransformFilter から継承
// ============================================================================

// ----------------------------------------------------------------------------
// 入力メディアタイプを受け付けるか確認
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
// 変換メディアタイプを受け付けるか確認
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

	// ここまで到達したものは受け付けるが、実際に変換可能かどうかは別問題
	// 以下で、変換可能かどうかの確認をする
	try {
		// サブタイプの確認
		DebugWrite(L"CheckTransform() sub type: " + GuidToWString(oMtIn->Subtype()));
		if (*oMtIn->Subtype() != MEDIASUBTYPE_PCM) {
			throw L"音声データがリニア PCM ではありません。";
		}

		// フォーマットタイプの確認
		DebugWrite(L"CheckTransform() format type: " + GuidToWString(oMtIn->FormatType()));
		if (*oMtIn->FormatType() != FORMAT_WaveFormatEx) {
			throw L"音声データのフォーマットが WAVEFORMATEX ではありません。";
		}

		// WAVE フォーマットの確認
		mWaveFormat = *reinterpret_cast<WAVEFORMATEX*>(oMtIn->Format());
		if (mWaveFormat.nChannels != NUM_CHANNEL_2) {
			throw L"音声データのチャンネル数がステレオではありません。";
		}
		if (mWaveFormat.wBitsPerSample != BITS_PER_SAMPLE_16) {
			throw L"音声データのビット深度が 16 ではありません。";
		}

		// 変換可能
		mTransformable = true;
	}
	catch (const wchar_t* oReason) {
		mNoTransformReason = oReason;
	}
	catch (...) {
		mNoTransformReason = L"原因は不明です。";
	}

#ifdef DEBUGWRITE
	if (!mTransformable) {
		DebugWrite(L"CheckTransform() 変換不能: " + mNoTransformReason);
	}
#endif

	DebugWrite(L"CheckTransform() OK");
	return S_OK;
}

// ----------------------------------------------------------------------------
// 入力ピン・出力ピンそれぞれの接続が成功した時に呼ばれる
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CompleteConnect(PIN_DIRECTION oDirection, IPin* oReceivePin)
{
	DebugWrite(L"CompleteConnect()");

	// PINDIR_INPUT の場合以外はやること無し
	if (oDirection != PINDIR_INPUT) {
		DebugWrite(L"CompleteConnect() return: not PINDIR_INPUT");
		return S_OK;
	}

	// 出力メディアを設定
	SetupOutputMedia(m_pInput->CurrentMediaType());

	DebugWrite(L"CompleteConnect() OK: INPUT");
	return S_OK;
}

// ----------------------------------------------------------------------------
// 出力ピンのアロケータに必要なバッファサイズを知らせる
// CompleteConnect() の後に呼ばれることを前提としている
// 変換不能の場合は、パススルー用の出力バッファ設定のみ行う
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::DecideBufferSize(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties)
{
	DebugWrite(L"DecideBufferSize()");

	// ピンの接続を確認
	if (!m_pInput->IsConnected()) {
		return E_UNEXPECTED;
	}
	if (!m_pOutput->IsConnected()) {
		return E_UNEXPECTED;
	}

	HRESULT aHResult = S_OK;

	// 出力ピンのバッファ
	if (SUCCEEDED(aHResult)) {
		aHResult = SetupOutputBuffer(oAlloc, oProperties);
	}

	// 変換不能の場合はここで終了
	if (!mTransformable) {
		return aHResult;
	}

	// 変換用のバッファ
	if (SUCCEEDED(aHResult)) {
		SetupTransformPre();
	}

	// サーバー構築
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
// 出力ピンのメディアを返す
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::GetMediaType(int oPosition, CMediaType* oMediaType)
{
	DebugWrite(L"GetMediaType()");

	// 入力ピンが接続されていない場合はエラー
	if (!m_pInput->IsConnected()) {
		return E_UNEXPECTED;
	}

	// 何番目の出力ピンのメディアを返すのか（0 番目のみ返せる）
	if (oPosition < 0) {
		return E_INVALIDARG;
	}
	if (oPosition > 0) {
		return VFW_S_NO_MORE_ITEMS;
	}

	// 作成済みの出力メディアタイプをそのまま返す
	if (mOutputMedia == NULL) {
		DebugWrite(L"GetMediaType() mOutputMedia NULL");
		return E_UNEXPECTED;
	}
	*oMediaType = *mOutputMedia;

	DebugWriteMediaType(L"GetMediaType() OK", oMediaType);
	return S_OK;
}

// ----------------------------------------------------------------------------
// 上流からデータを受信した
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::Receive(IMediaSample* oIn)
{
	return BASE::Receive(oIn);
}

// ----------------------------------------------------------------------------
// フィルター動作
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::Transform(IMediaSample* oIn, IMediaSample *oOut)
{
	AssertWrite(this == mInitialThis, L"Transform() インスタンス移動");

	HRESULT aHResult = S_OK;

	// ヘッダー
	if (SUCCEEDED(aHResult)) {
		aHResult = CopyHeader(oIn, oOut);
	}

	// 中身変換の準備
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

	// 中身変換
	if (SUCCEEDED(aHResult)) {
		// CWebServer スレッドによって書き換えられる可能性があるものは、今回変換用の値をキャッシュしておく
		int aCachedKeyShift = mKeyShift;

		// 変換
		if (mTransformable && (aCachedKeyShift != 0)) {
			// キーが変わった場合は、切り出し幅などを再設定
			if (aCachedKeyShift != mPrevKeyShift) {
				mCutTime = aCachedKeyShift > 0 ? mCutTimeTableUp[aCachedKeyShift] : mCutTimeTableDown[-aCachedKeyShift];
				mCrossTime = aCachedKeyShift > 0 ? mCrossTimeTableUp[aCachedKeyShift] : mCrossTimeTableDown[-aCachedKeyShift];
			}

			// 厳密には、再設定中に CWebServer スレッドによって mCutTime が変更されるのを防ぐべきであるが、面倒くさいので無視
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
// アクセサー
// ============================================================================

// ----------------------------------------------------------------------------
// mCrossTime 書き込み
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
// mCutTime 書き込み
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
// mKeyShift 読み出し
// ----------------------------------------------------------------------------
int CEasyKeyChanger::KeyShift() const
{
	return mKeyShift;
}

// ----------------------------------------------------------------------------
// mKeyShift 書き込み
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
// static 関数
// ============================================================================

// ----------------------------------------------------------------------------
// インスタンス作成（ファクトリーテンプレート用）
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
// private 関数
// ============================================================================

// ----------------------------------------------------------------------------
// 実際にデータを取り扱えるかは別として、システムに対して取り扱いの意思の有無を
// 返す。パススルーになるものだとしても、オーディオなら取り扱いの意思を表明。
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CheckTypeCore(const CMediaType* oMediaType) const
{
	// メジャータイプがオーディオか
	if (*oMediaType->Type() != MEDIATYPE_Audio) {
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	return S_OK;
}

// ----------------------------------------------------------------------------
// メディアの情報をコピー
// それぞれの作業が失敗しても、後続の作業をなるべく行う
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::CopyHeader(IMediaSample* oIn, IMediaSample* oOut)
{
	HRESULT aHResult;

	// ストリームタイム
	REFERENCE_TIME aTimeStart, aTimeEnd;
	aHResult = oIn->GetTime(&aTimeStart, &aTimeEnd);
	if (SUCCEEDED(aHResult)) {
		aHResult = oOut->SetTime(&aTimeStart, &aTimeEnd);
	}

	// メディアタイム
	LONGLONG aMediaStart, aMediaEnd;
	aHResult = oIn->GetMediaTime(&aMediaStart, &aMediaEnd);
	if (SUCCEEDED(aHResult)) {
		aHResult = oOut->SetMediaTime(&aMediaStart, &aMediaEnd);
	}

	// 同期ポイント
	aHResult = oIn->IsSyncPoint();
	if (aHResult == S_OK) {
		aHResult = oOut->SetSyncPoint(true);
	}
	else if (aHResult == S_FALSE) {
		aHResult = oOut->SetSyncPoint(false);
	}

	// メディアタイプ
	oOut->SetMediaType(mOutputMedia);

	// 連続性情報
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
// 切り出し幅テーブル、クロスフェード幅テーブルの初期化
// ----------------------------------------------------------------------------
void CEasyKeyChanger::InitTimeTable()
{
	// 【全般的な傾向】
	// ＜切り出し幅＞     長い→○滑らか、×舌足らず、×発声遅延大
	//                    短い→○ハキハキ、○発声遅延小、×音が波打つ
	// ＜クロスフェード＞ 長い→○ノイズが少ない、×波打つ（特にロングトーンにおいて）
	//                    短い→○波打たない、×プチプチノイズ

	// 【設定方針】
	// キー -1 の時、切り出し幅は 120～240 程度で音が良くなるが、240 では発声遅延が大きいため、
	// 切り出し幅を 120 とする。クロスフェード幅は 15 が良い。これをデフォルト値とする。
	// キー -12 において、デフォルト値だとボーカルが舌足らずになる。
	// 切り出し幅 30、クロスフェード 5 程度が良い。
	// -1～-12 の間の値は、中間値とする。キー -6 辺りからボーカルの舌足らずさが目立つことを
	// 考慮して中間値を設定する。
	// キーを上げるほうは、下げるほうとは逆の調整が必要だが、発声遅延の問題から、切り出し幅は
	// デフォルト値よりも長くできないため、切り出し幅はデフォルト値とする。
	// クロスフェード幅はさほど音質に影響しないように聞こえるが、長くしても波打ち感は少ない
	// ので、大きくしておく。

	// 【キー下げ：切り出し幅】
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

	// 【キー下げ：クロスフェード幅】
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

	// 【キー上げ：切り出し幅】
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

	// 【キー上げ：クロスフェード幅】
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
// 間引きバッファ→最終バッファ
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
// 出力ピンのバッファを設定
// ここで設定したバッファサイズと、Transform() へ送られてくるバッファサイズは
// 無関係の模様
// ここで設定したバッファサイズより大きなバッファを下流に流すとエラーになるので、
// ここでは少し大きめのバッファサイズを設定しておく
// 事実上、ここで設定したバッファサイズが、Transform() で受け入れ可能な
// バッファサイズの最大値となる
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::SetupOutputBuffer(IMemAllocator* oAlloc, ALLOCATOR_PROPERTIES* oProperties)
{
	DebugWrite(L"SetupOutputBuffer()");

	// ページ数
	oProperties->cBuffers = NUM_TRANSFORM_BUFFERS;

	// バッファサイズ
	if (mTransformable) {
		// 0.5 秒分のバッファを確保
		WAVEFORMATEX* aWaveEx = reinterpret_cast<WAVEFORMATEX*>(mOutputMedia->Format());
		mMaxOutputFrames = static_cast<int>(0.5 * aWaveEx->nSamplesPerSec);
		oProperties->cbBuffer = mMaxOutputFrames * aWaveEx->nChannels * aWaveEx->wBitsPerSample / 8;
	}
	else {
		oProperties->cbBuffer = NO_TRANSFORM_BUF_SIZE;
	}

	// 確保
	ALLOCATOR_PROPERTIES aActual;
	HRESULT aHResult = S_OK;
	if (SUCCEEDED(aHResult)) {
		aHResult = oAlloc->SetProperties(oProperties, &aActual);
	}

	// 確保されたかのチェック
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
// 出力メディアタイプを作成
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::SetupOutputMedia(const CMediaType& oMtIn)
{
	mOutputMedia = new CMediaType(oMtIn);

	// 出力形式変換
	mOutputMedia->SetSubtype(&MEDIASUBTYPE_PCM);

	return S_OK;
}

// ----------------------------------------------------------------------------
// 変換用の設定（キーなどが変わるごとに設定が変わるもの）
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SetupTransform(int oNewKey, int oNewCutTime, int oNewCrossTime)
{
	// 切り出し幅 [Frame] を決める
	mCutFrames = static_cast<int>(round(oNewCutTime / 1000.0 * mWaveFormat.nSamplesPerSec));
	DebugWrite(L"SetupTransformPre() mCutFrames: " + lexical_cast<wstring>(mCutFrames));

	// ピッチの拡大縮小率（オク下げ→0.5、オク上げ→2、1 キー上げ→1.059）
	mScale = pow(2, static_cast<double>(oNewKey) / 12);
	//DebugWrite(L"SetupTransform() mScale: " + lexical_cast<wstring>(mScale));

	// シフト長 [Frame] を決める
	mShiftFrames = static_cast<int>(round(oNewCutTime / 1000.0 * mWaveFormat.nSamplesPerSec / mScale));

	// 一度の変換で mCutFrames 分の音声データを使うので、初期に音声データを追加する位置は mCutFrames 以降とする
	mSrcAddBasePos = mSrcAddPos = mCutFrames;

	// クロスフェード長 [Frame]
	int aCrossFrames = static_cast<int>(round(oNewCrossTime / 1000.0 * mWaveFormat.nSamplesPerSec));
	DebugWrite(L"SetupTransformCrossTime() aCrossFrames: " + lexical_cast<wstring>(aCrossFrames));

	// クロスフェードバッファ
	mCrossL.resize(aCrossFrames);
	mCrossR.resize(aCrossFrames);

	// クロスフェード用窓関数の設定
	mWin.resize(aCrossFrames * 2);

	for (int i = 0; i < aCrossFrames * 2; i++) {
		mWin[i] = 0.54 - 0.46 * cos(2 * M_PI * i / (aCrossFrames * 2));	// ハミング窓
																		//mWin[i] = 0.5 - 0.5 * cos(2 * M_PI * i / (aCrossFrames * 2));	// ハン窓
#if 0
																		// 三角波
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
// 変換用の設定（最初に設定して以降変わらないもの）
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SetupTransformPre()
{
	// 元の音声データを保持する長さ [Frame] を決める
	// 本来的には Transform() での受信量に依存するが、Transform() をまたいで値を保持する
	// 必要があるため、Transform() の度に長さを変えるわけにはいかない
	// そこで、予め、mMaxOutputFrames を基準にアロケートしておく
	// 受信量が mMaxOutputFrames だった場合、その 2 個分を確保しておけば、
	// なんとなく足りるのではないか、程度の根拠
	int aSrcLen = mMaxOutputFrames * 2;
	DebugWrite(L"SetupTransformPre() aSrcLen: " + lexical_cast<wstring>(aSrcLen));
	mSrcL.resize(aSrcLen);
	mSrcR.resize(aSrcLen);

	// 伸張後のデータを格納するバッファ
	// ピッチが 2 倍の時、ソースの 2 倍の長さが必要になるのが最大値
	int aStrechLen = aSrcLen * 2;
	DebugWrite(L"SetupTransformPre() aStrechLen: " + lexical_cast<wstring>(aStrechLen));
	mStrechL.resize(aStrechLen);
	mStrechR.resize(aStrechLen);

	// 間引き後のデータを格納するバッファ
	// ソースと同じ長さが最大値
	mPartialL.resize(aSrcLen);
	mPartialR.resize(aSrcLen);

}

// ----------------------------------------------------------------------------
// ソースバッファを前方に mShiftFrames シフト
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

	// 末尾クロスフェードの保存
	AssertWrite(mCutFrames + static_cast<int>(mCrossL.size()) <= static_cast<int>(mSrcL.size()),
		L"SrcToStrech() tail cross oSrc index over: " + lexical_cast<wstring>(mCutFrames + static_cast<int>(mCrossL.size())));
	AssertWrite(static_cast<int>(mCrossL.size() + mCrossL.size()) <= static_cast<int>(mWin.size()),
		L"SrcToStrech() tail cross mWin index over: " + lexical_cast<wstring>(static_cast<int>(mCrossL.size() + mCrossL.size())));
	for (int i = 0; i < static_cast<int>(mCrossL.size()); i++) {
		mCrossL[i] = mSrcL[mCutFrames + i] * mWin[static_cast<int>(mCrossL.size()) + i];
		mCrossR[i] = mSrcR[mCutFrames + i] * mWin[static_cast<int>(mCrossL.size()) + i];
	}

	// シフト
	for (int i = 0; i < mCutFrames; i++) {
		mSrcL[i] = mSrcL[i + mShiftFrames];
		mSrcR[i] = mSrcR[i + mShiftFrames];
	}
	mSrcAddPos = mSrcAddBasePos;

	// 伸張
	SrcToStrech(&mSrcL, &mStrechL, &mCrossL);
	SrcToStrech(&mSrcR, &mStrechR, &mCrossR);

	// 間引き
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
// ソースバッファ→伸張バッファへコピー
// 常に mCutFrames＋クロスフェード分コピーする
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SrcToStrech(vector<int>* oSrc, vector<double>* oStrech, vector<double>* oCross)
{
	// 先頭クロスフェード（末尾クロスフェードとの合成）
	AssertWrite(oCross->size() <= oStrech->size(),
		L"SrcToStrech() head cross oStrech index over: " + lexical_cast<wstring>(oCross->size()));
	AssertWrite(oCross->size() <= oSrc->size(),
		L"SrcToStrech() head cross oSrc index over: " + lexical_cast<wstring>(oCross->size()));
	AssertWrite(oCross->size() <= mWin.size(),
		L"SrcToStrech() head cross mWin index over: " + lexical_cast<wstring>(oCross->size()));
	for (int i = 0; i < static_cast<int>(oCross->size()); i++) {
		(*oStrech)[i] = (*oCross)[i] + (*oSrc)[i] * mWin[i];
	}

	// 中間
	AssertWrite(mCutFrames <= static_cast<int>(oStrech->size()),
		L"SrcToStrech() mid oStrech index over: " + lexical_cast<wstring>(mCutFrames));
	AssertWrite(mCutFrames <= static_cast<int>(oSrc->size()),
		L"SrcToStrech() mid oSrc index over: " + lexical_cast<wstring>(mCutFrames));
	for (int i = static_cast<int>(oCross->size()); i < mCutFrames; i++) {
		(*oStrech)[i] = (*oSrc)[i];
	}
}

// ----------------------------------------------------------------------------
// 伸張バッファ→間引きバッファに集約
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
// 1 切り出し幅分（未満のこともある）を変換して mDestL, mDestR に格納する
// 必要な部分のみデスティネーションにコピー
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformOneCut(int oDoneFrames, int oThisTimeFrames)
{
	// 最終バッファへ
	PartialToDest(&mPartialL, oDoneFrames, oThisTimeFrames, &mDestL);
	PartialToDest(&mPartialR, oDoneFrames, oThisTimeFrames, &mDestR);

	return S_OK;
}

// ----------------------------------------------------------------------------
// フィルター変換（キーチェンジ）
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformTask(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen)
{
	DebugWrite(L"TransformTask()");
#ifdef DEBUGWRITE
	// 変換パフォーマンス測定
	// 測定条件：flower of sorrow 0:10→0:40 の約 30 秒間キー 1 下げ
	// Ver 1.03: 1,310 回、ave 407 us
	// Ver 1.04: 1,305 回、ave 224 us
	// Ver 1.05: 1,301 回、ave 134 us
	static LONGLONG saTotalDealTime = 0;
	static long saTotalDealCount = 0;
	LARGE_INTEGER aBeginCounter;
	QueryPerformanceCounter(&aBeginCounter);
#endif

	// 処理済みの量 [Frame]
	int aDoneFrames = 0;

	// サイズ計算
	int aBlockSize = (mWaveFormat.wBitsPerSample / 8) * mWaveFormat.nChannels;
	int aTotalFrames = oBufLen / aBlockSize;
	//DebugWrite(L"TransformTask() 総量 aTotalFrames: " + lexical_cast<wstring>(aTotalFrames));

	// バッファ
	mDestL.resize(aTotalFrames);
	mDestR.resize(aTotalFrames);
	short aShort;

	// 元の音声データをソースバッファに追加しながら、都度変換
	// mSrcAddBasePos から追加するのが通例だが、途中から追加、というのもあり得る
	while (aDoneFrames < aTotalFrames) {
		// 今回のループで追加する量を決定する
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

		// 元の音声データをソースバッファに追加
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

		// 変換
		TransformOneCut(aDoneFrames, aThisTimeFrames);

		// 位置の変更
		aDoneFrames += aThisTimeFrames;
		mSrcAddPos += aThisTimeFrames;

		if (mSrcAddPos >= mSrcAddBasePos + mShiftFrames) {
			ShiftSrc();
		}
	}

	// 出力
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
// フィルター変換（変換せずにパススルー）
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformThrough(BYTE* oInBuf, BYTE* oOutBuf, long oBufLen)
{
	DebugWrite(L"TransformThrough()");

	CopyMemory(oOutBuf, oInBuf, oBufLen);

	return S_OK;
}





