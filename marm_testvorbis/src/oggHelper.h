#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vorbis/codec.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#ifdef _WIN32 /* We need the following two to set stdin/stdout to binary */
#include <io.h>
#include <fcntl.h>
#endif
#include <string>
#include <pthread.h>
#include "s3eFile.h"
#include <vector>


#define _CONVSIZE_ 4096
#define _CIRCBUFSIZE_	300000


class CThread
{
public:
	CThread();
	int Start(void * arg);
	int Suspend();
	int Cancel();

protected:
	int Run(void * arg);
	static void* EntryPoint(void*);
	void Setup();
	void Execute(void*);
	void * Arg() const {return Arg_;}
	void Arg(void* a){Arg_ = a;}
private:
	pthread_t ThreadId_;
	void * Arg_;
};

//original code from this class cames from: http://bytes.com/topic/c/answers/443602-how-create-circular-queue-c
class CCircularBuffer
{
public:
	enum ERRORS {
		BUFF_EMPTY = -1
	};
	CCircularBuffer (unsigned int size);
	bool read (ogg_int16_t& result);
	bool write (ogg_int16_t value);
	void clear();

	bool BufferIsEmpty() const { return bBufferIsEmpty; };
	bool BBufferIsFull() const { return bBufferIsFull; };

	unsigned int get_freespace();
	unsigned int get_bufferSize() const { return iBufferSize; }

private:
	std::vector<ogg_int16_t> aInternalBuffer;
	unsigned int iBufferSize;
	unsigned int rIdx;
	unsigned int wIdx;
	bool bBufferIsEmpty;
	bool bBufferIsFull;
	int count; // index for test functions
};

class COggVorbisFileHelper
{
protected:
	char convbuffer[_CONVSIZE_]; /* take 4k out of the data segment, not the stack */

	OggVorbis_File	vf;
	vorbis_info		*vi;

	ogg_int64_t		nSamples;
	long			nRate;
	double			time_length;
	double			current_time;
	int				nChannels;
	int				current_section;

	s3eFile*			oggvorbis_filein;
	CCircularBuffer*	mDecBuffer;
	CThread				mDecThread;
	std::string			m_strLastError;

	bool	m_bStopDecoding;
	int		nSoundChannel;
	int32	nOutputRate;
	int		bOutputIsStereo;
	float	dResampleFactor;
	int		nW, nL;             // Interpolation and decimation factors

public:
	COggVorbisFileHelper();
	~COggVorbisFileHelper();

	enum
	{
		ERR = 0,
		EOK = 1,
		EOS	= 2,
		BFF = 3
	};

	enum OHStatus
	{
		OH_NAN			= 0,
		OH_READY		= 1,
		OH_PLAYING		= 2,
		OH_STOPPED		= 3,
		OH_PAUSED		= 4,
		OH_ERROR		= 5,
		OH_BUFFERING	= 6
	} nStatus;

	enum STEREO_MODE
	{
		STEREO_MODE_MONO,
		STEREO_MODE_BOTH,
		STEREO_MODE_LEFT,
		STEREO_MODE_RIGHT,
		STEREO_MODE_COUNT
	};

	enum SAMPLE_RATE_CONVERTER
	{
		NO_RESAMPLE,
		ZERO_ORDER_HOLD,
		FIRST_ORDER_INTERPOLATION,
		QUADRATIC_INTERPOLATION,
	};

	int get_status() const { return nStatus; };

	bool init(std::string fin_str);
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
	void set_outputStereoMode(STEREO_MODE val);

	SAMPLE_RATE_CONVERTER get_conversionType() const {return conversionType;};
	void set_conversionType(SAMPLE_RATE_CONVERTER val){ conversionType = val;};

	double get_time_length() {return time_length;};
	double get_current_time(){return current_time;};
	bool set_current_timepos(double pos);
	
	
	ogg_int64_t get_nsamples() const { return nSamples; };
	ogg_int16_t get_sample();

	void decode_loop();
private:
	STEREO_MODE stereoOutputMode;
	SAMPLE_RATE_CONVERTER conversionType;
	// internal functions 
	int decode();
	

	// oggVorbis loading callbacks
	static size_t read_func(void *ptr, size_t size, size_t nmemb, void *datasource);
	static int seek_func(void *datasource, ogg_int64_t offset, int whence);
	static int close_func(void *datasource);
	static long tell_func(void *datasource);

public:
	// streaming callbacks
	static int32 EndSampleCallback(void* sys, void* user);
	static int GenerateAudioCallback(void* sys, void* user);
};
