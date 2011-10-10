/*
 * This file is part of the Marmalade SDK Code Samples.
 *
 * Copyright (C) 2001-2011 Ideaworks3D Ltd.
 * All Rights Reserved.
 *
 * This source code is intended only as a supplement to Ideaworks Labs
 * Development Tools and/or on-line documentation.
 *
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
 * KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
 * PARTICULAR PURPOSE.
 */
//#include "s3e.h"
#include "ExamplesMain.h"
#include "oggHelper.h"
#include "sound_helper.h"



int16*  g_SoundData;        // Buffer that holds the sound data
int16*  g_SoundData2;        // Buffer that holds the sound data
int     g_SoundLen;         // Length of sound data
int		g_curSoundPos = 0;
int     g_SoundSamples;     // Length of sound data in 16-bit samples
int		g_InputIsStereo;
int		g_OutputIsStereo;
int		g_PlayingChannel;
int		g_SamplesPlayed;
int		g_OutputRate;
int		g_InputRate;
float	g_ResampleFactor = 1.0f;
int g_W, g_L;             // Interpolation and decimation factors

COggVorbisFileHelper* ogg_hlp;

int32 EndSampleCallback(void* sys, void* user)
{
	s3eSoundEndSampleInfo* info = (s3eSoundEndSampleInfo*)sys;
	g_SamplesPlayed = 0;
	return info->m_RepsRemaining;
}

int32 AudioCallback(void* sys, void* user)
{
	s3eTimerGetMs();
	s3eSoundGenAudioInfo* info = (s3eSoundGenAudioInfo*)sys;
	info->m_EndSample = 0;
	int16* target = (int16*)info->m_Target;

	// The user value is g_SamplesPlayed. This tracks the number of 16/32 bit
	// samples played, i.e. playback position in output space. To find position
	// in input track, we scale by sample size and resample factor.
	int outputStartPos = *((int*)user);
	int inputSampleSize = 1;
	int outputSampleSize = 1;

	if (info->m_Stereo)
		outputSampleSize = 2;

	if(g_InputIsStereo)
		inputSampleSize = 2;

	int samplesPlayed = 0;

	//ogg_hlp.FillBuffer();
	// For stereo output, info->m_NumSamples is number of l/r pairs (each sample is 32bit)
	// info->m_OrigNumSamples always measures the total number of 16 bit samples,
	// regardless of whether input was mono or stereo.

	memset(info->m_Target, 0, info->m_NumSamples * outputSampleSize * sizeof(int16));

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
		//if ((uint)inputSamplesUsed >= info->m_OrigNumSamples)
		//{
		//	info->m_EndSample = 1;
		//	outputStartPos = 0;
		//	break;
		//}

		// For each sample (pair) required, we either do:
		//  * get mono sample if input is mono (output can be either)
		//  * get left sample if input is stereo (output can be either)
		//  * get right sample if input and output are both stereo

		int outPosLeft = (outputStartPos + i) * inputSampleSize;

		if (conversionType != NO_RESAMPLE)
		{
			outPosLeft = (int)(outPosLeft * g_ResampleFactor);
			for(int k=0;k<g_ResampleFactor-1;k++)
			{
				yLeft = ogg_hlp->get_sample();
				if (g_InputIsStereo)
				{
					yRight = ogg_hlp->get_sample();
				}
			}
		}
			

		switch (conversionType)
		{
		case NO_RESAMPLE:
			{
				// copy left (and right) 16bit sample directly from input buffer
				yLeft = ogg_hlp->get_sample();

				if (g_InputIsStereo)
				{
					yRight = ogg_hlp->get_sample();
					if (g_OutputMode == STEREO_MODE_MONO)
						yRight = 0;
				}


				break;
			}
		case ZERO_ORDER_HOLD:
			{
				//yLeft = info->m_OrigStart[outPosLeft];
				yLeft = ogg_hlp->get_sample();
				if (g_InputIsStereo)
				{
					yRight = ogg_hlp->get_sample();
					if (g_OutputMode == STEREO_MODE_MONO)
						yRight = 0;
				}
				break;
			}
		case FIRST_ORDER_INTERPOLATION:
			{
				// Note, these can overflow if posMono directs to the first/last sample.
				// Ideally should do some bounds checking and re-use same sample if needed.
				break; //not supported
				int16 sample0, sample1;
				sample0 = info->m_OrigStart[outPosLeft];
				if (!g_InputIsStereo)
				{
					sample1 = info->m_OrigStart[outPosLeft + 1];
				}
				else
				{
					sample1 = info->m_OrigStart[outPosLeft + 2];

					if (g_OutputMode != STEREO_MODE_MONO)
					{
						int sampleR0 = info->m_OrigStart[outPosLeft + 1];
						int sampleR1 = info->m_OrigStart[outPosLeft + 3];
						yRight = (sampleR0 + sampleR1) / 2;
					}
				}

				yLeft = (sample0 + sample1) / 2;

				break;
			}
		case QUADRATIC_INTERPOLATION:
			{
				// place a parabolic curve between three points to approximate
				// interpolation value.
				break; //not supported
				float pf = 0.5f;
				int sample0, sample1, sample2;

				sample0 = info->m_OrigStart[outPosLeft];

				if (!g_InputIsStereo)
				{
					sample1 = info->m_OrigStart[outPosLeft + 1];
					sample2 = info->m_OrigStart[outPosLeft + 2];
				}
				else
				{
					sample1 = info->m_OrigStart[outPosLeft + 2];
					sample2 = info->m_OrigStart[outPosLeft + 4];
					if (g_OutputMode != STEREO_MODE_MONO)
					{
						int sampleR0 = info->m_OrigStart[outPosLeft + 1];
						int sampleR1 = info->m_OrigStart[outPosLeft + 3];
						int sampleR2 = info->m_OrigStart[outPosLeft + 5];
						yRight = sampleR0 + (int16)((pf/2.0f) * (pf * (sampleR0 - (2.0f * sampleR1) + sampleR2))
							- 3.0f * sampleR0 + 4.0f * sampleR1 - sampleR2);
					}
				}

				yLeft = sample0 + (int16)((pf/2.0f) * (pf * (sample0 - (2.0f * sample1) + sample2))
					- 3.0f * sample0 + 4.0f * sample1 - sample2);

				break;
			}
		}

	
		int16 orig = 0;
		int16 origR = 0;
		if (info->m_Mix)
		{
			orig = *target;
			origR = *(target+1);
		}


		switch (g_OutputMode)
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
			*target++ = ClipToInt16(yLeft + orig);
			break;
		}

		samplesPlayed++;
	}

	// Update global output pointer (track samples played in app)
	*((int*)user) += samplesPlayed;
	g_OutputIsStereo = info->m_Stereo;
	// Inform s3e sound how many samples we played
	return samplesPlayed;
}

bool LoadSound()
{
	s3eFile *fileHandle = s3eFileOpen("ogg.raw", "rb");
	if(fileHandle == NULL) return false;



	g_SoundLen = s3eFileGetSize(fileHandle);
	g_SoundSamples = g_SoundLen/2; //or 4 if its 2 bytes per channel and 2 channels per sample
	g_SoundData = (int16*)malloc(g_SoundLen);
	g_SoundData2 = (int16*)malloc(g_SoundLen);

	memset(g_SoundData, 0, g_SoundLen);
	memset(g_SoundData2, 0, g_SoundLen);

	if(!ogg_hlp->init("test.ogg"))
		return 0;

	//ogg_hlp->start_decoding();

	//s3eFileRead(g_SoundData2, g_SoundLen, 1, fileHandle);
	s3eFileClose(fileHandle);

	//memset(g_SoundData, 0, g_SoundLen);

	//ogg_hlp.decode(); //buffering


	return true;
}

bool StartSoundSystem(int inputRate,int inputIsStereo)
{
	g_OutputIsStereo	= 0;
	g_SamplesPlayed		= 0;
	g_PlayingChannel = s3eSoundGetFreeChannel();
	s3eSoundChannelRegister(g_PlayingChannel, S3E_CHANNEL_GEN_AUDIO, AudioCallback, &g_SamplesPlayed);
	s3eSoundChannelRegister(g_PlayingChannel, S3E_CHANNEL_END_SAMPLE, EndSampleCallback, NULL);

	g_OutputMode = STEREO_MODE_MONO;

	g_SamplesPlayed = 0;
	g_OutputRate = s3eSoundGetInt(S3E_SOUND_OUTPUT_FREQ);
	g_InputRate  = 44100;
	g_InputIsStereo = 1;

	int gcd = GCD(g_InputRate, g_OutputRate);
	g_W = g_InputRate  / gcd;
	g_L = g_OutputRate / gcd;
	s3eDebugTracePrintf("Resampling by rational factor %d / %d", g_W, g_L);

	// As a float scale factor. Multiply output pos by this to find input.
	g_ResampleFactor = g_W / (float) g_L;

	return 1;
}

void cleanup()
{
	if (g_SoundData)
		delete[] g_SoundData;
}

Button* g_PlayNoResampleButton;
Button* g_PlayZeroOrderButton;
Button* g_PlayLinearButton;
Button* g_PlayQuadraticButton;
Button* g_StopButton;
Button* g_StereoButton;
Button* g_OutputFilterButton;
Button* g_InputButton;

bool    g_Playing = false;  // True if we're playing a sample


void ExampleInit()
{
	ogg_hlp = new COggVorbisFileHelper;
	
	g_SoundData = 0;
    g_PlayNoResampleButton = NewButton("Play (no resampling)");
    g_PlayZeroOrderButton = NewButton("Play (zero order hold)");
    g_PlayLinearButton = NewButton("Play (linear interpolation)");
    g_PlayQuadraticButton = NewButton("Play (quadratic interpolation)");
    g_StopButton = NewButton("Stop");

    g_InputButton = NewButton("Load File");
    g_StereoButton = NewButton("Change Output (mono/stereo/L/R)");

}
/*
 * Do cleanup work
 */
void ExampleTerm()
{
    free(g_SoundData);
	g_SoundData = 0;

	//ogg_hlp->end_decoding();
	ogg_hlp->cleanup();
}

/*
 * The following function checks which buttons where pressed and changes
 * between mono and sterio sound playback - by registering/unregistering
 * AudioCallback() as the S3E_CHANNEL_GEN_AUDIO_STEREO function - or
 * cycles through left/right/both stereo channels.
 */
bool ExampleUpdate()
{
    Button* pressed = GetSelectedButton();

    if (!g_Playing)
    {
        bool play = false;

        if (pressed == g_PlayNoResampleButton)
        {
            conversionType = NO_RESAMPLE;
            play = true;
        }
        else if (pressed == g_PlayZeroOrderButton)
        {
            conversionType = ZERO_ORDER_HOLD;
            play = true;
        }
        else if (pressed == g_PlayLinearButton)
        {
            conversionType = FIRST_ORDER_INTERPOLATION;
            play = true;
        }
        else if (pressed == g_PlayQuadraticButton)
        {
            conversionType = QUADRATIC_INTERPOLATION;
            play = true;
        }
        if (play)
        {
            g_SamplesPlayed = 0;
			//while(ogg_hlp.FillBuffer() != COggHelper::BFF)
			//	fprintf(stderr,"Fill buffer\n");
            s3eSoundChannelPlay(g_PlayingChannel, g_SoundData, (uint32) ogg_hlp->get_nsamples(), 5, 0);
            g_Playing = true;
        }
    }
    else if (pressed == g_StopButton)
    {
        s3eSoundChannelStop(g_PlayingChannel);
		ogg_hlp->set_current_timepos(0);
        g_Playing = false;
    }
    if (pressed == g_StereoButton)
    {
        g_OutputMode = (STEREO_MODE)((g_OutputMode + 1) % STEREO_MODE_COUNT);

        if (g_OutputMode != STEREO_MODE_MONO)
        {
            s3eSoundChannelRegister(g_PlayingChannel, S3E_CHANNEL_GEN_AUDIO_STEREO, AudioCallback, &g_SamplesPlayed);
        }
        else
        {
            s3eSoundChannelUnRegister(g_PlayingChannel, S3E_CHANNEL_GEN_AUDIO_STEREO);
        }

    }

    if (pressed == g_InputButton)
    {
		int inputIsStereo = false;
		int inputRate = 0;

		g_SoundData = NULL;
		g_PlayingChannel = 0;
		LoadSound();
		StartSoundSystem(inputRate,inputIsStereo);
		
		switch(ogg_hlp->get_nChannels())
		{
		case 1:
			inputIsStereo = false;
			break;
		case 2:
			inputIsStereo = true;
			break;
		default:
			s3eDebugTracePrintf("Number of channels %d not supported.\n", ogg_hlp->get_nChannels());
		}

		if(ogg_hlp->get_rate() > 0)
			inputRate = ogg_hlp->get_rate();
		else
		{
			s3eDebugTracePrintf("Input rate not valid %ld not supported.\n", ogg_hlp->get_rate());
		}

	}
    return true;
}

void ExampleRender()
{
	const int textHeight = s3eDebugGetInt(S3E_DEBUG_FONT_SIZE_HEIGHT);

	if (g_Playing)
	{
		g_PlayNoResampleButton->m_Enabled = false;
		g_PlayZeroOrderButton->m_Enabled = false;
		g_PlayLinearButton->m_Enabled = false;
		g_PlayQuadraticButton->m_Enabled = false;
		g_StopButton->m_Enabled = true;
		g_InputButton->m_Enabled = false;

		//ogg_hlp.decode();
		//while(ogg_hlp.FillBuffer() != COggHelper::BFF)
		//	fprintf(stderr,"Fill buffer %s\n",ogg_hlp.get_info());
	}
	else
	{
		g_PlayNoResampleButton->m_Enabled = true;
		g_PlayZeroOrderButton->m_Enabled = true;
		g_PlayLinearButton->m_Enabled = true;
		g_PlayQuadraticButton->m_Enabled = true;
		g_StopButton->m_Enabled = false;
		g_InputButton->m_Enabled = true;
	}

	int y = GetYBelowButtons() + 3 * textHeight;
	const int x = 10;

	if (s3eSoundGetInt(S3E_SOUND_STEREO_ENABLED))
	{
		s3eDebugPrintf(x, y, 0, "Stereo sound output supported and enabled");
	}
	else
	{
		s3eDebugPrintf(x, y, 0, "Stereo sound output disabled or unsupported");
	}
	y += textHeight;

	if (g_InputIsStereo)
		s3eDebugPrintf(x, y, 0, "`x666666Input: Stereo");
	else
		s3eDebugPrintf(x, y, 0, "`x666666Input: Mono");
	y += textHeight;

	if (g_OutputMode == STEREO_MODE_MONO)
	{
		if (!g_InputIsStereo)
			s3eDebugPrintf(x, y, 0, "`x666666Output: Mono");
		else
			s3eDebugPrintf(x, y, 0, "`x666666Output: Mono (left channel of stereo input)");
	}
	else
	{
		const char* modeLRString;
		switch (g_OutputMode)
		{
		case STEREO_MODE_LEFT:
			modeLRString = "left only";
			break;
		case STEREO_MODE_RIGHT:
			modeLRString = "right only";
			break;
		default:
			modeLRString = "left and right";
			break;
		}

		s3eDebugPrintf(x, y, 0, "`x666666Output: Stereo (%s)", modeLRString);
	}
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Input freq: %d", g_InputRate);
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Output freq: %d", g_OutputRate);
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666g_OutputIsStereo: %d", (int)g_OutputIsStereo);
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Plays Remaining: %d", 666);
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Total Samples: %ld", (long)ogg_hlp->get_nsamples());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Total time: %.0f", ogg_hlp->get_time_length());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Current time: %.0f (%00.0f%%)", ogg_hlp->get_current_time(),ogg_hlp->get_current_time()/ogg_hlp->get_time_length()*100);
	y += textHeight;
	//if(g_Playing)
	//	s3eDebugPrintf(x, y, 0, "`x666666Buffer: %.2f", ((double)(ogg_hlp.mDecBuffer->get_freespace())) / 8000);

}