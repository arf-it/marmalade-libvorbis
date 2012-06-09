#pragma once
#include <s3e.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef _USELIBTREMOR
#include <ivorbiscodec.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <ivorbisfile.h>
#else
#include <vorbis/codec.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#endif

#include <string>
#include "s3eFile.h"
#include <vector>
#include "..\libs\resample.h"
#include <s3eMemory.h>

#if defined(HAVE_PTHREAD)
#include <pthread.h>
#else
#define pthread_t int
#endif

#define _CONVSIZE_ 4096
#define _CIRCBUFSIZE_	262144 //must be power of 2


class CThread
{
public:
	CThread();
	int Start(void * arg);
	int Suspend();
	int Cancel();
	void * Arg() const {return Arg_;}
	void Arg(void* a){Arg_ = a;}
	enum status 
	{
		TRUNNING = 1,
		TIDLE	= 2
	} thread_status;

protected:
	int Run(void * arg);
	static void* EntryPoint(void*);
	void Setup();
	virtual void Execute(void*);
private:
	pthread_t ThreadId_;
	void * Arg_;
	s3eMemoryUsrMgr mgr;
};

//---------------------------------------------------------------------------
// template class Queue
//---------------------------------------------------------------------------
template <class T> class Queue {

	T *qbuf;   // buffer data
	int qsize; // 
	int head;  // index begin data
	int tail;  // index stop data

	inline void Free()
	{
		if (qbuf != 0)
		{
			delete []qbuf;
			qbuf= 0;
		}
		qsize= 1;
		head= tail= 0;
	}

public:
	Queue()
	{
		qsize= 32;
		qbuf= new T[qsize];
		head= tail= 0;
	}

	Queue(const int size): qsize(1), qbuf(0), head(0), tail(0)
	{
		if ((size <= 0) || (size & (size - 1)))
		{
			return;
		}

		qsize= size;
		qbuf= new T[qsize];
		head= tail= 0;
	}

	~Queue()
	{
		Free();
	}

	bool Enqueue(const T &p)
	{
		if (IsFull()) 
		{
			return false;
		}

		qbuf[tail]= p;
		tail= (tail + 1) & (qsize - 1);
		return true;
	}

	// Retrieve the item from the queue
	bool Dequeue(T &p)
	{
		if (IsEmpty())
		{
			return false;
		}

		p= qbuf[head];
		head= (head + 1) & (qsize - 1);
		return true;
	}

	// Get i-element with not delete
	bool Peek(const int i, T &p) const
	{
		int j= 0;
		int k= head;
		while (k != tail)
		{
			if (j == i) break;
			j++;

			k= (k + 1) & (qsize - 1);
		}
		if (k == tail) return false;
		p= qbuf[k];
		return true;
	}

	// Size must by: 1, 2, 4, 8, 16, 32, 64, ..
	bool Resize(const int size)
	{
		if ((size <= 0) || (size & (size - 1)))
		{
			return false;
		}

		Free();
		qsize= size;
		qbuf= new T[qsize];
		head= tail= 0;
		return true;
	}

	inline void Clear(void) { head= tail= 0; }

	inline int  GetCapacity(void) const { return (qsize - 1); }

	// Count elements
	inline int  GetBusy(void) const   { return ((head > tail) ? qsize : 0) + tail - head; }

	// true - if queue if empty
	inline bool IsEmpty(void) const { return (head == tail); }

	// true - if queue if full
	inline bool IsFull(void) const  { return ( ((tail + 1) & (qsize - 1)) == head ); }

};





#define min(a,b) (a)<(b)?(a):(b)

class COggVorbisFileHelper
{
protected:
	char convbuffer[_CONVSIZE_]; /* take 4k out of the data segment, not the stack */
	char remaing_convbuffer[_CONVSIZE_]; /* take 4k out of the data segment, not the stack */
	int rcb_len;

	short m_tmpbufR[_CONVSIZE_];
	short m_tmpbufL[_CONVSIZE_];

	unsigned int m_outbufsizeL;
	unsigned int m_outbufsizeR;

	short* m_outL;
	short* m_outR;

	unsigned int cb_pos;
	unsigned int cb_size;

	OggVorbis_File	vf;
	vorbis_info		*vi;

	
	long			nRate;
	int				nChannels;
	int				current_section;

	s3eFile*			oggvorbis_filein;
	Queue<ogg_int16_t> *mDecBuffer; 
	CThread				mDecThread;
	std::string			m_strLastError;
	std::string			m_strStatus;

	bool	bStopDecoding;
	int		nSoundChannel;
	int32	nOutputRate;
	int		bOutputIsStereo;
	float	dResampleFactor;
	int		nW, nL;             // Interpolation and decimation factors

	double	time_length;
	double	current_time;

	ogg_int64_t		total_samples;
	ogg_int64_t		current_sample;

	bool	bEnableResampling;
	bool	bEnableFilter;
	int		nResampleQuality;

	SpeexResamplerState* res_contR;
	SpeexResamplerState* res_contL;

	int nFilterCoefficients;
	int16* iFilterBufferL;
	int16* iFilterBufferR;
	double* dFilterCoefficients;

	float	m_dBufferingMaxCapacity;

public:
	COggVorbisFileHelper();
	virtual ~COggVorbisFileHelper();

	enum
	{
		ERR = 0,
		EOK = 1,
		EOS	= 2,
		BFF = 3,
		EBUFFCOMP = 4
	};

	enum OHStatus
	{
		OH_NAN			= 0,
		OH_READY		= 1,
		OH_PLAYING		= 2,
		OH_STOPPED		= 3,
		OH_PAUSED		= 4,
		OH_ERROR		= 5,
		OH_BUFFERING	= 6,
		OH_BUFFERUNDERRUN = 7,
		OH_END			= 8
	} nStatus;

	enum STEREO_MODE
	{
		STEREO_MODE_MONO,
		STEREO_MODE_BOTH,
		STEREO_MODE_LEFT,
		STEREO_MODE_RIGHT,
		STEREO_MODE_COUNT
	};

	void set_status(OHStatus status);
	OHStatus get_status() { return nStatus; };
	std::string get_statusstr();

	int get_decbufspace() { if(mDecBuffer)return mDecBuffer->GetBusy();return 0;};
	double get_decbuf(){return (double)get_decbufspace()/_CIRCBUFSIZE_;};
	bool init(std::string fin_str,bool bResample = true,int nResQuality = 0, char* pData = NULL, uint32 iSize = 0);
	bool play();
	bool stop();
	bool pause();
	bool resume();

	void cleanup();
	
	int get_nChannels(){return nChannels;};
	long get_rate(){return nRate;};
	int32 get_outputrate(){return nOutputRate;};
	float get_dResampleFactor() const { return dResampleFactor; };

	int get_outputIsStereo() const {return bOutputIsStereo;};
	void set_outputIsStereo(int val){ bOutputIsStereo = val;};
	
	STEREO_MODE get_outputStereoMode() const {return stereoOutputMode;};
	bool set_outputStereoMode(STEREO_MODE val);

	void set_channelvolume(int val);

	double get_time_length() {return time_length;};
	double get_current_time(){return current_time;};
	bool set_current_timepos(double pos);

	
	ogg_int64_t get_nsamples() const { return total_samples; };
	ogg_int16_t get_sample();

	virtual void decode_loop();

	int Wait_counter() const { return wait_counter; }
	void Wait_counter(int val) { wait_counter = val; }

	float get_bufferingMaxCapacity() const { return m_dBufferingMaxCapacity; }
	void set_bufferingMaxCapacity(float val) { m_dBufferingMaxCapacity = val; }
	

protected:
	STEREO_MODE stereoOutputMode;
	int wait_counter;
	// internal functions 
	int decode();

	

	// oggVorbis loading callbacks
	static size_t read_func(void *ptr, size_t size, size_t nmemb, void *datasource);
	static int seek_func(void *datasource, ogg_int64_t offset, int whence);
	static int close_func(void *datasource);
	static long tell_func(void *datasource);


	long ov_read_func(OggVorbis_File *vf,char *buffer,int length,int bigendianp,int word,int sgned,int *bitstream);
	double ov_time_total_func(OggVorbis_File *vf,int i);
	double ov_time_tell_func(OggVorbis_File *vf);
	int ov_time_seek_func(OggVorbis_File *vf,double pos);

	virtual bool IsLastSample();

public:
	// streaming callbacks
	static int32 EndSampleCallback(void* sys, void* user);
	static int GenerateAudioCallback(void* sys, void* user);
	
};
