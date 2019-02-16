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
    }
    printf("EXITING\n");
    bftps_stop();
#elif __linux__
    // on linux we will just sleep indefinitely on this thread
    bftps_start();
    sleep(INT_MAX);
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
