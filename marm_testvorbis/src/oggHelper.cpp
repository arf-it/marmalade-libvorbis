#include "oggHelper.h"
#include "s3eDebug.h"
#include "s3eMemory.h"
#include "unistd.h"
#include "s3eSound.h"
#include "sound_helper.h"

pthread_mutex_t mutex1;
//THREAD
CThread::CThread() 
{

}

int CThread::Start(void * arg)
{
	Arg(arg); // store user data
	int ret = pthread_create(&ThreadId_, NULL, EntryPoint, this);
	if (ret)
	{
		s3eDebugErrorPrintf("s3eThreadCreate failed: %d", ret);
		return 0;
	}
	return 1;
}

int CThread::Run(void * arg)
{
	Setup();
	Execute( Arg() );
	return 1;
}

/*static */
void* CThread::EntryPoint(void * pthis)
{
	if(!pthis) return 0;
	CThread * pt = (CThread*)pthis;
	pt->Run( pt->Arg() );
	return 0;
}

void CThread::Setup()
{
	// Do any setup here
}

void CThread::Execute(void* arg)
{
	if (!arg) return;
	((COggVorbisFileHelper*) arg)->decode_loop();

}

int CThread::Cancel()
{

	return 0;
}

//CIRCULAR BUFFER

CCircularBuffer::CCircularBuffer (unsigned int size) :
aInternalBuffer(size), iBufferSize(size), rIdx(0), wIdx(0),
	bBufferIsEmpty(true), bBufferIsFull(false), count(0) {}

bool CCircularBuffer::read(ogg_int16_t& result)
{

	if (bBufferIsEmpty)
	{
		bBufferIsFull = false;
		return false;
	}
	
	result = aInternalBuffer[rIdx];
	rIdx = (rIdx + 1) % iBufferSize;
	bBufferIsFull = false; // buffer can't be full after a read
	bBufferIsEmpty = (rIdx == wIdx); // true if read catches up to write

	return true;
}

bool CCircularBuffer::write (ogg_int16_t value)
{
	if (bBufferIsFull)
	{
		bBufferIsEmpty = false;

		return false;
	}

	aInternalBuffer[wIdx] = value;
	wIdx = (wIdx + 1) % iBufferSize;

	bBufferIsEmpty = false; // buffer can't be empty after a write
	bBufferIsFull = (wIdx == rIdx); // true if write catches up to read

	return true;
}

void CCircularBuffer::clear()
{
	wIdx = rIdx = 0;
	bBufferIsFull = false;
	bBufferIsEmpty = true;
}

unsigned int CCircularBuffer::get_freespace()
{
	unsigned int remaining = (wIdx >= rIdx)
		? iBufferSize - wIdx + rIdx
		: rIdx - wIdx;

	return remaining;

}

//OGG_HELPER
//////////////////////////////////////////////////////////////////////////
// COggVorbisFileHelper
//////////////////////////////////////////////////////////////////////////
COggVorbisFileHelper::COggVorbisFileHelper()
{
	mDecBuffer = NULL;
	vi = NULL;
	nSamples = 0;
	nChannels = 0;
	nRate = 0;
	nSoundChannel	= -1;
	nOutputRate		= 0;
	dResampleFactor	= 0;
	bOutputIsStereo	= -1;
	time_length = 0;
	current_time	= 0;
	current_section = 0;
	m_bStopDecoding = false;
	nStatus = OH_NAN;
	wait_counter = 0;
	nW	= 0;
	nL	= 0;

	stereoOutputMode = STEREO_MODE_MONO;
	conversionType = ZERO_ORDER_HOLD;

	memset(&vf,0,sizeof(vf));
}

COggVorbisFileHelper::~COggVorbisFileHelper()
{
	if(mDecBuffer != NULL)
		delete mDecBuffer;
	cleanup();
}

void COggVorbisFileHelper::cleanup()
{

	nSamples = 0;
	nChannels = 0;
	nRate = 0;
	time_length = 0;
	current_time	= 0;
	current_section = 0;
	nSoundChannel	= -1;
	nOutputRate		= 0;
	bOutputIsStereo = -1;
	dResampleFactor	= 0;
	wait_counter = 0;
	nStatus = OH_NAN;
	nW	= 0;
	nL	= 0;
	m_bStopDecoding = false;
	
	stereoOutputMode = STEREO_MODE_MONO;
	conversionType = ZERO_ORDER_HOLD;

	if(nSoundChannel != -1)
	{
		s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO_STEREO);
		s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO);
		s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_END_SAMPLE);
	}
	ov_clear(&vf);
	if(vi)
		vorbis_info_clear(vi);
}

bool COggVorbisFileHelper::init( std::string fin_str )
{
	if(mDecBuffer == NULL)
		mDecBuffer = new CCircularBuffer(_CIRCBUFSIZE_);

	cleanup();

	nSoundChannel = s3eSoundGetFreeChannel();
	if(nSoundChannel == -1)
	{
		m_strLastError.clear();
		m_strLastError = "Cannot open a sound channel.";
		s3eDebugTracePrintf("Cannot open a sound channel.\n");
		cleanup();
		return false;
	}
	s3eSoundChannelRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO, GenerateAudioCallback, this);
	s3eSoundChannelRegister(nSoundChannel, S3E_CHANNEL_END_SAMPLE, EndSampleCallback, this);

	ov_callbacks callbacks;
	callbacks.read_func = read_func;
	callbacks.seek_func = seek_func;
	callbacks.close_func = close_func;
	callbacks.tell_func = tell_func;

	oggvorbis_filein = s3eFileOpen(fin_str.c_str(),"rb");
	if(oggvorbis_filein == NULL)
	{
		m_strLastError.clear();
		s3eDebugTracePrintf("Cannot open file '%s'.\n",fin_str.c_str());
		m_strLastError = "Cannot open file " + fin_str; 
		cleanup();
		return false;
	}

	if(ov_open_callbacks(oggvorbis_filein, &vf, NULL, 0, callbacks) < 0) 
	{
		m_strLastError.clear();
		m_strLastError = "Input does not appear to be an Ogg bitstream.";
		s3eDebugTracePrintf("Input does not appear to be an Ogg bitstream.\n");
		cleanup();
		return false;
	}

	/* Throw the comments plus a few lines about the bitstream we're
		decoding */
	{
		char **ptr=ov_comment(&vf,-1)->user_comments;
		vorbis_info *vi=ov_info(&vf,-1);
		while(*ptr)
		{
			fprintf(stderr,"%s\n",*ptr);
			++ptr;
		}
		nSamples = ov_pcm_total(&vf,-1);
		time_length = ov_time_total(&vf,-1);
		nChannels = vi->channels;
		nRate	= vi->rate;

		s3eSoundChannelSetInt(nSoundChannel, S3E_CHANNEL_RATE, nRate);
		nOutputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);
		
		int gcd = GCD(nRate, nOutputRate);
		nW = nRate  / gcd;
		nL = nOutputRate / gcd;

		// As a float scale factor. Multiply output pos by this to find input.
		dResampleFactor = nW / (float) nL;

		s3eDebugTracePrintf("\nBitstream is %d channel, %ldHz\n",vi->channels,vi->rate);
		s3eDebugTracePrintf("\nDecoded length: %ld samples\n",(long)nSamples);
		s3eDebugTracePrintf("Encoded by: %s\n\n",ov_comment(&vf,-1)->vendor);
		s3eDebugTracePrintf("Resampling by rational factor %d / %d", nW, nL);
	}

	m_bStopDecoding = false;
	nStatus = OH_READY;

	return EOK;
}

int COggVorbisFileHelper::decode()
{
	pthread_mutex_lock(&mutex1);
	long ret=ov_read_func(&vf,convbuffer,sizeof(convbuffer),0,2,1,&current_section);
	current_time = ov_time_tell_func(&vf);
	pthread_mutex_unlock(&mutex1);
    if (ret == 0)
	{
      /* EOF */
      return EOF;
    } 
	else if (ret < 0) 
	{
		if(ret==OV_EBADLINK)
		{
			fprintf(stderr,"Corrupt bitstream section! Exiting.\n");
			return ERR;
		}

      /* some other error in the stream.  Not a problem, just reporting it in
         case we (the app) cares.  In this case, we don't. */
    }
	else 
	{
		/* we don't bother dealing with sample rate changes, etc, but
			you'll have to*/
		//fwrite(pcmout,1,ret,stdout);

		if(m_bStopDecoding) return EOS;

		for(unsigned int k=0;k<ret/sizeof(ogg_int16_t);k++)
		{
			if(m_bStopDecoding) return EOS;
			//if((nStatus == OH_BUFFERING) &&
			//	(mDecBuffer->get_freespace() <= mDecBuffer->get_bufferSize() / 2))
			//{
			//	s3eDebugTracePrintf("buffering complete. Playing now..\n");
			//	Wait_counter(0);
			//	nStatus = OH_PLAYING;
			//}
			while(mDecBuffer->get_freespace() <= mDecBuffer->get_bufferSize() / 4)
			{
				usleep(50);
				if(nStatus == OH_BUFFERING) 
				{
					s3eDebugTracePrintf("buffering complete. Playing now..\n");
					nStatus = OH_PLAYING;
					Wait_counter(0);
				}
				if(m_bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
			}
			ogg_int16_t* p = (ogg_int16_t*)(convbuffer+k*sizeof(ogg_int16_t));
			mDecBuffer->write(*p);
		}

    }

	return EOK;
}

void COggVorbisFileHelper::decode_loop()
{
	int res = EOK;
	while(res == EOK)
	{
		res = decode();
	}
}

ogg_int16_t COggVorbisFileHelper::get_sample()
{
	ogg_int16_t res = 0;
	while(!mDecBuffer->read(res))
	{
		if(m_bStopDecoding) return 0;
		s3eDebugTracePrintf("Buffer under run\n"); //wait
	}

	return res;
}

bool COggVorbisFileHelper::set_current_timepos( double pos )
{
	if(vf.datasource && vf.seekable)
	{
		s3eSoundChannelPause(nSoundChannel);
		mDecBuffer->clear();
		pthread_mutex_lock(&mutex1);
		ov_time_seek_func(&vf,pos);
		pthread_mutex_unlock(&mutex1);
		nStatus = OH_BUFFERING;
		s3eSoundChannelResume(nSoundChannel);
		return true;
	}
	return false;
}

size_t COggVorbisFileHelper::read_func(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	return s3eFileRead(ptr, size, nmemb, (s3eFile*)datasource);
}

int COggVorbisFileHelper::seek_func(void *datasource, ogg_int64_t offset, int whence)
{
	return s3eFileSeek((s3eFile*)datasource, (int)offset, (s3eFileSeekOrigin)whence);
}

int COggVorbisFileHelper::close_func(void *datasource)
{
	return s3eFileClose((s3eFile*)datasource);
}

long COggVorbisFileHelper::tell_func(void *datasource)
{
	return s3eFileTell((s3eFile*)datasource);
}

int COggVorbisFileHelper::GenerateAudioCallback( void* sys, void* user )
{
	s3eTimerGetMs();
	s3eSoundGenAudioInfo* info = (s3eSoundGenAudioInfo*)sys;
	info->m_EndSample = 0;
	int16* target = (int16*)info->m_Target;


	if(user == NULL)
		return 0;

	// The user value is the pointer to ogg_hlp object. 

	COggVorbisFileHelper* ogg_hlp = (COggVorbisFileHelper*) user;
	
	int inputSampleSize = 1;
	int outputSampleSize = 1;

	if (info->m_Stereo)
		outputSampleSize = 2;
	ogg_hlp->set_outputIsStereo(info->m_Stereo == 2);

	inputSampleSize = ogg_hlp->get_nChannels();

	int samplesPlayed = 0;
	float dResFactor = ogg_hlp->get_dResampleFactor();

	// For stereo output, info->m_NumSamples is number of l/r pairs (each sample is 32bit)
	// info->m_OrigNumSamples always measures the total number of 16 bit samples,
	// regardless of whether input was mono or stereo.

	
	int nWC = ogg_hlp->Wait_counter();
	if((ogg_hlp->get_status() == OH_BUFFERING) || (nWC < 5))
	{
		s3eDebugTracePrintf("Buffering. Free space: %d\nSamples: %d",
			ogg_hlp->mDecBuffer->get_freespace(),
			info->m_NumSamples); //wait

		memset(info->m_Target, 0, info->m_NumSamples * outputSampleSize * sizeof(int16));
		nWC++;
		ogg_hlp->Wait_counter(nWC);
		return info->m_NumSamples;
	}
	// Loop through samples (mono) or sample-pairs (stereo) required.
	// If stereo, we copy the 16bit sample for each l/r channel and do per
	// left/right channel processing on each sample for the pair. i needs
	// scaling when comparing to input sample count as that is always 16bit.

	for (uint i = 0; i < info->m_NumSamples; i++)
	{
		int16 yLeft = 0;  // or single sample if using mono input
		int16 yRight = 0;

		// Number of samples to play in total needs scaling by resample factor
		//int inputSamplesUsed = (outputStartPos + i) * inputSampleSize;
		// Stop when hitting end of data. Must scale to 16bit if stereo
		// (m_OrigNumSamples is always 16bit) and by resample factor as we're
		// looping through output position, not input.

		// For each sample (pair) required, we either do:
		//  * get mono sample if input is mono (output can be either)
		//  * get left sample if input is stereo (output can be either)
		//  * get right sample if input and output are both stereo

		int outPosLeft = i * inputSampleSize;

		if (ogg_hlp->conversionType != NO_RESAMPLE)
		{
			outPosLeft = (int)(outPosLeft * dResFactor);
			for(int k=0;k<dResFactor-1;k++)
			{
				yLeft = ogg_hlp->get_sample();
				if (ogg_hlp->get_nChannels() == 2)
				{
					yRight = ogg_hlp->get_sample();
				}
			}
		}
		switch (ogg_hlp->conversionType)
		{
		case NO_RESAMPLE:
			{
				// copy left (and right) 16bit sample directly from input buffer
				yLeft = ogg_hlp->get_sample();

				if (ogg_hlp->get_nChannels() == 2)
				{
					yRight = ogg_hlp->get_sample();
					if (ogg_hlp->stereoOutputMode == STEREO_MODE_MONO)
						yRight = 0;
				}


				break;
			}
		case ZERO_ORDER_HOLD:
			{
				//yLeft = info->m_OrigStart[outPosLeft];
				yLeft = ogg_hlp->get_sample();
				if (ogg_hlp->get_nChannels() == 2)
				{
					yRight = ogg_hlp->get_sample();
					if (ogg_hlp->stereoOutputMode == STEREO_MODE_MONO)
						yRight = 0;
				}
				break;
			}
		case FIRST_ORDER_INTERPOLATION:
			break;
		case QUADRATIC_INTERPOLATION:
			break;
		
		}


		int16 orig = 0;
		int16 origR = 0;
		if (info->m_Mix)
		{
			orig = *target;
			origR = *(target+1);
		}


		switch (ogg_hlp->stereoOutputMode)
		{
		case STEREO_MODE_BOTH:
			*target++ = ClipToInt16(yLeft + orig);

			if (info->m_Stereo)
				*target++ = ClipToInt16(yRight + origR);
			else
				*target++ = ClipToInt16(yLeft + orig);

			break;

		case STEREO_MODE_LEFT:
			*target++ = ClipToInt16(yLeft + orig);

			if (info->m_Stereo)
				*target++ = ClipToInt16(origR);
			else
				*target++ = ClipToInt16(orig);

			break;

		case STEREO_MODE_RIGHT:
			*target++ = ClipToInt16(orig);
			if (info->m_Stereo)
				*target++ = ClipToInt16(yRight +  origR);
			else
				*target++ = ClipToInt16(yLeft +  orig);

			break;

		default: //Mono
			if (ogg_hlp->get_nChannels() == 2)
				*target++ = ClipToInt16(yLeft + orig+yRight+origR);
			else
				*target++ = ClipToInt16(yLeft + orig);
			break;
		}

		samplesPlayed++;
	}


	// Inform s3e sound how many samples we played
	return samplesPlayed;
}

int32 COggVorbisFileHelper::EndSampleCallback( void* sys, void* user )
{
	s3eSoundEndSampleInfo* info = (s3eSoundEndSampleInfo*)sys;

	return info->m_RepsRemaining;
}

bool COggVorbisFileHelper::play()
{
	int16 dummydata[16];
	memset(dummydata, 0, 16);
	if( nStatus == OH_STOPPED)
	{
		m_bStopDecoding = false;
		s3eSoundChannelPlay(nSoundChannel, dummydata,8, 1, 0);
		return true;
	}
	if (nStatus == OH_PAUSED) 
	{
		resume();
		return true;
	}
	if(nStatus == OH_READY)
	{
		//buffering
		nStatus = OH_BUFFERING;
		m_bStopDecoding = false;
		mDecThread.Start(this);
		s3eSoundChannelPlay(nSoundChannel, dummydata,8, 1, 0);
		return true;
	}
	return false;
}

bool COggVorbisFileHelper::stop()
{
	s3eSoundChannelStop(nSoundChannel);
	if(vf.datasource && vf.seekable)
	{
		pthread_mutex_lock(&mutex1);
		ov_time_seek(&vf,0);
		pthread_mutex_unlock(&mutex1);
		Wait_counter(0);
		mDecBuffer->clear();
	}
	nStatus = OH_STOPPED;
	return true;
}

bool COggVorbisFileHelper::pause()
{
	s3eSoundChannelPause(nSoundChannel);
	nStatus = OH_PAUSED;
	return true;
}

bool COggVorbisFileHelper::resume()
{
	s3eSoundChannelResume(nSoundChannel);
	nStatus = OH_PLAYING;
	return true;
}

void COggVorbisFileHelper::set_outputStereoMode( STEREO_MODE val )
{
	if(nSoundChannel == -1) return;
	stereoOutputMode = val;
	if (stereoOutputMode != STEREO_MODE_MONO)
	{
	    s3eSoundChannelRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO_STEREO, GenerateAudioCallback, this);
	}
	else
	{
	    s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO_STEREO);
	}
}



long COggVorbisFileHelper::ov_read_func( OggVorbis_File *vf,char *buffer,int length, int bigendianp,int word,int sgned,int *bitstream )
{
	long ret;
#ifdef _USELIBTREMOR
	ret=ov_read(vf,convbuffer,sizeof(convbuffer),&current_section);
#else
	ret=ov_read(vf,convbuffer,sizeof(convbuffer),0,2,1,&current_section);
#endif
	return ret;
}

double COggVorbisFileHelper::ov_time_tell_func( OggVorbis_File *vf )
{
#ifdef _USELIBTREMOR
	ogg_int64_t tt = 0;
	tt = ov_time_tell(vf);
	return (double)tt / 1000;
#else
	double tt = 0;
	tt = ov_time_tell(vf);
	return tt;
#endif
}

double COggVorbisFileHelper::ov_time_total_func( OggVorbis_File *vf,int i )
{
#ifdef _USELIBTREMOR
	ogg_int64_t tt = 0;
	tt = ov_time_total(vf,-1);
	return (double)tt / 1000;
#else
	double tt = 0;
	tt = ov_time_total(vf,-1);
	return tt;
#endif
}

int COggVorbisFileHelper::ov_time_seek_func( OggVorbis_File *vf,double pos )
{
#ifdef _USELIBTREMOR
	ogg_int64_t ipos = 0;
	ipos = pos * 1000; 
	return ov_time_seek(vf,ipos);
#else
	return ov_time_seek(vf,pos);
#endif
}


