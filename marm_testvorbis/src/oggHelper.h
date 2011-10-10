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
#include "s3eThread.h"
#include <vector>
#

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
	s3eThread* ThreadId_;
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

private:
	std::vector<ogg_int16_t> aInternalBuffer;
	unsigned int iBufferSize;
	unsigned int rIdx;
	unsigned int wIdx;
	bool bBufferIsEmpty;
	bool bBufferIsFull;
	int count; // index for test functions
};

class COggHelper
{
protected:
	ogg_int16_t convbuffer[_CONVSIZE_]; /* take 8k out of the data segment, not the stack */
	int convsize;

	bool	m_bStopDecoding;

	ogg_sync_state   oy; /* sync and verify incoming physical bitstream */
	ogg_stream_state os; /* take physical pages, weld into a logical
							stream of packets */
	ogg_page         og; /* one Ogg bitstream page. Vorbis packets are inside */
	ogg_packet       op; /* one raw packet of data for decode */

	vorbis_info      vi; /* struct that stores all the static vorbis bitstream
							settings */
	vorbis_comment   vc; /* struct that stores all the bitstream user comments */
	vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
	vorbis_block     vb; /* local working space for packet->PCM decode */


	char *buffer;
	int  bytes;

	FILE* ogg_filein;
	CCircularBuffer*	mDecBuffer;
	CThread				mDecThread;
	std::string			m_strLastError;
	
public:
	COggHelper();
	~COggHelper();

	enum
	{
		ERR = 0,
		EOK = 1,
		EOS	= 2,
		BFF = 3
	};
protected:
	
public:
	bool init_ogg(std::string fin_str);
	bool start_decoding();
	void end_decoding();
	void cleanup();
	int decode();
	void decode_loop();
	int get_nChannels(){return vi.channels;};
	long get_rate(){return vi.rate;};

	ogg_int16_t get_sample();


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

	FILE*				oggvorbis_filein;
	CCircularBuffer*	mDecBuffer;
	CThread				mDecThread;
	std::string			m_strLastError;

	bool	m_bStopDecoding;

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


	bool init(std::string fin_str);
	void cleanup();
	int decode();
	void decode_loop();
	void seek(double pos);
	int get_nChannels(){return nChannels;};
	long get_rate(){return nRate;};
	ogg_int64_t get_nsamples() const { return nSamples; };
	double get_time_length() {return time_length;};
	double get_current_time(){return current_time;};

	bool set_current_timepos(double pos);
	ogg_int16_t get_sample();
};
