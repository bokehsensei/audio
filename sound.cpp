#include <stdio.h>
#include <stdlib.h>
#include <portaudio.h>
#include <signal.h>
#include <boost/thread.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>

#include "circularfifo.h"

/* #define SAMPLE_RATE  (17932) // Test failure to open with this value. */
#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (512)
#define NUM_SECONDS     (2)
#define NUM_CHANNELS    (2)

#define NUM_CHUNKS          ( (NUM_SECONDS*SAMPLE_RATE/FRAMES_PER_BUFFER) + 1)
#define CHUNK_SIZE          FRAMES_PER_BUFFER*NUM_CHANNELS*sizeof(SAMPLE)
#define AUDIO_BUFFER_SIZE   NUM_CHUNKS*CHUNK_SIZE

/* #define DITHER_FLAG     (paDitherOff) */
#define DITHER_FLAG     (0) /**/
/** Set to 1 if you want to capture the recording to a file. */
#define WRITE_TO_FILE   (0)

/* Select sample format. */
#if 1
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

//just to create a timeout and switch
boost::mutex g_full_or_empty_mx;
boost::condition_variable_any g_full_or_empty_cv;
bool g_proceed;


typedef SAMPLE* sample_buffer_t;
typedef CircularFifo<sample_buffer_t, NUM_CHUNKS> ring_t;
// the ring buffer shared between the audio thread (represented by recordCallback)
// and the consumer thread
ring_t g_audio_samples_ring;
// The lockfree guarantee of CircularFifo means the ring's push function can only be 
// read by one given thread while the other thread can only call the pop function.
// If we want to recycle the buffer store in each element of the array of audio samples,
// we need to put them back on the recycle ring after they have been poped.
ring_t g_recycled_buffers_ring;
SAMPLE *g_current_write_buf;

void SegFault(int signal)
{
    printf("cur buf: %p\n", g_current_write_buf);
    exit(1);
}

void StopListening(int signal)
{
    g_proceed = false;
    g_full_or_empty_cv.notify_one();
}
/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    SAMPLE *rptr = (SAMPLE*)inputBuffer;
    long i;
    int finished;
    bool full = g_audio_samples_ring.isFull(), was_empty = g_audio_samples_ring.isEmpty();

    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    if(full)
        return paContinue;

    SAMPLE* destination_chunk;
    if(g_recycled_buffers_ring.pop(destination_chunk))
    {
        g_current_write_buf = destination_chunk;
        memcpy(destination_chunk, rptr, CHUNK_SIZE);
    }
    else
        syslog(LOG_ALERT, "Ran out of destination chunks!");

    g_audio_samples_ring.push(destination_chunk);
    if(was_empty)
        g_full_or_empty_cv.notify_one();

    return paContinue;
}

/*******************************************************************/
int main(void);
int main(void)
{
    PaStreamParameters  inputParameters;
    PaStream*           stream;
    PaError             err = paNoError;

    SAMPLE s;
    bool empty;
    const char *path = "/tmp/microphone";
    char buf[128];
    int len = 0;
    //long t = 0;
    ssize_t write_return_value;
    g_proceed = true;

    setlinebuf(stdout);
    signal(SIGINT, StopListening);
    signal(SIGSEGV, SegFault);
    signal(SIGPIPE, SIG_IGN);

    while(g_proceed)
    {

        g_proceed = false; //to prevent looping if there is an error before we start the loop that writes to pipe.
        syslog(LOG_ALERT, "About to open %s\n", path);
        int error = mkfifo(path, 0666);
        if( error && errno != EEXIST )
        {
            printf("Named pipe %s already exists.\n", path);
        }

        printf("Waiting for reader to open %s\n", path);
        int fifo = open(path, O_WRONLY);
        if( fifo == -1 )
        {
            printf("Error while trying to open %s \n", path);
            return 1;
        }
        printf("we have a reader of %s !\n", path);

        // allocate memory for the audio samples.
        SAMPLE* current_chunk;

        for( long chunk_index = 0; chunk_index < NUM_CHUNKS; chunk_index++)
        {
            current_chunk = (SAMPLE*)malloc(CHUNK_SIZE);
            if(current_chunk == NULL) goto done;
            g_recycled_buffers_ring.push(current_chunk);
        }

        err = Pa_Initialize();
        if( err != paNoError ) goto done;

        inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
        if (inputParameters.device == paNoDevice) {
            fprintf(stderr,"Error: No default input device.\n");
            goto done;
        }
        inputParameters.channelCount = 2;                    /* stereo input */
        inputParameters.sampleFormat = PA_SAMPLE_TYPE;
        inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
        inputParameters.hostApiSpecificStreamInfo = NULL;

        /* Record some audio. -------------------------------------------- */
        err = Pa_OpenStream(
                &stream,
                &inputParameters,
                NULL,                  /* &outputParameters, */
                SAMPLE_RATE,
                FRAMES_PER_BUFFER,
                paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                recordCallback,
                &g_audio_samples_ring );
        if( err != paNoError ) goto done;

        err = Pa_StartStream( stream );
        if( err != paNoError ) goto done;
        printf("\n=== Now recording!! Please speak into the microphone. ===\n");

        printf("Hit Ctrl-c to kill this process.\n");

        g_proceed = true;
        while(g_proceed)
        {
            SAMPLE *fresh_samples;

            if(!g_audio_samples_ring.pop(fresh_samples))
            {// ring is empty
                g_full_or_empty_cv.wait(g_full_or_empty_mx);
                //usleep(100);
            }
            else
            {
                write_return_value = write(fifo, fresh_samples, CHUNK_SIZE);
                if(!g_recycled_buffers_ring.push(fresh_samples))
                    syslog(LOG_ALERT, "Recycle ring is full");
                if(write_return_value == -1)
                {
                    break;
                }
            }
        }   

        err = Pa_CloseStream( stream );
        if( err != paNoError ) goto done;

done:
        Pa_Terminate();
        if( err != paNoError )
        {
            fprintf( stderr, "An error occured while using the portaudio stream\n" );
            fprintf( stderr, "Error number: %d\n", err );
            fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
            err = 1;          /* Always return 0 or 1, but no other return codes. */
        }
        close(fifo);
        printf("closed %s\n", path);
        SAMPLE *buf, *fill = 0;
        while(g_audio_samples_ring.pop(buf)) free(buf);
        while(g_recycled_buffers_ring.pop(buf)) free(buf);
    }
    g_full_or_empty_mx.unlock();
    return err;
}

