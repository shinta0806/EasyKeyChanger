// ----------------------------------------------------------------------------
#pragma once
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Windows
#include <Streams.h>
// C++
#include <string>
// ----------------------------------------------------------------------------
using namespace std;
// ----------------------------------------------------------------------------

// ============================================================================
// �f�o�b�O�o�̓t���O�L������p�֐�
// ============================================================================

#ifdef DEBUGWRITE
#define	AssertWrite(oCond, oMsg)	AssertWriteFunc((oCond), (oMsg))
#define	DebugWrite(oMsg)	DebugWriteFunc((oMsg))
#define	DebugWriteMediaType(oTitle, oMediaType)	DebugWriteMediaTypeFunc((oTitle), (oMediaType))
#else
#define	AssertWrite(oCond, oMsg)
#define	DebugWrite(oMsg)
#define	DebugWriteMediaType(oTitle, oMediaType)
#endif

#ifdef DEBUGWRITE
void AssertWriteFunc(bool oCond, const wstring& oMsg);
void DebugWriteFunc(const wstring& oMsg);
void DebugWriteMediaTypeFunc(const wstring& oTitle, const CMediaType* oMediaType);
#endif

