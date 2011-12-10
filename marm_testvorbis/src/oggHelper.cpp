#include "oggHelper.h"
#include "s3eDebug.h"
#include "s3eMemory.h"
#include "unistd.h"
#include "s3eSound.h"
#include "sound_helper.h"
#include "wsfir.h"
//////////////////////////////////////////////////////////////////////////
//	OggHelper class
//	written by Francesco Aruta
//	ilpanda@gmail.com
//////////////////////////////////////////////////////////////////////////

pthread_mutex_t mutex2;
pthread_mutex_t mutex1;

//THREAD
CThread::CThread() 
{
	thread_status = TIDLE;
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
	if (!arg) return ;
	COggVorbisFileHelper *p = (COggVorbisFileHelper *) arg;
	thread_status = TRUNNING;
	p->decode_loop();
	//((COggVorbisFileHelper*) arg)->decode_loop((COggVorbisFileHelper*) arg);
	thread_status = TIDLE;
}

int CThread::Cancel()
{
	int ret =  pthread_cancel(ThreadId_);
	if (ret)
	{
		s3eDebugErrorPrintf("pthread_cancel failed: %d", ret);
		return 0;
	}
	thread_status = TIDLE;
	return 1;
}


//////////////////////////////////////////////////////////////////////////
// COggVorbisFileHelper
//////////////////////////////////////////////////////////////////////////
COggVorbisFileHelper::COggVorbisFileHelper()
{
	mDecBuffer = NULL;
	vi = NULL;
	total_samples = 0;
	nChannels = 0;
	nRate = 0;
	nSoundChannel	= -1;
	nOutputRate		= 0;
	dResampleFactor	= 0;
	bOutputIsStereo	= -1;
	time_length = 0;
	current_time	= 0;
	current_sample = 0;
	current_section = 0;
	bStopDecoding = false;
	nStatus = OH_NAN;
	wait_counter = 0;
	nW	= 0;
	nL	= 0;

	stereoOutputMode = STEREO_MODE_MONO;

	res_contR = NULL;
	res_contL = NULL;
	rcb_len = 0;

	nResampleQuality = 0;
	bEnableResampling = false;
	bEnableFilter = false;

	memset(&vf,0,sizeof(vf));

	nFilterCoefficients = 129;
	iFilterBufferL = new int16[nFilterCoefficients];
	iFilterBufferR = new int16[nFilterCoefficients];
	dFilterCoefficients = new double[nFilterCoefficients];

	m_outbufsizeL =  _CONVSIZE_;
	m_outbufsizeR =  _CONVSIZE_;
	m_outL = new short[m_outbufsizeL];
	m_outR = new short[m_outbufsizeR];
	
}

COggVorbisFileHelper::~COggVorbisFileHelper()
{
	if(mDecBuffer != NULL)
		delete mDecBuffer;
	cleanup();

	if(res_contR)	speex_resampler_destroy(res_contR);
	if(res_contL)	speex_resampler_destroy(res_contL);

	delete [] iFilterBufferL;
	delete [] iFilterBufferR;
	delete [] dFilterCoefficients;

	delete [] m_outL;
	delete [] m_outR;
}

void COggVorbisFileHelper::cleanup()
{
	if(nSoundChannel >= 0) s3eSoundChannelStop(nSoundChannel);
	bStopDecoding = true;
	while(mDecThread.thread_status == CThread::TRUNNING) 
	{
		s3eDebugTracePrintf("waiting decoding thread terminating\n");
		s3eDeviceYield(10);
	}
	if(nSoundChannel != -1)
	{
		s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO_STEREO);
		s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO);
		s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_END_SAMPLE);
	}

	//m_resampler.clear();


	total_samples = 0;
	nChannels = 0;
	nRate = 0;
	time_length = 0;
	current_time	= 0;
	current_sample	= 0;
	current_section = 0;
	nSoundChannel	= -1;
	nOutputRate		= 0;
	bOutputIsStereo = -1;
	dResampleFactor	= 0;
	wait_counter = 0;
	nStatus = OH_NAN;
	nW	= 0;
	nL	= 0;

	bStopDecoding = false;
	stereoOutputMode = STEREO_MODE_MONO;

	if(mDecBuffer != NULL)
		delete mDecBuffer;

	mDecBuffer = NULL;

	rcb_len = 0;

	ov_clear(&vf);
	if(vi)
		vorbis_info_clear(vi);

	nResampleQuality = 0;
	bEnableResampling = false;

}

bool COggVorbisFileHelper::init( std::string fin_str,bool bResample /*= true*/,int nResQuality/*=0*/ )
{
	cleanup();

	if(mDecBuffer == NULL)
		mDecBuffer = new  Queue<ogg_int16_t>(_CIRCBUFSIZE_);  //CCircularBuffer(_CIRCBUFSIZE_);


	nSoundChannel = s3eSoundGetFreeChannel();
	if(nSoundChannel == -1)
	{
		m_strLastError.clear();
		m_strLastError = "Cannot open a sound channel.";
		s3eDebugTracePrintf("%s\n",m_strLastError.c_str());
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
		m_strLastError = "Cannot open file " + fin_str; 
		s3eDebugTracePrintf("%s\n",m_strLastError.c_str());
		cleanup();
		return false;
	}

	if(ov_open_callbacks(oggvorbis_filein, &vf, NULL, 0, callbacks) < 0)
	{
		m_strLastError.clear();
		m_strLastError = "Input does not appear to be an Ogg bitstream.";
		s3eDebugTracePrintf("%s\n",m_strLastError.c_str());
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
		total_samples = ov_pcm_total(&vf,-1);
		time_length = ov_time_total(&vf,-1);
		nChannels = vi->channels;
		nRate	= vi->rate;

		s3eSoundChannelSetInt(nSoundChannel, S3E_CHANNEL_RATE, nRate);
		//nOutputRate = s3eSoundChannelGetInt(nSoundChannel,S3E_CHANNEL_RATE);
		nOutputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);

	
		int gcd = GCD(nRate, nOutputRate);
		nW = nRate  / gcd;
		nL = nOutputRate / gcd;


		dResampleFactor = (float)nOutputRate / (float)vi->rate;	// 0 - 4.0 ?

		int fs = min(nRate, nOutputRate);
		double fc = (fs/2) / (double)nOutputRate; // half the input sample rate (eg nyquist limit of input)

		// Generate filter coefficients
		wsfirLP(dFilterCoefficients, nFilterCoefficients, W_BLACKMAN, fc);


		int err;
		bEnableResampling = bResample;
		nResampleQuality = nResQuality;

		if(bEnableResampling)
		{
			if(res_contL)	speex_resampler_destroy(res_contL);
			res_contL =  speex_resampler_init(1,nRate,nOutputRate,nResampleQuality,&err);

			
			if(res_contR) speex_resampler_destroy(res_contR);

			res_contR =  speex_resampler_init(1,nRate,nOutputRate,nResampleQuality,&err);

			if(err != RESAMPLER_ERR_SUCCESS)
			{
				m_strLastError.clear();
				m_strLastError = "Cannot start resampler.";
				s3eDebugTracePrintf("%s\n",m_strLastError.c_str());
				cleanup();
				return false;
			}
		}


		s3eDebugTracePrintf("\nBitstream is %d channel, %ldHz\n",vi->channels,vi->rate);
		s3eDebugTracePrintf("\nDecoded length: %ld samples\n",(long)total_samples);
		s3eDebugTracePrintf("Encoded by: %s\n\n",ov_comment(&vf,-1)->vendor);
		s3eDebugTracePrintf("Resampling by rational factor %d / %d", nW, nL);
	}

	bStopDecoding = false;
	nStatus = OH_READY;

	return true;
}

int COggVorbisFileHelper::decode()
{
	pthread_mutex_lock(&mutex1);
	int cb_len = sizeof(convbuffer) -rcb_len-16;
	char* p = convbuffer + 16; //pre buffer
	memcpy(p,remaing_convbuffer,rcb_len);
	p = p+rcb_len;
	long ret=ov_read_func(&vf,p,cb_len,0,2,1,&current_section);
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
			m_strLastError = "Corrupt bitstream section! Exiting.";
			s3eDebugTracePrintf("%s\n",m_strLastError.c_str());
			return ERR;
		}

    }
	else 
	{
		if(bStopDecoding) return EOS;

		int nr_samples = (ret + rcb_len)/sizeof(ogg_int16_t);
		//if(!bEnableResampling) //store samples in the circular buffer
		//{
		//	for(int k=0;k<nr_samples;k++)
		//	{
		//		ogg_int16_t val = *(ogg_int16_t*)(convbuffer+k*sizeof(ogg_int16_t));
		//		if(bStopDecoding) return EOS;
		//		if(nStatus == OH_BUFFERING)
		//		{
		//			if(mDecBuffer->GetBusy() >= mDecBuffer->GetCapacity() * 0.75)
		//			{
		//				m_strStatus = "buffering complete. Playing now..";
		//				s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
		//				nStatus = OH_PLAYING;
		//				Wait_counter(0);
		//			}
		//		}
		//		while(!mDecBuffer->Enqueue(val))
		//		{
		//			s3eDeviceYieldUntilEvent();
		//			if(bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
		//			if(nStatus == OH_BUFFERING) 
		//			{
		//				m_strStatus = "buffering complete. Playing now..";
		//				s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
		//				nStatus = OH_PLAYING;
		//				Wait_counter(0);
		//			}
		//		}

		//	}

		//	return EOK;
		//}

		if(!bEnableResampling) //store samples in the circular buffer
		{
			rcb_len = 0;
			if (get_nChannels() == 2) nr_samples /= 2;

			int kmax = (int)(nr_samples*dResampleFactor);
			for(int k=0;k<kmax;k++)
			{
				int kr = k;
				kr=(int)(k/dResampleFactor);
				if((kr+8) >= nr_samples)
				{
					p = (convbuffer+(kr)*sizeof(ogg_int16_t));
					//char* rp = remaing_convbuffer+rcb_len;
					memcpy(remaing_convbuffer,p,sizeof(ogg_int16_t)*8);
					p = (convbuffer+(kr-8)*sizeof(ogg_int16_t));
					memcpy(convbuffer,p,sizeof(ogg_int16_t)*8);
					rcb_len =sizeof(ogg_int16_t)*8;
					return EOK;
				}
				else
				{
					float pf;
					pf = 0.5f;
					ogg_int16_t valL = 0;
					ogg_int16_t valR = 0;
					int kr0,kr1;
					int sample0, sample1, sample2,sample3,sample4,sample5;
					int sampleR0, sampleR1,sampleR2,sampleR3,sampleR4,sampleR5;
	
					kr0 = kr -1;
					kr1 = kr -2;
					//if(kr <= 5) {kr1=0;kr0=0;};

					double x = ((double)k/dResampleFactor) - (double) kr;

					//if(nChannels == 1)
					//{
					//	sample0 = *(ogg_int16_t*)(convbuffer+kr*sizeof(ogg_int16_t));
					//	sample1 = *(ogg_int16_t*)(convbuffer+(kr+1)*sizeof(ogg_int16_t));
					//	sample2 = *(ogg_int16_t*)(convbuffer+(kr+2)*sizeof(ogg_int16_t));
					//}
					//else
					//{
					//	sample0 = *(ogg_int16_t*)(convbuffer+2*kr*sizeof(ogg_int16_t));
					//	sample1 = *(ogg_int16_t*)(convbuffer+(2*kr+2)*sizeof(ogg_int16_t));
					//	sample2 = *(ogg_int16_t*)(convbuffer+(2*kr+4)*sizeof(ogg_int16_t));
					//
					//	int sampleR0 = *(ogg_int16_t*)(convbuffer+(2*kr+1)*sizeof(ogg_int16_t));
					//	int sampleR1 = *(ogg_int16_t*)(convbuffer+(2*kr+3)*sizeof(ogg_int16_t));
					//	int sampleR2 = *(ogg_int16_t*)(convbuffer+(2*kr+5)*sizeof(ogg_int16_t));
					//	valR = sampleR0 + (int16)((pf/2.0f) * (pf * (sampleR0 - (2.0f * sampleR1) + sampleR2))
					//		- 3.0f * sampleR0 + 4.0f * sampleR1 - sampleR2);

					//}

					//valL = sample0 + (int16)((pf/2.0f) * (pf * (sample0 - (2.0f * sample1) + sample2))
					//	- 3.0f * sample0 + 4.0f * sample1 - sample2);

					if(nChannels == 1)
					{
						sample0=*(ogg_int16_t*)(convbuffer+(kr1)*sizeof(ogg_int16_t));
						sample1=*(ogg_int16_t*)(convbuffer+(kr0)*sizeof(ogg_int16_t));
						sample2=*(ogg_int16_t*)(convbuffer+kr*sizeof(ogg_int16_t));
						sample3=*(ogg_int16_t*)(convbuffer+(kr+1)*sizeof(ogg_int16_t));
						sample4=*(ogg_int16_t*)(convbuffer+(kr+2)*sizeof(ogg_int16_t));
						sample5=*(ogg_int16_t*)(convbuffer+(kr+3)*sizeof(ogg_int16_t));
					}
					else
					{
						sample0=*(ogg_int16_t*)(convbuffer+(2*kr1)*sizeof(ogg_int16_t));
						sample1=*(ogg_int16_t*)(convbuffer+(2*kr0)*sizeof(ogg_int16_t));
						sample2=*(ogg_int16_t*)(convbuffer+2*kr*sizeof(ogg_int16_t));
						sample3=*(ogg_int16_t*)(convbuffer+(2*kr+2)*sizeof(ogg_int16_t));
						sample4=*(ogg_int16_t*)(convbuffer+(2*kr+4)*sizeof(ogg_int16_t));
						sample5=*(ogg_int16_t*)(convbuffer+(2*kr+6)*sizeof(ogg_int16_t));

						sampleR0=*(ogg_int16_t*)(convbuffer+(2*kr1+1)*sizeof(ogg_int16_t));
						sampleR1=*(ogg_int16_t*)(convbuffer+(2*kr0+1)*sizeof(ogg_int16_t));
						sampleR2=*(ogg_int16_t*)(convbuffer+(2*kr+1)*sizeof(ogg_int16_t));
						sampleR3=*(ogg_int16_t*)(convbuffer+(2*kr+3)*sizeof(ogg_int16_t));
						sampleR4=*(ogg_int16_t*)(convbuffer+(2*kr+5)*sizeof(ogg_int16_t));
						sampleR5=*(ogg_int16_t*)(convbuffer+(2*kr+7)*sizeof(ogg_int16_t));


						//valR = (ogg_int16_t) (sampleR2 + 0.04166666666*x*((sampleR3-sampleR1)*16.0+(sampleR0-sampleR4)*2.0
						//	+ x *((sampleR3+sampleR1)*16.0-sampleR0-sampleR2*30.0- sampleR4
						//	+ x *(sampleR3*66.0-sampleR2*70.0-sampleR4*33.0+sampleR1*39.0+ sampleR5*7.0- sampleR0*9.0
						//	+ x *( sampleR2*126.0-sampleR3*124.0+sampleR4*61.0-sampleR1*64.0- sampleR5*12.0+sampleR0*13.0
						//	+ x *((sampleR3-sampleR2)*50.0+(sampleR1-sampleR4)*25.0+(sampleR5-sampleR0)*5.0))))));

						float c = (float)(sampleR2 - sampleR1) * 0.5f;
						float v = (float)(sampleR2 - sampleR3);
						float w = c + v;
						float a = w + v + (float)(sampleR3 - sampleR2) * 0.5f;
						float b_neg = w + a;

						valR = (ogg_int16_t)((((a * x) - b_neg) * x + c) * x + sampleR2);

					}

					//valL = (ogg_int16_t) (sample2 + 0.04166666666*x*((sample3-sample1)*16.0+(sample0-sample4)*2.0
					//	+ x *((sample3+sample1)*16.0-sample0-sample2*30.0- sample4
					//	+ x *(sample3*66.0-sample2*70.0-sample4*33.0+sample1*39.0+ sample5*7.0- sample0*9.0
					//	+ x *( sample2*126.0-sample3*124.0+sample4*61.0-sample1*64.0- sample5*12.0+sample0*13.0
					//	+ x *((sample3-sample2)*50.0+(sample1-sample4)*25.0+(sample5-sample0)*5.0))))));

						float c = (float)(sample2 - sample1) * 0.5f;
						float v = (float)(sample2 - sample3);
						float w = c + v;
						float a = w + v + (float)(sample3 - sample2) * 0.5f;
						float b_neg = w + a;

						valL = (ogg_int16_t)((((a * x) - b_neg) * x + c) * x + sample2);
				


					if (bEnableFilter)
					{
						// apply FIR filter to output to remove artifacts
						static int bufferPos = -1;
						bufferPos = (bufferPos + 1) % nFilterCoefficients;

						// use circular buffer to store previous inputs
						iFilterBufferL[bufferPos] = valL;

						valL = 0;
						for (int i=0; i< nFilterCoefficients; i++)
						{
							valL += (int16)(dFilterCoefficients[i] * iFilterBufferL[(bufferPos + i) % nFilterCoefficients]);
						}

						if(nChannels == 2)
						{
							iFilterBufferR[bufferPos] = valR;
							valR = 0;
							for (int i=0; i< nFilterCoefficients; i++)
							{
								valR += (int16)(dFilterCoefficients[i] * iFilterBufferR[(bufferPos + i) % nFilterCoefficients]);
							}
						}
					}



					if(bStopDecoding) return EOS;
					if(nStatus == OH_BUFFERING)
					{
						if(mDecBuffer->GetBusy() >= mDecBuffer->GetCapacity() * 0.75)
						{
							m_strStatus = "buffering complete. Playing now..";
							s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
							nStatus = OH_PLAYING;
							Wait_counter(0);
						}
					}
					while(!mDecBuffer->Enqueue(valL))
					{
						s3eDeviceYieldUntilEvent();
						if(bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
						if(nStatus == OH_BUFFERING) 
						{
							m_strStatus = "buffering complete. Playing now..";
							s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
							nStatus = OH_PLAYING;
							Wait_counter(0);
						}
					}
					if(get_nChannels() == 2)
					{
						while(!mDecBuffer->Enqueue(valR))
						{
							s3eDeviceYieldUntilEvent();
							if(bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
							if(nStatus == OH_BUFFERING) 
							{
								m_strStatus = "buffering complete. Playing now..";
								s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
								nStatus = OH_PLAYING;
								Wait_counter(0);
							}
						}
					}
				}
			}

			return EOK;
		}


		if (get_nChannels() == 2) nr_samples /= 2;

		for(int k=0;k<nr_samples;k++)
		{
			if (get_nChannels() == 2) 
			{
				m_tmpbufL[k] = *(ogg_int16_t*)(convbuffer+k*2*sizeof(ogg_int16_t));
				m_tmpbufR[k] = *(ogg_int16_t*)(convbuffer+(k*2+1)*sizeof(ogg_int16_t));
			} 
			else 
			{
				m_tmpbufL[k] = *(ogg_int16_t*)(convbuffer+k*sizeof(ogg_int16_t));
			}
		}
		
		unsigned int inlengthL = nr_samples;
		unsigned int inlengthR = nr_samples;

		unsigned int inused = 0;   // output
		unsigned int outputL,outputR;
		outputL= m_outbufsizeL;
		outputR= m_outbufsizeR;
		if (get_nChannels() == 2)  // stereo input
		{		

			speex_resampler_process_int(res_contL,0,m_tmpbufL,&inlengthL,m_outL,&outputL);
			speex_resampler_process_int(res_contR,0,m_tmpbufR,&inlengthR,m_outR,&outputR);


			if(outputL != outputR)
			{
				s3eDebugTracePrintf("Left and Right channels out of sync\n");
			}

			if(inlengthL != inlengthR)
			{
				s3eDebugTracePrintf("Left and Right channels out of sync\n");
			}

			inused = inlengthL*2;

		}
		else
		{
			speex_resampler_process_interleaved_int(res_contL,m_tmpbufL,&inlengthL,m_outL,&outputL);
			
			inused = inlengthL;

		}


		p = convbuffer + inused * sizeof(ogg_int16_t);
		rcb_len = ret- inused*sizeof(ogg_int16_t);

		memcpy(remaing_convbuffer,p,rcb_len);

		for(unsigned int k = 0;k< outputL;k++)
		{
			if(bStopDecoding) return EOS;
			if(nStatus == OH_BUFFERING)
			{
				if(mDecBuffer->GetBusy() >= mDecBuffer->GetCapacity() * 0.75)
				{
					m_strStatus = "buffering complete. Playing now..";
					s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
					nStatus = OH_PLAYING;
					Wait_counter(0);
				}
			}
			
			while(!mDecBuffer->Enqueue((ogg_int16_t)m_outL[k]))
			{
				s3eDeviceYieldUntilEvent();
				if(bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
				if(nStatus == OH_BUFFERING) 
				{
					m_strStatus = "buffering complete. Playing now..";
					s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
					nStatus = OH_PLAYING;
					Wait_counter(0);
				}
			}
			if(get_nChannels() == 2)
			{
				while(!mDecBuffer->Enqueue((ogg_int16_t)m_outR[k]))
				{
					s3eDeviceYieldUntilEvent();
					if(bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
					if(nStatus == OH_BUFFERING) 
					{
						m_strStatus = "buffering complete. Playing now..";
						s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
						nStatus = OH_PLAYING;
						Wait_counter(0);
					}
				}
			}
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
	;
}

bool COggVorbisFileHelper::IsLastSample()
{
	if ((current_sample+1) >= total_samples* (ogg_int64_t)dResampleFactor*get_nChannels()) return true;
	return false;
}

ogg_int16_t COggVorbisFileHelper::get_sample()
{
	ogg_int16_t res = 0;

	if (IsLastSample())
	{
		return res;
	}
	
	while(!mDecBuffer->Dequeue(res))
	{
		if(nStatus != OH_PLAYING) return 0;
		m_strStatus = "Buffer under run";
		s3eDebugTracePrintf("%s\n",m_strStatus.c_str());
	}
	current_sample++;
	if(get_nChannels() == 2 )
		current_time = (double)current_sample/nOutputRate/2;
	else
		current_time = (double)current_sample/nOutputRate;
	

	return res;
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

	// For stereo output, info->m_NumSamples is number of l/r pairs (each sample is 32bit)
	// info->m_OrigNumSamples always measures the total number of 16 bit samples,
	// regardless of whether input was mono or stereo.

	if(!info->m_Mix)
		memset(info->m_Target, 0, info->m_NumSamples * outputSampleSize * sizeof(int16));

	int nWC = ogg_hlp->Wait_counter();
	if((ogg_hlp->get_status() == OH_BUFFERING) || (nWC < 5))
	{
		char str_tmp[1024];
		sprintf(str_tmp,"Buffering. Free space: %d\nSamples: %d\nCounter:%d\n",
			ogg_hlp->mDecBuffer->GetBusy(),
			info->m_NumSamples,
			nWC);
		s3eDebugTracePrintf("%s\n",str_tmp);

		if(ogg_hlp->get_status() == OH_PLAYING)
		{
			nWC++;
			ogg_hlp->Wait_counter(nWC);
		}
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

		
		// copy left (and right) 16bit sample directly from input buffer
		if(ogg_hlp->IsLastSample())
		{
			yLeft = yRight = 0;
			info->m_EndSample = true;
			return samplesPlayed;
		}
		else
		{
			yLeft = ogg_hlp->get_sample();
			if (ogg_hlp->get_nChannels() == 2)
			{
				yRight = ogg_hlp->get_sample();
				if (ogg_hlp->stereoOutputMode == STEREO_MODE_MONO)
						yRight = 0;
			}
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
	
	COggVorbisFileHelper* ogg_hlp = (COggVorbisFileHelper*) user;
	ogg_hlp->set_status(COggVorbisFileHelper::OH_STOPPED);

	return info->m_RepsRemaining;
}

bool COggVorbisFileHelper::play()
{
	int16 dummydata[16];
	memset(dummydata, 0, 16);
	if( nStatus == OH_STOPPED)
	{
		stop();
		nStatus = OH_BUFFERING;
		bStopDecoding = false;
		if(mDecThread.thread_status == CThread::TIDLE) mDecThread.Start(this);
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
		bStopDecoding = false;
		if(mDecThread.thread_status == CThread::TIDLE) mDecThread.Start(this);
		s3eSoundChannelPlay(nSoundChannel, dummydata,8, 1, 0);
		return true;
	}
	return false;
}

bool COggVorbisFileHelper::stop()
{
	if(nSoundChannel >= 0) s3eSoundChannelStop(nSoundChannel);
	s3eDeviceYield(1);
	if(vf.datasource && vf.seekable)
	{
		if(mDecThread.thread_status == CThread::TRUNNING)	pthread_mutex_lock(&mutex1);
		ov_time_seek(&vf,0);
		current_time = 0;
		current_sample = 0;
		if(mDecThread.thread_status == CThread::TRUNNING)	pthread_mutex_unlock(&mutex1);
		Wait_counter(0);
		mDecBuffer->Clear();
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

bool COggVorbisFileHelper::set_current_timepos( double pos )
{
	if(vf.datasource && vf.seekable)
	{
		s3eSoundChannelPause(nSoundChannel);
		s3eDeviceYield(1);
		if(mDecThread.thread_status == CThread::TRUNNING)	pthread_mutex_lock(&mutex1);
		ov_time_seek_func(&vf,pos);
		current_time = pos;
		current_sample = ov_pcm_tell(&vf) * (ogg_int64_t)dResampleFactor*get_nChannels();
		if(mDecThread.thread_status == CThread::TRUNNING)	pthread_mutex_unlock(&mutex1);
		nStatus = OH_BUFFERING;
		mDecBuffer->Clear();
		Wait_counter(0);
		s3eSoundChannelResume(nSoundChannel);
		return true;
	}
	return false;
}

bool COggVorbisFileHelper::set_outputStereoMode( STEREO_MODE val )
{
	s3eResult res = S3E_RESULT_ERROR;
	int32 bStereo = 0;
	if(nSoundChannel == -1) return false;
	if (val != STEREO_MODE_MONO)
	{
		bStereo = s3eSoundGetInt(S3E_SOUND_STEREO_ENABLED);
		if(!bStereo)
		{
			s3eDebugTracePrintf("Stereo mode not supported\n");
			return false;
		}
		res =  s3eSoundChannelRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO_STEREO, GenerateAudioCallback, this);
		if(res == S3E_RESULT_ERROR)
		{
			s3eDebugTracePrintf("Stereo mode cannot be set\n"); 
			return false;
		}
	}
	else
	{
	    s3eSoundChannelUnRegister(nSoundChannel, S3E_CHANNEL_GEN_AUDIO_STEREO);
	}

	stereoOutputMode = val;
	return true;
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

std::string COggVorbisFileHelper::get_statusstr()
{
	switch (nStatus)
	{
	case  OH_NAN:
		m_strStatus = "NA";
		break;
	case  OH_READY:
		m_strStatus = "READY";
		break;
	case  OH_PLAYING:
		m_strStatus = "PLAYING";
		break;
	case  OH_STOPPED:
		m_strStatus = "STOPPED";
		break;
	case  OH_PAUSED:
		m_strStatus = "PAUSED";
		break;
	case  OH_ERROR:
		m_strStatus = "ERROR";
		break;
	case  OH_BUFFERING:
		m_strStatus = "BUFFERING";
		break;
	}

	return m_strStatus;
}

void COggVorbisFileHelper::set_status( OHStatus status )
{
	nStatus = status;
	get_status();
}


