#include "OggHandler.h"
#include "..\sound_helper.h"

void COggHandler::decode_loop()
{
	int res = EOK;
	while(res == EOK && !bStopDecoding)
	{
		res = decode();
		//loop count 0 means endless looping
		if (res == EOF && m_bLoop && (m_iCurrentLoopCount++ < m_iLoopCount || m_iLoopCount == 0))
		{
			ov_time_seek_func(&vf, 0);
			current_time = 0;
			current_sample = ov_pcm_tell(&vf) * (ogg_int64_t)dResampleFactor*get_nChannels();
			res = decode();
		}
		s3eDeviceYield();
	}
}

bool COggHandler::IsLastSample()
{
	if (!m_bLoop || (m_iCurrentLoopCount >= m_iLoopCount && m_iLoopCount > 0))
	{
		if (bEnableResampling)
		{
			if ((current_sample + 1) >= (ogg_int64_t) total_samples * dResampleFactor*get_nChannels()) return true;
		}
		else
		{
			return current_sample + 1 >= total_samples;
		}
	}
	return false;
}

int COggHandler::getSoundChannel()
{
	nSoundChannel = s3eSoundGetFreeChannel();
	if(nSoundChannel == -1)
	{
		m_strLastError.clear();
		m_strLastError = "Cannot open a sound channel.";
		s3eDebugTracePrintf("%s\n",m_strLastError.c_str());
		cleanup();
	}
	return nSoundChannel;
}

bool COggHandler::playSynchron()
{
	int16 dummydata[16];
	memset(dummydata, 0, 16);
	nStatus = OH_PLAYING;
	nSoundChannel = getSoundChannel();
	if(nSoundChannel == -1)
	{
		return false;
	}
	s3eSoundChannelPlay(nSoundChannel, dummydata,8, 1, 0);
	return true;
}

bool COggHandler::playBuffered(ogg_int16_t* pData, int iLength)
{
	cleanup();
	nStatus = OH_PLAYING;
	nSoundChannel = getSoundChannel();
	s3eSoundChannelSetInt(nSoundChannel, S3E_CHANNEL_RATE, s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ));
	s3eSoundChannelRegister(nSoundChannel, S3E_CHANNEL_END_SAMPLE, EndSampleCallback, this);
	if(nSoundChannel == -1)
	{
		return false;
	}	
	s3eSoundChannelPlay(nSoundChannel, pData, iLength, 1, 0);
	return true;
}

CUInt16ArrayReturnValue* COggHandler::decodeOggFileToArray(const std::string& strFileName)
{
	init(strFileName);
	ogg_int64_t iCount = (ogg_int64_t) (total_samples * dResampleFactor*get_nChannels() + 2);
	ogg_int16_t* pData = new ogg_int16_t[size_t(iCount)];

	int res = EOK;
	int iIndex = 0;
	while(res == EOK)
	{
		res = decode();
		s3eDeviceYield();
		ogg_int16_t iValue;
		while (mDecBuffer->Dequeue(iValue))
		{
			pData[iIndex++] = iValue;
		}
	}
	return new CUInt16ArrayReturnValue(pData, iIndex);
}

void COggHandler::decodeSynchron()
{
	int res = EOK;
	int64 before = s3eTimerGetUST();
	res = decode();
	int64 after = s3eTimerGetUST();
	while ((res == EOK && mDecBuffer->GetBusy() <= mDecBuffer->GetCapacity() * m_dSyncronBufferingMaxCapacity) &&
		((after - before) < 10))
	{
		res = decode();
		s3eDeviceYield();
		after = s3eTimerGetUST();
	}
}

bool COggHandler::play(const bool& bEnableLoop /*= false*/, const int& iLoopCount /*= 0*/, const bool& bIsBuffered /*= false*/, const bool& bIsSynchron /*= false*/, ogg_int16_t* pData /*= NULL*/, const int& iLength /*= 0*/)
{
	m_bLoop = bEnableLoop;
	m_iLoopCount = iLoopCount;
	m_bIsBuffered = bIsBuffered;

	bool bRet = false;
	if (bIsBuffered)
	{
		bRet = playBuffered(pData, iLength);
	}
	else if (bIsSynchron)
	{
		bRet = playSynchron();
	}
	else 
	{
		bRet = COggVorbisFileHelper::play();
	}
	return bRet;
}


