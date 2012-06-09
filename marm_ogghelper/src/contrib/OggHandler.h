#pragma once

#include "oggHelper.h"

class CUInt16ArrayReturnValue
{
public:
	ogg_int16_t* m_pData;
	int m_uiSize;

public:
	CUInt16ArrayReturnValue(ogg_int16_t* pData, int iSize) : m_pData(pData), m_uiSize(iSize) {}
	~CUInt16ArrayReturnValue() {delete[] m_pData;}
};

class COggHandler : public COggVorbisFileHelper
{
private:
	bool m_bLoop;
	int m_iLoopCount;
	int m_iCurrentLoopCount;
	bool m_bIsBuffered;
	float m_dSyncronBufferingMaxCapacity;

public:
	COggHandler() : COggVorbisFileHelper(), m_bLoop(false), m_iLoopCount(0), m_iCurrentLoopCount(0), m_bIsBuffered(false),m_dSyncronBufferingMaxCapacity(0.75) {}
	~COggHandler(){};
	void decode_loop();
	void decodeSynchron();
	bool IsLastSample();
	CUInt16ArrayReturnValue* decodeOggFileToArray(const std::string& strFileName);
	bool playBuffered(ogg_int16_t* pData, int iLength);
	bool playSynchron();
	bool play(const bool& bEnableLoop = false, const int& iLoopCount = 0, const bool& bBuffered = false, const bool& bIsSynchron = false, ogg_int16_t* pData = NULL, const int& iLength = 0);
	bool isBuffered() {return m_bIsBuffered;}
	int getSoundChannel();
};


