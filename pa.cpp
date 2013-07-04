#include <portaudio.h>
#include <stdio.h>
#include <signal.h>
#include <boost/thread.hpp>

#define SAMPLE_RATE (44100)
#define NUM_SECONDS 5
PaStream *stream;
PaError err;

//just to create a fucking timeout and switch
boost::mutex g_mx;
boost::condition_variable_any g_kill_switch;
boost::posix_time::seconds period(60);

/* This routine will be called by the PortAudio engine when audio is needed.
 It may called at interrupt level on some machines so don't do anything
 that could mess up the system like calling malloc() or free().
*/ 
static int patestCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    unsigned char *in, *out;
    in = (unsigned char*) inputBuffer;
    out = (unsigned char*) outputBuffer;
    unsigned long i;
    (void)userData; 
    
    for( i=0; i<framesPerBuffer; i++ )
    {
        *out++ = *in++; //left channel?
	*out++ = *in++; //right channel?
    }
    return 0;
}

void StopListening(int signal)
{
    g_kill_switch.notify_one();
}

int main()
{
    PaError err = Pa_Initialize();
    if( err != paNoError )
    {
	printf(  "PortAudio error: %s\n", Pa_GetErrorText( err ) );
	return err;
    }

/* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream( &stream,
                                2,          /* no input channels */
                                2,          /* stereo output */
                                paInt16,  /* 32 bit floating point output */
                                SAMPLE_RATE,
                                paFramesPerBufferUnspecified,        /* frames per buffer, i.e. the number
                                                   of sample frames that PortAudio will
                                                   request from the callback. Many apps
                                                   may want to use
                                                   paFramesPerBufferUnspecified, which
                                                   tells PortAudio to pick the best,
                                                   possibly changing, buffer size.*/
                                patestCallback, /* this is your callback function */
                                NULL ); /*This is a pointer that will be passed to
                                                   your callback*/

    if( err != paNoError ) goto error;
    printf("successfully opened a stream!\n");

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto error;

    printf("Hit Ctrl-c to kill this process.");
    fflush(stdout);
    g_kill_switch.timed_wait(g_mx, period);
    g_mx.unlock();

    err = Pa_StopStream( stream ); if( err != paNoError ) goto error;
    err = Pa_CloseStream( stream ); if( err != paNoError ) goto error;

error:
    err = Pa_Terminate();
    if( err != paNoError )
	printf(  "PortAudio error: %s\n", Pa_GetErrorText( err ) );

    return 0;
}
