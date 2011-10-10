#include "oggHelper.h"
#include "s3eDebug.h"
#include "s3eMemory.h"
#include "unistd.h"


//THREAD
CThread::CThread() 
{
	ThreadId_ = NULL;
}

int CThread::Start(void * arg)
{
	Arg(arg); // store user data
	//int code = pthread_create(Thread::EntryPoint, this, & ThreadId_);
	//int code = pthread_create(&ThreadId_,NULL,CThread::EntryPoint,this);
	ThreadId_ = s3eThreadCreate(EntryPoint,this, NULL);
	if (!ThreadId_)
	{
		s3eDebugErrorPrintf("s3eThreadCreate failed: %s", s3eThreadGetErrorString());
		return 0;
	}
	//pino = this;
	return 1;
}

int CThread::Run(void * arg)
{
	Setup();
	Execute( Arg() );
	return 1;
}

/*static */
extern COggHelper* ogg_hlp;
extern s3eMemoryUsrMgr mgr;
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
	if(ThreadId_)
		s3eThreadCancel(ThreadId_);

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

	//if(wIdx == 0)
	//	fprintf(stderr,"wdix 0\n\n\n\n");
	//if(rIdx == 0)
	//	fprintf(stderr,"rdix 0\n\n\n\n");

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

//OGG_HELPER

COggHelper::COggHelper()
{
	convsize = _CONVSIZE_;
	ogg_filein	= NULL;
	mDecBuffer	= NULL;
	m_bStopDecoding = false;

	memset(&os,0,sizeof(os));
	memset(&vc,0,sizeof(vc));
	memset(&vi,0,sizeof(vi));
	memset(&oy,0,sizeof(oy));

}


COggHelper::~COggHelper()
{
	if(mDecBuffer != NULL)
		delete mDecBuffer;
	cleanup();
}

bool COggHelper::init_ogg( std::string fin_str )
{
	if(mDecBuffer == NULL)
		mDecBuffer = new CCircularBuffer(_CIRCBUFSIZE_);


	cleanup();

	ogg_sync_init(&oy); /* Now we can read pages */

    int i;

    /* grab some data at the head of the stream. We want the first page
       (which is guaranteed to be small and only contain the Vorbis
       stream initial header) We need the first page to get the stream
       serialno. */

	ogg_filein = fopen(fin_str.c_str(),"rb");
	if(ogg_filein == NULL)
	{
		fprintf(stderr,"Cannot open file '%s'.\n",fin_str.c_str());
		cleanup();
		return false;
	}
    /* submit a 4k block to libvorbis' Ogg layer */
    buffer=ogg_sync_buffer(&oy,4096);
    bytes=fread(buffer,1,4096,ogg_filein);
    ogg_sync_wrote(&oy,bytes);
    
    /* Get the first page. */
    if(ogg_sync_pageout(&oy,&og)!=1)
	{
      /* have we simply run out of data?  If so, we're done. */
      //if(bytes<4096);
      
      /* error case.  Must not be Vorbis data */
      fprintf(stderr,"Input does not appear to be an Ogg bitstream.\n");
	  cleanup();
      return false;
    }

	    /* Get the serial number and set up the rest of decode. */
    /* serialno first; use it to set up a logical stream */
    ogg_stream_init(&os,ogg_page_serialno(&og));
    
    /* extract the initial header from the first page and verify that the
       Ogg bitstream is in fact Vorbis data */
    
    /* I handle the initial header first instead of just having the code
       read all three Vorbis headers at once because reading the initial
       header is an easy way to identify a Vorbis bitstream and it's
       useful to see that functionality seperated out. */
    
    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    if(ogg_stream_pagein(&os,&og)<0){ 
      /* error; stream version mismatch perhaps */
      fprintf(stderr,"Error reading first page of Ogg bitstream data.\n");
	  cleanup();
      return false;
    }
    
    if(ogg_stream_packetout(&os,&op)!=1){ 
      /* no page? must not be vorbis */
      fprintf(stderr,"Error reading initial header packet.\n");
	  cleanup();
     return false;
    }
    
    if(vorbis_synthesis_headerin(&vi,&vc,&op)<0){ 
      /* error case; not a vorbis header */
      fprintf(stderr,"This Ogg bitstream does not contain Vorbis "
              "audio data.\n");
	  cleanup();
     return false;
    }
    
    /* At this point, we're sure we're Vorbis. We've set up the logical
       (Ogg) bitstream decoder. Get the comment and codebook headers and
       set up the Vorbis decoder */
    
    /* The next two packets in order are the comment and codebook headers.
       They're likely large and may span multiple pages. Thus we read
       and submit data until we get our two packets, watching that no
       pages are missing. If a page is missing, error out; losing a
       header page is the only place where missing data is fatal. */
    
    i=0;
    while(i<2)
	{
      while(i<2)
	  {
        int result=ogg_sync_pageout(&oy,&og);
        if(result==0)break; /* Need more data */
        /* Don't complain about missing or corrupt data yet. We'll
           catch it at the packet output phase */
        if(result==1){
          ogg_stream_pagein(&os,&og); /* we can ignore any errors here
                                         as they'll also become apparent
                                         at packetout */
          while(i<2){
            result=ogg_stream_packetout(&os,&op);
            if(result==0)break;
            if(result<0){
              /* Uh oh; data at some point was corrupted or missing!
                 We can't tolerate that in a header.  Die. */
              fprintf(stderr,"Corrupt secondary header.  Exiting.\n");
			  cleanup();
             return false;
            }
            result=vorbis_synthesis_headerin(&vi,&vc,&op);
            if(result<0){
              fprintf(stderr,"Corrupt secondary header.  Exiting.\n");
			  cleanup();
              return false;
            }
            i++;
          }
        }
      }
      /* no harm in not checking before adding more */
      buffer=ogg_sync_buffer(&oy,4096);
      bytes=fread(buffer,1,4096,ogg_filein);
      if(bytes==0 && i<2){
        fprintf(stderr,"End of file before finding all Vorbis headers!\n");
		cleanup();
       return false;
      }
      ogg_sync_wrote(&oy,bytes);
    }

	return true;
}

bool COggHelper::start_decoding()
{
	{
      char **ptr=vc.user_comments;
      while(*ptr){
        fprintf(stderr,"%s\n",*ptr);
        ++ptr;
      }
      fprintf(stderr,"\nBitstream is %d channel, %ldHz\n",vi.channels,vi.rate);
      fprintf(stderr,"Encoded by: %s\n\n",vc.vendor);
    }
    
    convsize=4096/vi.channels;

    /* OK, got and parsed all three headers. Initialize the Vorbis
       packet->PCM decoder. */
    if(vorbis_synthesis_init(&vd,&vi)==0)
	{ /* central decode state */
      vorbis_block_init(&vd,&vb);          /* local state for most of the decode
                                              so multiple block decodes can
                                              proceed in parallel. We could init
                                              multiple vorbis_block structures
                                              for vd here */
	  m_bStopDecoding = false;
	  mDecThread.Start(this);
	  return true;
	}
	fprintf(stderr,"Error: Corrupt header during playback initialization.\n");

	cleanup();
	return false;
}

int COggHelper::decode( )
{
	int i = 0;
	int eos = 0;

	while(!eos)
	{
		int result=ogg_sync_pageout(&oy,&og);
		if(result==0)break; /* need more data */
		if(result<0){ /* missing or corrupt data at this page position */
		fprintf(stderr,"Corrupt or missing data in bitstream; "
				"continuing...\n");
		}else{
		ogg_stream_pagein(&os,&og); /* can safely ignore errors at
										this point */
		while(1){
			result=ogg_stream_packetout(&os,&op);
              
			if(result==0)break; /* need more data */
			if(result<0){ /* missing or corrupt data at this page position */
			/* no reason to complain; already complained above */
			}else{
			/* we have a packet.  Decode it */
			float **pcm;
			int samples;
                
			if(vorbis_synthesis(&vb,&op)==0) /* test for success! */
				vorbis_synthesis_blockin(&vd,&vb);
			/* 
                   
			**pcm is a multichannel float vector.  In stereo, for
			example, pcm[0] is left, and pcm[1] is right.  samples is
			the size of each channel.  Convert the float values
			(-1.<=range<=1.) to whatever PCM format and write it out */
                
			while((samples=vorbis_synthesis_pcmout(&vd,&pcm))>0)
			{
				int j;
				int clipflag=0;
				int bout=(samples<convsize?samples:convsize);
                  
				/* convert floats to 16 bit signed ints (host order) and
					interleave */
				for(i=0;i<vi.channels;i++){
				ogg_int16_t *ptr=convbuffer+i;
				float  *mono=pcm[i];
				for(j=0;j<bout;j++){
	#if 1
					int val=(int)floor(mono[j]*32767.f+.5f);
	#else /* optional dither */
					int val=mono[j]*32767.f+drand48()-0.5f;
	#endif
					/* might as well guard against clipping */
					if(val>32767){
					val=32767;
					clipflag=1;
					}
					if(val<-32768){
					val=-32768;
					clipflag=1;
					}
					*ptr=val;
					ptr+=vi.channels;
				}
				}
                  
				if(clipflag)
				fprintf(stderr,"Clipping in frame %ld\n",(long)(vd.sequence));
                  
				if(m_bStopDecoding) return EOS;
				for(int k=0;k<bout*vi.channels;k++)
				{
					if(m_bStopDecoding) return EOS;
					while(mDecBuffer->BBufferIsFull())
					{
						usleep(25);
						if(m_bStopDecoding) return EOS;/*fprintf(stderr,"Buffer full\n")*/;
					}

					mDecBuffer->write(convbuffer[k]);
				}

				vorbis_synthesis_read(&vd,bout); /* tell libvorbis how
													many samples we
													actually consumed */
			}            
			}
		}
		if(ogg_page_eos(&og))eos=1;
		}
	}

	if(eos == 1)
		return EOS;

	buffer=ogg_sync_buffer(&oy,4096);
	bytes=fread(buffer,1,4096,ogg_filein);
	ogg_sync_wrote(&oy,bytes);
	if(bytes==0)eos=1;

	return	COggHelper::EOK;
}

void COggHelper::decode_loop()
{
	int res;
	res = decode();
	while(res == COggHelper::EOK)
	{
		res = decode();
	}
}

void COggHelper::end_decoding()
{
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	m_bStopDecoding = true;
}

void COggHelper::cleanup()
{
	if(ogg_filein)
	{
		fclose(ogg_filein);
		ogg_filein = NULL;
	}

	ogg_stream_clear(&os);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi); 
	ogg_sync_clear(&oy);
}

ogg_int16_t COggHelper::get_sample()
{
	ogg_int16_t res = 0;
	while(!mDecBuffer->read(res))
	{
		if(m_bStopDecoding) return 0;
		fprintf(stderr,"Buffer under run\n"); //wait
	}

	return res;
}



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
	time_length = 0;
	current_time	= 0;
	current_section = 0;
	m_bStopDecoding = false;

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
	m_bStopDecoding = false;
	
	ov_clear(&vf);
	if(vi)
		vorbis_info_clear(vi);
}

static int _ov_header_fseek_wrap(FILE *f,ogg_int64_t off,int whence){
	if(f==NULL)return(-1);

#ifdef __MINGW32__
	return fseeko64(f,off,whence);
#elif defined (_WIN32)
	return _fseeki64(f,off,whence);
#else
	return fseek(f,(long)off,whence);
#endif
}

static ov_callbacks OV_CALLBACKS_NOCLOSE = {
	(size_t (*)(void *, size_t, size_t, void *))  fread,
	(int (*)(void *, ogg_int64_t, int))           _ov_header_fseek_wrap,
	(int (*)(void *))                             NULL,
	(long (*)(void *))                            ftell
};

bool COggVorbisFileHelper::init( std::string fin_str )
{
	if(mDecBuffer == NULL)
		mDecBuffer = new CCircularBuffer(_CIRCBUFSIZE_);

	cleanup();

	oggvorbis_filein = fopen(fin_str.c_str(),"rb");
	if(oggvorbis_filein == NULL)
	{
		fprintf(stderr,"Cannot open file '%s'.\n",fin_str.c_str());
		cleanup();
		return false;
	}

	if(ov_open_callbacks(oggvorbis_filein, &vf, NULL, 0, OV_CALLBACKS_NOCLOSE) < 0) 
	{
		fprintf(stderr,"Input does not appear to be an Ogg bitstream.\n");
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
		fprintf(stderr,"\nBitstream is %d channel, %ldHz\n",vi->channels,vi->rate);
		fprintf(stderr,"\nDecoded length: %ld samples\n",
			(long)nSamples);
		fprintf(stderr,"Encoded by: %s\n\n",ov_comment(&vf,-1)->vendor);


	}

	m_bStopDecoding = false;
	mDecThread.Start(this);

	return EOK;
}

int COggVorbisFileHelper::decode()
{
	long ret=ov_read(&vf,convbuffer,sizeof(convbuffer),0,2,1,&current_section);
	current_time = ov_time_tell(&vf);
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
			while(mDecBuffer->BBufferIsFull())
			{
				usleep(25);
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
		fprintf(stderr,"Buffer under run\n"); //wait
	}

	return res;
}

bool COggVorbisFileHelper::set_current_timepos( double pos )
{
	if(vf.datasource && vf.seekable)
	{
		ov_time_seek(&vf,pos);
		mDecBuffer->clear();
	}
	return true;
}


