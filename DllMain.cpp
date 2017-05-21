// ============================================================================
// 
// �ȈՃL�[�`�F���W���[
// 
// ============================================================================

// ----------------------------------------------------------------------------
// �����[�X�r���h�� Strmbase.lib ���g���� EasyKeyChanger ���f�o�b�O�r���h����Ɨ�����̂Œ��ӁiDEBUG �t���O���Ă邾���ł��_���j
// MFC ���X�^�e�B�b�N�ɂ���
//   �S�ʁ�MFC �̎g�p�F�X�^�e�B�b�N
//   C++ �R�[�h�����������^�C�����C�u�����F�}���`�X���b�h
//   ��Strmbase.lib �������I�v�V�����Ńr���h���Ă����K�v������
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
// Strmbase.lib �Ŏg�p�����t�@�N�g���e���v���[�g�̐錾
// ============================================================================

// ���̓s���̃��f�B�A�^�C�v
const AMOVIESETUP_MEDIATYPE gPinMediaTypeIn =
{
	&MEDIATYPE_Audio, &MEDIASUBTYPE_NULL
};

// �o�̓s���̃��f�B�A�^�C�v
const AMOVIESETUP_MEDIATYPE gPinMediaTypeOut =
{
	&MEDIATYPE_Audio, &MEDIASUBTYPE_NULL
};

// �s�����
const AMOVIESETUP_PIN gPinInfo[] =
{
	{
		L"Input",			// �s���̖��O�i�p�~�j
		FALSE,				// ���̃s������̓��͂������_�����O����
		FALSE,				// �o�̓s�����ǂ���
		FALSE,				// TRUE �̏ꍇ�́A�t�B���^�����̃s���̃C���X�^���X�� 1 �������Ȃ����Ƃ�����
		FALSE,				// TRUE �̏ꍇ�́A�t�B���^�����̎�ނ̃s���̃C���X�^���X�𕡐��쐬�ł���
		&CLSID_NULL,		// �p�~
		NULL,				// �p�~
		1,					// ���f�B�A�^�C�v�̐�
		&gPinMediaTypeIn	// ���f�B�A�^�C�v
	},
	{
		L"Output",			// �s���̖��O�i�p�~�j
		FALSE,				// ���̃s������̓��͂������_�����O����
		TRUE,				// �o�̓s�����ǂ���
		FALSE,				// TRUE �̏ꍇ�́A�t�B���^�����̃s���̃C���X�^���X�� 1 �������Ȃ����Ƃ�����
		FALSE,				// TRUE �̏ꍇ�́A�t�B���^�����̎�ނ̃s���̃C���X�^���X�𕡐��쐬�ł���
		&CLSID_NULL,		// �p�~
		NULL,				// �p�~
		1,					// ���f�B�A�^�C�v�̐�
		&gPinMediaTypeOut	// ���f�B�A�^�C�v
	}
};

// �t�B���^�[
const AMOVIESETUP_FILTER gFilterInfo =
{
	&CLSID_EasyKeyChanger,			// �N���XID
	FILTER_NAME,					// �t�B���^��
	MERIT_DO_NOT_USE,				// �����b�g
	2,								// �s���̐�
	gPinInfo						// �s�����
};

// �t�@�N�g���e���v���[�g
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
// �O���[�o���֐�
// ============================================================================

// ============================================================================
BOOL APIENTRY DllMain(HMODULE oHModule, DWORD  oReason, LPVOID oReserved)
{
	switch (oReason)
	{
	case DLL_PROCESS_ATTACH:
		// ������
		CoInitialize(NULL);
		g_hInst = static_cast<HINSTANCE>(oHModule);
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		// ��n��
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

