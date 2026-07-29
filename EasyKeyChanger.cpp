// ============================================================================
// 
// 簡易キーチェンジャー本体（DirectShow フィルター）
// 
// ============================================================================

// ----------------------------------------------------------------------------
// 入力は非圧縮のリニア PCM 系（整数 16/24/32 ビット・32 ビット Float、
// 1～8 チャンネル、サンプリングレート任意）のみ受け入れ、すべて変換対象とする
// 圧縮音声（AAC 等）は接続を拒否することで、デコーダーより上流（スプリッター
// 直後）に本フィルターが挿入されるのを防ぎ、デコード済み音声を受け取れる位置に
// 挿入されるようにする（ffdshow Audio Processor と同じ受け入れ方式）
// 受け入れたが変換対応外のもの（希少形式）は、Transform() でスルーする
// これにより、非対応時にもユーザーへの応答が可能となる
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// システムから呼びだされる順番は、以下のようになっている模様
// 再生中に音声トラックが変更された場合は、◆の関数が再度呼ばれる
//   CheckInputType() ◆
//   CompleteConnect() ※入力側
//   GetMediaType() ※複数回
//   CheckTransform() ※複数回 ◆
//   CompleteConnect() ※出力側
//   DecideBufferSize()
//   Transform() ※複数回
// トラック変更時は◆以外の関数は呼ばれないため、トラック変更に伴う
// フォーマット変更への対応（出力メディアタイプや変換用設定の更新）は
// CheckTransform() で行う
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
// Windows
#include <mmreg.h>
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
// static 変数（全インスタンス共有）
// ============================================================================

// キーシフト量
volatile int CEasyKeyChanger::smKeyShift = 0;

// 切り出し幅・クロスフェード幅をテーブル値で設定した時のキーシフト量
volatile int CEasyKeyChanger::smPrevTableKeyShift = 0;

// 切り出し幅設定 [ms]
volatile int CEasyKeyChanger::smCutTime = 0;

// クロスフェード幅設定 [ms]
volatile int CEasyKeyChanger::smCrossTime = 0;

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
	DebugWrite(L"CEasyKeyChanger() ==================== CEasyKeyChanger Ver 1b START ====================");
	DebugWrite(L"CEasyKeyChanger() DEBUGWRITE mode");
#endif

	// 初期化
	// （smKeyShift などの共有変数は、他のインスタンスが使用中の可能性があるため初期化しない）
	mOutputMedia = NULL;
	mOutputMediaChanged = false;
	mMaxOutputFrames = 0;
	mTransformable = false;
	InitTimeTable();
	mPrevKeyShift = 0;
	mPrevCutTime = 0;
	mPrevCrossTime = 0;
	ZeroMemory(&mWaveFormatOut, sizeof(mWaveFormatOut));
	mNumChannels = 0;
	mBytesPerSample = 0;
	mSampleIsFloat = false;
	mWebServer = NULL;

	// 変換処理用の初期化
	mSrcAddBasePos = 0;
	mSrcAddPos = 0;
	mScale = 1.0;
	mCutFrames = 0;
	mShiftFrames = 0;
	mCrossFrames = 0;

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

	// 非圧縮のリニア PCM 系（整数 PCM／IEEE Float）のみ受け入れる
	// 圧縮音声（AAC 等）まで受け入れると、プレーヤーによってはデコーダーより上流
	// （スプリッター直後）に本フィルターが挿入され、そのチェーンではキーチェンジが
	// 不可能になってしまう。拒否すれば、デコーダーが挿入された後の位置
	// （デコード済み音声を受け取れる位置）で本フィルターの挿入が試みられる
	// （ffdshow Audio Processor と同じ受け入れ方式）

	// サブタイプの確認
	// ビッグエンディアンの LPCM（Blu-ray／DVD）等は、WAVEFORMATEX 上は
	// リニア PCM に見えるがバイト順が異なるため、サブタイプで除外する
	DebugWrite(L"CheckInputType() sub type: " + GuidToWString(oMtIn->Subtype()));
	if (*oMtIn->Subtype() != MEDIASUBTYPE_PCM && *oMtIn->Subtype() != MEDIASUBTYPE_IEEE_FLOAT) {
		DebugWrite(L"CheckInputType() NG: sub type is not linear PCM");
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	if (*oMtIn->FormatType() != FORMAT_WaveFormatEx) {
		DebugWrite(L"CheckInputType() NG: not WAVEFORMATEX");
		return VFW_E_TYPE_NOT_ACCEPTED;
	}
	if (oMtIn->Format() == NULL || oMtIn->FormatLength() < sizeof(PCMWAVEFORMAT)) {
		DebugWrite(L"CheckInputType() NG: format info too short");
		return VFW_E_TYPE_NOT_ACCEPTED;
	}
	WORD aFormatTag = reinterpret_cast<WAVEFORMATEX*>(oMtIn->Format())->wFormatTag;
	if (aFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		// WAVEFORMATEXTENSIBLE の場合は SubFormat がリニア PCM 系かを確認
		if (oMtIn->FormatLength() < sizeof(WAVEFORMATEXTENSIBLE)) {
			DebugWrite(L"CheckInputType() NG: WAVEFORMATEXTENSIBLE info too short");
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
		GUID aSubFormat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(oMtIn->Format())->SubFormat;
		if (aSubFormat != MEDIASUBTYPE_PCM && aSubFormat != MEDIASUBTYPE_IEEE_FLOAT) {
			DebugWrite(L"CheckInputType() NG: sub format is not linear PCM: " + GuidToWString(&aSubFormat));
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	}
	else if (aFormatTag != WAVE_FORMAT_PCM && aFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
		DebugWrite(L"CheckInputType() NG: compressed format tag: " + lexical_cast<wstring>(aFormatTag));
		return VFW_E_TYPE_NOT_ACCEPTED;
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

	// 以下でメンバー変数を更新するため、ストリーミングスレッドの Transform() と
	// 競合しないようにロックする（Transform() と同一スレッドから呼ばれた場合は、
	// 再帰ロックとなるので問題ない）
	CAutoLock aLock(&m_csReceive);

	// 再生中の音声トラック変更などにより、入力メディアタイプが変更された場合は、
	// 出力メディアタイプを新しい入力メディアタイプに合わせて作り直す
	// （トラック変更時は CompleteConnect() や DecideBufferSize() は再度呼ばれないため、ここで対応する）
	// サンプリングレートのみならず、チャンネル数やビット深度などの変更もすべて検出できるよう、
	// メディアタイプ全体を比較する
	bool aFormatChanged = false;
	if (mOutputMedia != NULL && *oMtIn != mInputMedia) {
		DebugWriteMediaType(L"CheckTransform() input format changed", oMtIn);
		SetupOutputMedia(*oMtIn);
		aFormatChanged = true;
	}

	// 変換可能かどうかのフラグを一旦リセット
	mTransformable = false;

	// ここまで到達したものは受け付けるが、実際に変換可能かどうかは別問題
	// 以下で、変換可能かどうかの確認をする
	try {
		// サブタイプの確認
		// （ビッグエンディアンの LPCM 等を除外する）
		DebugWrite(L"CheckTransform() sub type: " + GuidToWString(oMtIn->Subtype()));
		if (*oMtIn->Subtype() != MEDIASUBTYPE_PCM && *oMtIn->Subtype() != MEDIASUBTYPE_IEEE_FLOAT) {
			throw L"音声データがリニア PCM ではありません。";
		}

		// フォーマットタイプの確認
		DebugWrite(L"CheckTransform() format type: " + GuidToWString(oMtIn->FormatType()));
		if (*oMtIn->FormatType() != FORMAT_WaveFormatEx) {
			throw L"音声データのフォーマットが WAVEFORMATEX ではありません。";
		}
		if (oMtIn->Format() == NULL || oMtIn->FormatLength() < sizeof(PCMWAVEFORMAT)) {
			throw L"音声データのフォーマット情報が不足しています。";
		}

		// WAVE フォーマットの確認
		// 旧形式の PCMWAVEFORMAT（cbSize 無し・16 バイト）の場合もあるため、存在する分のみコピーする
		WAVEFORMATEX aWaveFormatIn;
		ZeroMemory(&aWaveFormatIn, sizeof(aWaveFormatIn));
		CopyMemory(&aWaveFormatIn, oMtIn->Format(),
				oMtIn->FormatLength() < sizeof(aWaveFormatIn) ? oMtIn->FormatLength() : sizeof(aWaveFormatIn));
		DebugWrite(L"CheckTransform() a");

		// リニア PCM（整数）か IEEE Float かの確認
		bool aIsFloat;
		if (aWaveFormatIn.wFormatTag == WAVE_FORMAT_PCM) {
			aIsFloat = false;
		}
		else if (aWaveFormatIn.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
			aIsFloat = true;
		}
		else if (aWaveFormatIn.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
			if (oMtIn->FormatLength() < sizeof(WAVEFORMATEXTENSIBLE)) {
				throw L"音声データのフォーマット情報（WAVEFORMATEXTENSIBLE）が不足しています。";
			}
			GUID aSubFormat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(oMtIn->Format())->SubFormat;
			if (aSubFormat == MEDIASUBTYPE_PCM) {
				aIsFloat = false;
			}
			else if (aSubFormat == MEDIASUBTYPE_IEEE_FLOAT) {
				aIsFloat = true;
			}
			else {
				throw L"音声データがリニア PCM ではありません。";
			}
		}
		else {
			throw L"音声データがリニア PCM ではありません。";
		}

		// チャンネル数の確認
		if (aWaveFormatIn.nChannels < 1 || aWaveFormatIn.nChannels > MAX_NUM_CHANNELS) {
			throw L"音声データのチャンネル数に対応していません。";
		}

		// ビット深度の確認
		if (aIsFloat) {
			if (aWaveFormatIn.wBitsPerSample != 32) {
				throw L"音声データのビット深度に対応していません（Float は 32 ビットのみ対応）。";
			}
		}
		else {
			if (aWaveFormatIn.wBitsPerSample != 16 && aWaveFormatIn.wBitsPerSample != 24 && aWaveFormatIn.wBitsPerSample != 32) {
				throw L"音声データのビット深度に対応していません（整数は 16/24/32 ビットのみ対応）。";
			}
		}

		// ブロックアライメントの整合確認（変換時のバッファ位置計算の前提）
		if (aWaveFormatIn.nBlockAlign != aWaveFormatIn.nChannels * aWaveFormatIn.wBitsPerSample / 8) {
			throw L"音声データのブロックアライメントが不正です。";
		}

		// サンプリングレートの確認
		// （極端に低いレートでは切り出し幅が 0 フレームとなり変換処理が破綻するため）
		if (aWaveFormatIn.nSamplesPerSec < MIN_SAMPLES_PER_SEC || aWaveFormatIn.nSamplesPerSec > MAX_SAMPLES_PER_SEC) {
			throw L"音声データのサンプリングレートに対応していません。";
		}

		// 修正
		// （mOutputMedia のフォーマットは SetupOutputMedia() で修正済みのため、
		// 　ここでは変換処理用の値のみ修正する）
		FixBadWaveFormat(&aWaveFormatIn);

		// 変換処理用の情報を設定
		mWaveFormatOut = aWaveFormatIn;
		mNumChannels = aWaveFormatIn.nChannels;
		mBytesPerSample = aWaveFormatIn.wBitsPerSample / 8;
		mSampleIsFloat = aIsFloat;

		// 変換可能
		mTransformable = true;
	}
	catch (const wchar_t* oReason) {
		CAutoLock aReasonLock(&mOutputMediaLock);
		mNoTransformReason = oReason;
	}
	catch (...) {
		CAutoLock aReasonLock(&mOutputMediaLock);
		mNoTransformReason = L"原因は不明です。";
	}

	// 入力メディアタイプが変更され、かつ変換可能な場合は、変換用の設定も
	// 新しいフォーマットに合わせて更新する
	if (aFormatChanged && mTransformable) {
		if (mMaxOutputFrames == 0) {
			// 接続当初は変換不能だったが、トラック変更により変換可能となった場合
			// （DecideBufferSize() は再度呼ばれないため、ここで変換用の準備を行う）
			mMaxOutputFrames = static_cast<int>(0.5 * mWaveFormatOut.nSamplesPerSec);
		}

		// 変換用バッファを新しいサンプリングレートに合わせて確保し直す
		SetupTransformPre();

		// サーバー構築（未構築の場合のみ）
		if (mWebServer == NULL) {
			mWebServer = new CWebServer(this);
			mWebServer->Run();
		}
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

	// 上流が本フィルター自身の場合は接続を拒否する
	// （何でも受け入れる設計のため、グラフ構築時に本フィルターが連鎖して
	// 　多重挿入されるのを防ぐ。拒否すればグラフビルダーは次の候補
	// 　（デコーダー等）を試す）
	if (oReceivePin != NULL) {
		PIN_INFO aPinInfo;
		if (SUCCEEDED(oReceivePin->QueryPinInfo(&aPinInfo)) && aPinInfo.pFilter != NULL) {
			CLSID aUpstreamClsid = GUID_NULL;
			aPinInfo.pFilter->GetClassID(&aUpstreamClsid);
			aPinInfo.pFilter->Release();
			if (aUpstreamClsid == CLSID_EasyKeyChanger) {
				DebugWrite(L"CompleteConnect() NG: upstream is EasyKeyChanger itself");
				return VFW_E_TYPE_NOT_ACCEPTED;
			}
		}
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
	// （再生中の音声トラック変更により CheckTransform() で構築済みの場合もあるため、
	// 　未構築の場合のみ）
	if (SUCCEEDED(aHResult) && mWebServer == NULL) {
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
	// （再生中の音声トラック変更で SetupOutputMedia() が mOutputMedia を更新することが
	// 　あるため、ロックしてからコピーする。m_csReceive はストリーミングスレッドが
	// 　長時間保持することがあり、UI スレッドがフリーズするため使用しない）
	CAutoLock aLock(&mOutputMediaLock);
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
		int aCachedKeyShift = smKeyShift;

		// 変換
		if (mTransformable && (aCachedKeyShift != 0)) {
			// キーが変わった場合は、切り出し幅などを再設定
			// （音声トラックごとにインスタンスが作られる場合、後から変換を始めた
			// 　インスタンスが設定を巻き戻さないよう、キーの変化も共有して判定する）
			if (aCachedKeyShift != smPrevTableKeyShift) {
				smPrevTableKeyShift = aCachedKeyShift;
				smCutTime = aCachedKeyShift > 0 ? mCutTimeTableUp[aCachedKeyShift] : mCutTimeTableDown[-aCachedKeyShift];
				smCrossTime = aCachedKeyShift > 0 ? mCrossTimeTableUp[aCachedKeyShift] : mCrossTimeTableDown[-aCachedKeyShift];
			}

			// 厳密には、再設定中に CWebServer スレッドによって smCutTime が変更されるのを防ぐべきであるが、面倒くさいので無視
			int aCachedCutTime = smCutTime;
			int aCachedCrossTime = smCrossTime;
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
// smCrossTime 書き込み
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::SetCrossTime(int oCrossTime)
{
	if (oCrossTime < CROSS_TIME_MIN) {
		return false;
	}
	if (oCrossTime > CROSS_TIME_MAX) {
		return false;
	}
	if (oCrossTime == smCrossTime) {
		return false;
	}

	smCrossTime = oCrossTime;
	return true;
}

// ----------------------------------------------------------------------------
// smCutTime 書き込み
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::SetCutTime(int oCutTime)
{
	if (oCutTime < CUT_TIME_MIN) {
		return false;
	}
	if (oCutTime > CUT_TIME_MAX) {
		return false;
	}
	if (oCutTime == smCutTime) {
		return false;
	}

	smCutTime = oCutTime;
	return true;
}

// ----------------------------------------------------------------------------
// smKeyShift 読み出し
// ----------------------------------------------------------------------------
int CEasyKeyChanger::KeyShift() const
{
	return smKeyShift;
}

// ----------------------------------------------------------------------------
// smKeyShift 書き込み
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::SetKeyShift(int oKeyShift)
{
	if (oKeyShift < KEY_SHIFT_MIN) {
		return false;
	}
	if (oKeyShift > KEY_SHIFT_MAX) {
		return false;
	}
	if (oKeyShift == smKeyShift) {
		return false;
	}

	smKeyShift = oKeyShift;
	return true;
}

// ----------------------------------------------------------------------------
// 変換可能かどうか
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::Transformable() const
{
	return mTransformable;
}

// ----------------------------------------------------------------------------
// 変換できない理由
// ----------------------------------------------------------------------------
wstring CEasyKeyChanger::NoTransformReason()
{
	// CheckTransform() による更新と競合しないようにロックする
	CAutoLock aLock(&mOutputMediaLock);

	return mNoTransformReason;
}

// ----------------------------------------------------------------------------
// 現在の入力フォーマットの要約（診断用）
// ----------------------------------------------------------------------------
wstring CEasyKeyChanger::InputFormatSummary()
{
	// SetupOutputMedia() による更新と競合しないようにロックする
	CAutoLock aLock(&mOutputMediaLock);

	if (*mInputMedia.FormatType() == FORMAT_WaveFormatEx && mInputMedia.Format() != NULL
			&& mInputMedia.FormatLength() >= sizeof(PCMWAVEFORMAT)) {
		WAVEFORMATEX aWaveFormat;
		ZeroMemory(&aWaveFormat, sizeof(aWaveFormat));
		CopyMemory(&aWaveFormat, mInputMedia.Format(),
				mInputMedia.FormatLength() < sizeof(aWaveFormat) ? mInputMedia.FormatLength() : sizeof(aWaveFormat));
		return lexical_cast<wstring>(aWaveFormat.nChannels) + L"ch "
				+ lexical_cast<wstring>(aWaveFormat.nSamplesPerSec) + L"Hz "
				+ lexical_cast<wstring>(aWaveFormat.wBitsPerSample) + L"bit (tag "
				+ lexical_cast<wstring>(aWaveFormat.wFormatTag) + L")";
	}
	return L"subtype " + GuidToWString(mInputMedia.Subtype());
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
	// 変更があった場合のみ、出力サンプルに添付して下流に伝える
	// （毎サンプル添付すると、下流のフィルターによってはサンプルごとに
	// 　再初期化が走り、音声が途切れることがある）
	if (mOutputMediaChanged) {
		oOut->SetMediaType(mOutputMedia);
		mOutputMediaChanged = false;
	}

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
// 不正な音声情報を修正
// 一部の上流フィルターがハイレゾ音源の音声情報を正しく通知してこないため、修正
// （65536Hz 以上のサンプリングレートの場合、上位ビットが欠落している）
// ＜返値＞ 修正したら true
// ----------------------------------------------------------------------------
bool CEasyKeyChanger::FixBadWaveFormat(WAVEFORMATEX* oWaveFormat)
{
	DWORD aFixedSamplesPerSec = 0;

	switch (oWaveFormat->nSamplesPerSec) {
	case 0x5888:
		// 88.2kHz
		aFixedSamplesPerSec = 0x15888;
		break;
	case 0x7700:
		// 96kHz
		aFixedSamplesPerSec = 0x17700;
		break;
	case 0xB110:
		// 176.4kHz
		aFixedSamplesPerSec = 0x2B110;
		break;
	case 0xEE00:
		// 192kHz
		aFixedSamplesPerSec = 0x2EE00;
		break;
	case 0x6220:
		// 352.8kHz
		aFixedSamplesPerSec = 0x56220;
		break;
	case 0xDC00:
		// 384kHz
		aFixedSamplesPerSec = 0x5DC00;
		break;
	default:
		break;
	}

	if (aFixedSamplesPerSec == 0) {
		return false;
	}

	// サンプリングレート等を修正
	DebugWrite(L"~FixBadWaveFormat() Fix");
	oWaveFormat->nSamplesPerSec = aFixedSamplesPerSec;
	oWaveFormat->nAvgBytesPerSec = aFixedSamplesPerSec * oWaveFormat->nBlockAlign;
	return true;
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
void CEasyKeyChanger::PartialToDest(vector<double>* oPartial, int oDoneFrames, int oThisTimeFrames, vector<double>* oDest)
{
	AssertWrite((mSrcAddPos - mSrcAddBasePos) + oThisTimeFrames <= static_cast<int>(oPartial->size()),
		L"PartialToDest() oPartial index over: " + lexical_cast<wstring>((mSrcAddPos - mSrcAddBasePos) + oThisTimeFrames));
	AssertWrite(oDoneFrames + oThisTimeFrames <= static_cast<int>(oDest->size()),
		L"PartialToDest() oDest index over: " + lexical_cast<wstring>(oDoneFrames + oThisTimeFrames));

	// クランプはサンプル形式ごとに WriteSampleValue() で行うため、ここではコピーのみ
	for (int i = 0; i < oThisTimeFrames; i++) {
		int aPartialIndex = (mSrcAddPos - mSrcAddBasePos) + i;
		int aDestIndex = oDoneFrames + i;

		(*oDest)[aDestIndex] = (*oPartial)[aPartialIndex];
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
// 入力バッファから 1 サンプルを読み込んで double で返す
// ----------------------------------------------------------------------------
double CEasyKeyChanger::ReadSampleValue(const BYTE* oPtr) const
{
	if (mSampleIsFloat) {
		// 32bit Float
		float aFloat;
		CopyMemory(&aFloat, oPtr, sizeof(aFloat));
		return aFloat;
	}

	switch (mBytesPerSample) {
	case 2:
	{
		// 16bit 整数
		short aShort;
		CopyMemory(&aShort, oPtr, 2);
		return aShort;
	}
	case 3:
	{
		// 24bit 整数（3 バイト・リトルエンディアン）
		// 上位 24 ビットに詰めてから算術シフトで符号を維持したまま戻す
		int aInt = static_cast<int>((static_cast<DWORD>(oPtr[0]) << 8)
				| (static_cast<DWORD>(oPtr[1]) << 16) | (static_cast<DWORD>(oPtr[2]) << 24));
		return aInt >> 8;
	}
	default:
	{
		// 32bit 整数
		int aInt;
		CopyMemory(&aInt, oPtr, 4);
		return aInt;
	}
	}
}

// ----------------------------------------------------------------------------
// double 値を 1 サンプルとして出力バッファに書き込む
// 整数形式の場合は形式ごとの範囲にクランプする
// ----------------------------------------------------------------------------
void CEasyKeyChanger::WriteSampleValue(BYTE* oPtr, double oValue) const
{
	if (mSampleIsFloat) {
		// 32bit Float
		float aFloat = static_cast<float>(oValue);
		CopyMemory(oPtr, &aFloat, sizeof(aFloat));
		return;
	}

	switch (mBytesPerSample) {
	case 2:
	{
		// 16bit 整数
		short aShort;
		if (oValue > 32767.0) {
			aShort = 32767;
		}
		else if (oValue < -32768.0) {
			aShort = -32768;
		}
		else {
			aShort = static_cast<short>(oValue);
		}
		CopyMemory(oPtr, &aShort, 2);
		break;
	}
	case 3:
	{
		// 24bit 整数（3 バイト・リトルエンディアン）
		int aInt;
		if (oValue > 8388607.0) {
			aInt = 8388607;
		}
		else if (oValue < -8388608.0) {
			aInt = -8388608;
		}
		else {
			aInt = static_cast<int>(oValue);
		}
		oPtr[0] = static_cast<BYTE>(aInt);
		oPtr[1] = static_cast<BYTE>(aInt >> 8);
		oPtr[2] = static_cast<BYTE>(aInt >> 16);
		break;
	}
	default:
	{
		// 32bit 整数
		int aInt;
		if (oValue > 2147483647.0) {
			aInt = 2147483647;
		}
		else if (oValue < -2147483648.0) {
			aInt = -2147483647 - 1;
		}
		else {
			aInt = static_cast<int>(oValue);
		}
		CopyMemory(oPtr, &aInt, 4);
		break;
	}
	}
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

		// 再生中の音声トラック変更でサンプリングレートが高くなった場合でも
		// 受け入れられるよう、バッファサイズ自体は対応最大サンプリングレート分を
		// 確保しておく（トラック変更時は本関数が再度呼ばれず、バッファサイズを
		// 後から変更できないため）
		DWORD aBufferSamplesPerSec = aWaveEx->nSamplesPerSec;
		if (aBufferSamplesPerSec < MAX_SAMPLES_PER_SEC) {
			aBufferSamplesPerSec = MAX_SAMPLES_PER_SEC;
		}
		oProperties->cbBuffer = static_cast<long>(0.5 * aBufferSamplesPerSec) * aWaveEx->nBlockAlign;
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
// 初回接続時のほか、再生中の音声トラック変更で入力メディアタイプが
// 変更された場合にも呼ばれる
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::SetupOutputMedia(const CMediaType& oMtIn)
{
	// GetMediaType() 等の参照と競合しないようにロックする
	CAutoLock aMediaLock(&mOutputMediaLock);

	// 現在の入力メディアタイプを記録（再生中の音声トラック変更の検出用）
	mInputMedia = oMtIn;

	// 出力メディアタイプを入力メディアタイプから作り直す
	// 再生中に呼ばれた場合、他スレッドが mOutputMedia を参照している可能性があるため、
	// インスタンスは作り直さず内容のみ更新する
	if (mOutputMedia == NULL) {
		mOutputMedia = new CMediaType(oMtIn);
	}
	else {
		*mOutputMedia = oMtIn;
	}

	// サブタイプは入力のまま維持する
	// （以前は MEDIASUBTYPE_PCM に書き換えていたが、デコーダーより上流に挿入されて
	// 　圧縮音声（AAC 等）をパススルーする場合、PCM と誤ったラベルを付けると下流が
	// 　圧縮データを PCM と誤解して正常に再生されなくなるため、書き換えない。
	// 　変換対象となるリニア PCM は、入力のサブタイプが元々 MEDIASUBTYPE_PCM である）

	// 不正な音声情報の修正
	// フォーマットブロック全体（WAVEFORMATEXTENSIBLE の追加情報など）を
	// 維持したまま修正する
	if (*mOutputMedia->FormatType() == FORMAT_WaveFormatEx && mOutputMedia->Format() != NULL
			&& mOutputMedia->FormatLength() >= sizeof(PCMWAVEFORMAT)) {
		FixBadWaveFormat(reinterpret_cast<WAVEFORMATEX*>(mOutputMedia->Format()));
	}

	// 次回 Transform() 時に SetupTransform() を再実行させ、
	// 切り出し幅 [Frame] などを現在のサンプリングレートで再計算させる
	mPrevCutTime = -1;

	// 次回 Transform() 時に、出力サンプルへメディアタイプを添付させる
	mOutputMediaChanged = true;

	return S_OK;
}

// ----------------------------------------------------------------------------
// 変換用の設定（キーなどが変わるごとに設定が変わるもの）
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SetupTransform(int oNewKey, int oNewCutTime, int oNewCrossTime)
{
	// 切り出し幅 [Frame] を決める
	mCutFrames = static_cast<int>(round(oNewCutTime / 1000.0 * mWaveFormatOut.nSamplesPerSec));
	DebugWrite(L"SetupTransformPre() mCutFrames: " + lexical_cast<wstring>(mCutFrames));

	// ピッチの拡大縮小率（オク下げ→0.5、オク上げ→2、1 キー上げ→1.059）
	mScale = pow(2, static_cast<double>(oNewKey) / 12);
	//DebugWrite(L"SetupTransform() mScale: " + lexical_cast<wstring>(mScale));

	// シフト長 [Frame] を決める
	mShiftFrames = static_cast<int>(round(oNewCutTime / 1000.0 * mWaveFormatOut.nSamplesPerSec / mScale));

	// 各長さが 0 になると変換処理が進まなくなるため、最低 1 フレームは確保する
	if (mCutFrames < 1) {
		mCutFrames = 1;
	}
	if (mShiftFrames < 1) {
		mShiftFrames = 1;
	}

	// 一度の変換で mCutFrames 分の音声データを使うので、初期に音声データを追加する位置は mCutFrames 以降とする
	mSrcAddBasePos = mSrcAddPos = mCutFrames;

	// クロスフェード長 [Frame]
	int aCrossFrames = static_cast<int>(round(oNewCrossTime / 1000.0 * mWaveFormatOut.nSamplesPerSec));
	DebugWrite(L"SetupTransformCrossTime() aCrossFrames: " + lexical_cast<wstring>(aCrossFrames));
	mCrossFrames = aCrossFrames;

	// クロスフェードバッファ（チャンネルごと）
	mCross.resize(mNumChannels);
	for (int ch = 0; ch < mNumChannels; ch++) {
		mCross[ch].assign(aCrossFrames, 0.0);
	}

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

	// 再生中の音声トラック変更でサンプリングレートが高くなった場合でも、
	// 1 回の変換に必要な最大長（切り出し幅＋シフト幅＋クロスフェード幅の最大値）を
	// 確保できるようにする
	// シフト幅は最大で切り出し幅の 2 倍（キー -12 の時）なので、切り出し幅の 3 倍を見込む
	int aNeedLen = static_cast<int>(round((CUT_TIME_MAX * 3 + CROSS_TIME_MAX) / 1000.0 * mWaveFormatOut.nSamplesPerSec)) + 1;
	if (aSrcLen < aNeedLen) {
		aSrcLen = aNeedLen;
	}
	DebugWrite(L"SetupTransformPre() aSrcLen: " + lexical_cast<wstring>(aSrcLen));

	// 伸張後のデータを格納するバッファ
	// ピッチが 2 倍の時、ソースの 2 倍の長さが必要になるのが最大値
	int aStrechLen = aSrcLen * 2;
	DebugWrite(L"SetupTransformPre() aStrechLen: " + lexical_cast<wstring>(aStrechLen));

	// チャンネルごとにバッファを確保
	// （間引き後のデータを格納するバッファは、ソースと同じ長さが最大値）
	mSrc.resize(mNumChannels);
	mStrech.resize(mNumChannels);
	mPartial.resize(mNumChannels);
	mDest.resize(mNumChannels);
	for (int ch = 0; ch < mNumChannels; ch++) {
		mSrc[ch].assign(aSrcLen, 0.0);
		mStrech[ch].assign(aStrechLen, 0.0);
		mPartial[ch].assign(aSrcLen, 0.0);
	}

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
	DebugWrite(L"ShiftSrc() mShiftFrames + mCutFrames + mCrossFrames: " + lexical_cast<wstring>(mShiftFrames + mCutFrames + mCrossFrames));
#endif

	// 末尾クロスフェードの保存
	AssertWrite(mCutFrames + mCrossFrames <= static_cast<int>(mSrc[0].size()),
		L"SrcToStrech() tail cross oSrc index over: " + lexical_cast<wstring>(mCutFrames + mCrossFrames));
	AssertWrite(mCrossFrames + mCrossFrames <= static_cast<int>(mWin.size()),
		L"SrcToStrech() tail cross mWin index over: " + lexical_cast<wstring>(mCrossFrames + mCrossFrames));
	for (int ch = 0; ch < mNumChannels; ch++) {
		for (int i = 0; i < mCrossFrames; i++) {
			mCross[ch][i] = mSrc[ch][mCutFrames + i] * mWin[mCrossFrames + i];
		}
	}

	// シフト
	for (int ch = 0; ch < mNumChannels; ch++) {
		for (int i = 0; i < mCutFrames; i++) {
			mSrc[ch][i] = mSrc[ch][i + mShiftFrames];
		}
	}
	mSrcAddPos = mSrcAddBasePos;

	// 伸張・間引き
	for (int ch = 0; ch < mNumChannels; ch++) {
		SrcToStrech(&mSrc[ch], &mStrech[ch], &mCross[ch]);
		StrechToPartial(&mStrech[ch], &mPartial[ch]);
	}

#ifdef DEBUGWRITEz
	wstring src0;
	for (int i = 0; i < mShiftFrames + mCutFrames + mCrossFrames; i++) {
		src0 += lexical_cast<wstring>(mSrc[0][i]) + L",";
	}
	DebugWrite(L"ShiftSrc() src0: " + src0);
#endif

}

// ----------------------------------------------------------------------------
// ソースバッファ→伸張バッファへコピー
// 常に mCutFrames＋クロスフェード分コピーする
// ----------------------------------------------------------------------------
void CEasyKeyChanger::SrcToStrech(vector<double>* oSrc, vector<double>* oStrech, vector<double>* oCross)
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
// 1 切り出し幅分（未満のこともある）を変換して mDest に格納する
// 必要な部分のみデスティネーションにコピー
// ----------------------------------------------------------------------------
HRESULT CEasyKeyChanger::TransformOneCut(int oDoneFrames, int oThisTimeFrames)
{
	// 最終バッファへ（チャンネルごと）
	for (int ch = 0; ch < mNumChannels; ch++) {
		PartialToDest(&mPartial[ch], oDoneFrames, oThisTimeFrames, &mDest[ch]);
	}

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
	int aBlockSize = mBytesPerSample * mNumChannels;
	int aTotalFrames = oBufLen / aBlockSize;
	//DebugWrite(L"TransformTask() 総量 aTotalFrames: " + lexical_cast<wstring>(aTotalFrames));

	// バッファ
	for (int ch = 0; ch < mNumChannels; ch++) {
		mDest[ch].resize(aTotalFrames);
	}

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

		// 万一処理が進まない状況になった場合は、無限ループを避けるため中断する
		if (aThisTimeFrames <= 0) {
			AssertWrite(false, L"TransformTask() no progress: mShiftFrames: " + lexical_cast<wstring>(mShiftFrames));
			return E_FAIL;
		}
#if 0
		DebugWrite(L"TransformTask() aDoneFrames: " + lexical_cast<wstring>(aDoneFrames));
		DebugWrite(L"TransformTask() aTotalFrames: " + lexical_cast<wstring>(aTotalFrames));
		DebugWrite(L"TransformTask() mSrcAddPos: " + lexical_cast<wstring>(mSrcAddPos));
		DebugWrite(L"TransformTask() aThisTimeFrames: " + lexical_cast<wstring>(aThisTimeFrames));
#endif

		// 元の音声データをソースバッファに追加
		AssertWrite((aDoneFrames + aThisTimeFrames - 1) * aBlockSize + mBytesPerSample < oBufLen,
			L"TransformTask() input oInBuf index over: " + lexical_cast<wstring>((aDoneFrames + aThisTimeFrames - 1) * aBlockSize + mBytesPerSample));
		AssertWrite(mSrcAddPos + aThisTimeFrames <= static_cast<int>(mSrc[0].size()),
			L"TransformTask() input mSrc index over: " + lexical_cast<wstring>(mSrcAddPos + aThisTimeFrames));
		for (int i = 0; i < aThisTimeFrames; i++) {
			const BYTE* aFrame = oInBuf + (aDoneFrames + i) * aBlockSize;
			for (int ch = 0; ch < mNumChannels; ch++) {
				mSrc[ch][mSrcAddPos + i] = ReadSampleValue(aFrame + ch * mBytesPerSample);
			}
		}

#ifdef DEBUGWRITEz
		wstring src0;
		for (int i = 0; i < mShiftFrames + mCutFrames + mCrossFrames; i++) {
			src0 += lexical_cast<wstring>(mSrc[0][i]) + L",";
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
	AssertWrite(aTotalFrames <= static_cast<int>(mDest[0].size()),
		L"TransformTask() output mDest index over: " + lexical_cast<wstring>(aTotalFrames));
	AssertWrite((aTotalFrames - 1) * aBlockSize + mBytesPerSample < oBufLen,
		L"TransformTask() output oOutBuf index over: " + lexical_cast<wstring>((aTotalFrames - 1) * aBlockSize + mBytesPerSample));
	for (int i = 0; i < aTotalFrames; i++) {
		BYTE* aFrame = oOutBuf + i * aBlockSize;
		for (int ch = 0; ch < mNumChannels; ch++) {
			WriteSampleValue(aFrame + ch * mBytesPerSample, mDest[ch][i]);
		}
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





