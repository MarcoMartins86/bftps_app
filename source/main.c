#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#ifdef _3DS
#include <3ds.h>
#endif

#include "bftps.h"

#ifdef _3DS
bool IsN3DS() {
    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);
    return isNew3DS;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _3DS
    bool isNew3DS = IsN3DS();
    if(true == isNew3DS) {
        osSetSpeedupEnable(true);
    }
    gfxInitDefault();
    gfxSet3D(false);
    sdmcWriteSafe(false);
#ifdef _DEBUG
    consoleInit(GFX_TOP, NULL);
#endif
#endif
    // Main loop
#ifdef _3DS
    bftps_start();
    while (aptMainLoop()) {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        // Your code goes here
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break; // break in order to return to hbmenu     
        usleep(50000);
        const bftps_file_transfer_t* info = bftps_file_transfer_retrieve();
        if (info) {
            const bftps_file_transfer_t* aux = info;
            while(aux) {
                if(aux->mode == FILE_SENDING)
                    printf("Sending [%s] %f%%\n", aux->name, 
                            ((double)aux->filePosition / (double)aux->fileSize) * (double)100);
                else
                    printf("Receiving [%s] %fMB\n", aux->name, 
                            ((double)aux->filePosition / ((double)1024 * (double)1024)) );
                aux = aux->next;
            }
            bftps_file_transfer_cleanup(info);
        }
    }
    printf("EXITING\n");
    bftps_stop();
#elif __linux__
    // on linux we will just sleep indefinitely on this thread
    bftps_start();
    printf("%s\n",bftps_name());
    while (1) {
        usleep(150000);
        const bftps_file_transfer_t* info = bftps_file_transfer_retrieve();
        if (info) {
            const bftps_file_transfer_t* aux = info;
            while(aux) {
                if(aux->mode == FILE_SENDING)
                    printf("Sending [%s] %f%%\n", aux->name, 
                            ((double)aux->filePosition / (double)aux->fileSize) * (double)100);
                else
                    printf("Receiving [%s] %fMB\n", aux->name, 
                            ((double)aux->filePosition / ((double)1024 * (double)1024)) );
                aux = aux->next;
            }
            bftps_file_transfer_cleanup(info);
        }
    }    
    bftps_stop();
#endif        
#ifdef _3DS
    if(true == isNew3DS) {
        osSetSpeedupEnable(false);
    }
    gfxExit();
#endif
    return 0;
}
