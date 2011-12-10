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

int		g_curSoundPos = 0;
int     g_SoundSamples;     // Length of sound data in 16-bit samples
int		g_PlayingChannel;
int		g_SamplesPlayed;
int		g_ResampleQuality = -1;
bool	g_EnableResample = true;

#define	_BUILD	121
COggVorbisFileHelper* ogg_hlp;


bool LoadSound()
{
	g_ResampleQuality++;
	if(g_ResampleQuality >= 11)
	{
		g_ResampleQuality = 0;
		g_EnableResample = 0;
	}
	else
	{
		g_EnableResample = 1;
	}

	if(!ogg_hlp->init("test.ogg",g_EnableResample,g_ResampleQuality))
		return false;

	
	return true;
}

bool StartSoundSystem(int inputRate,int inputIsStereo)
{

	g_SamplesPlayed = 0;

	return 1;
}

void cleanup()
{

}

Button* g_PlayNoResampleButton;
Button* g_StopButton;
Button* g_PauseButton;
Button* g_StereoButton;
Button* g_InputButton;
Button* g_MoveButton;

bool    g_Playing = false;  // True if we're playing a sample



void ExampleInit()
{
	ogg_hlp = new COggVorbisFileHelper;
	
    g_PlayNoResampleButton = NewButton("Play");

    g_StopButton = NewButton("Stop");
	g_PauseButton = NewButton("Pause");

    g_InputButton = NewButton("Load File");
	g_MoveButton = NewButton("Move to 50%");
    g_StereoButton = NewButton("Change Output (mono/stereo/L/R)");



}
/*
 * Do cleanup work
 */
void ExampleTerm()
{
	ogg_hlp->stop();
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

	g_Playing = (ogg_hlp->get_status() == COggVorbisFileHelper::OH_PLAYING);
    if (!g_Playing)
    {
        bool play = false;

        if (pressed == g_PlayNoResampleButton)
        {
            play = true;
        }

        if (play)
        {
			if(ogg_hlp->play())	g_Playing = true;
        }
    }
    else if (pressed == g_StopButton)
    {
		ogg_hlp->stop();
        g_Playing = false;
    }
	else if (pressed == g_PauseButton)
	{
		ogg_hlp->pause();
		g_Playing = false;
	}
	else if (pressed == g_MoveButton)
	{
		ogg_hlp->set_current_timepos(180);
		g_Playing = true;
	}
    if (pressed == g_StereoButton)
    {		
		COggVorbisFileHelper::STEREO_MODE g_OutputMode;
        g_OutputMode = (COggVorbisFileHelper::STEREO_MODE)((ogg_hlp->get_outputStereoMode() + 1) % COggVorbisFileHelper::STEREO_MODE_COUNT);
		ogg_hlp->set_outputStereoMode(g_OutputMode);

    }

    if (pressed == g_InputButton)
    {
		int inputIsStereo = false;
		int inputRate = 0;

		g_PlayingChannel = 0;
		LoadSound();
		//StartSoundSystem(inputRate,inputIsStereo);
		
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
		g_StopButton->m_Enabled = true;
		g_PauseButton->m_Enabled = true;
		g_InputButton->m_Enabled = false;
		

	}
	else
	{
		g_PlayNoResampleButton->m_Enabled = true;
		g_StopButton->m_Enabled = false;
		g_PauseButton->m_Enabled = false;
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

	if (ogg_hlp->get_nChannels() == 2)
		s3eDebugPrintf(x, y, 0, "`x666666Input: Stereo");
	else
		s3eDebugPrintf(x, y, 0, "`x666666Input: Mono");
	y += textHeight;

	if (ogg_hlp->get_outputStereoMode() == COggVorbisFileHelper::STEREO_MODE_MONO)
	{
		if (!(ogg_hlp->get_nChannels() == 2))
			s3eDebugPrintf(x, y, 0, "`x666666Output: Mono");
		else
			s3eDebugPrintf(x, y, 0, "`x666666Output: Mono (left channel of stereo input)");
	}
	else
	{
		const char* modeLRString;
		switch (ogg_hlp->get_outputStereoMode())
		{
		case COggVorbisFileHelper::STEREO_MODE_LEFT:
			modeLRString = "left only";
			break;
		case COggVorbisFileHelper::STEREO_MODE_RIGHT:
			modeLRString = "right only";
			break;
		default:
			modeLRString = "left and right";
			break;
		}

		s3eDebugPrintf(x, y, 0, "`x666666Output: Stereo (%s)", modeLRString);
	}
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Input freq: %ld", ogg_hlp->get_rate());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Output freq: %d", ogg_hlp->get_outputrate());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666g_OutputIsStereo: %d", ogg_hlp->get_outputIsStereo());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Buffer: %.2f",ogg_hlp->get_decbuf()*100);
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Total Samples: %ld", (long)ogg_hlp->get_nsamples());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Total time: %.0f", ogg_hlp->get_time_length());
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Current time: %.0f (%0.f%%)", ogg_hlp->get_current_time(),ogg_hlp->get_current_time()/ogg_hlp->get_time_length()*100);
	y += textHeight;
	s3eDebugPrintf(x, y, 0, "`x666666Resample: %d - Quality: %d)", g_EnableResample,g_ResampleQuality);
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Status: %s", ogg_hlp->get_statusstr().c_str());
	y += textHeight;
	y += textHeight;
	y += textHeight;

	s3eDebugPrintf(x, y, 0, "`x666666Build: %0d", int(_BUILD));


}