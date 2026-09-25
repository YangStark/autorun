/* Full-package startup verification with a manifest-bound cache. */
#include "wine/nx_package_assets.h"
static u64 wa2_launch_started, wa2_package_check_ms;
static unsigned char wa2_hash_buffer[1024*1024];
static int wa2_package_mount(void)
{
    const char *marker=WINE_NX_SD_ROOT "/package-ready.txt";
    const char *temporary=WINE_NX_SD_ROOT "/package-ready.txt.part";
    const char *force_path=WINE_NX_SD_ROOT "/verify-package.flag";
    struct stat st;u64 program=0, done=0,total=0,last_ui=0;
    char cached_id[65]={0}, path[512];int cached=0, force=!access(force_path,F_OK), ok=1;
    wa2_launch_started=armGetSystemTick();
    consoleInit(NULL);printf("White Album 2 0.3.1\nChecking package...\n");consoleUpdate(NULL);
    appletSetAutoSleepDisabled(true);
    FILE *log=fopen(WINE_NX_SD_ROOT "/package-mount.log","w");
    Result rc=svcGetInfo(&program,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
    ok=R_SUCCEEDED(rc)&&(program==0x0500A17E00070000ULL||program==0x0500A17E00060000ULL);
    FILE *f=fopen(marker,"rb");if(f){size_t n=fread(cached_id,1,65,f);cached=n==64&&!memcmp(cached_id,WA2_PACKAGE_ID,64);fclose(f);}
    cached &= !force;
    rc=romfsMountFromCurrentProcess("wapkg");ok &= R_SUCCEEDED(rc);
    if(log)fprintf(log,"version=0.3.1\nmanifest=%s\nmode=%s\nprogram=%016llx\nmount_result=%08x\n",WA2_PACKAGE_ID,cached?"cached":"full",(unsigned long long)program,rc);
    for(unsigned i=0;i<WA2_PACKAGE_COUNT;i++)total+=wa2_package_assets[i].size;
    for(unsigned i=0;ok&&i<WA2_PACKAGE_COUNT;i++){
        const struct wa2_package_asset *asset=&wa2_package_assets[i];
        snprintf(path,sizeof(path),"%s/%s",WINE_NX_SD_ROOT,asset->relative);
        int absent=stat(path,&st)==-1&&errno==ENOENT;
        int size_ok=!stat(asset->package,&st)&&st.st_size>=0&&(u64)st.st_size==asset->size;
        int hash_ok=cached;u64 bytes=0;
        if(!absent||!size_ok)ok=0;
        printf("\x1b[3;1H\x1b[J%s %u/%u\n%s\n",cached?"Checking":"Verifying",i+1,(unsigned)WA2_PACKAGE_COUNT,asset->relative);consoleUpdate(NULL);
        if(ok&&!cached){
            Sha256Context hash;unsigned char digest[32];sha256ContextCreate(&hash);f=fopen(asset->package,"rb");
            if(!f)ok=0;
            else{
                size_t n;
                while((n=fread(wa2_hash_buffer,1,sizeof(wa2_hash_buffer),f))){
                    sha256ContextUpdate(&hash,wa2_hash_buffer,n);bytes+=n;done+=n;
                    u64 now=armGetSystemTick();
                    if(armTicksToNs(now-last_ui)>150000000){
                        printf("\x1b[6;1H%llu / %llu MiB  (%u%%)\n",(unsigned long long)(done>>20),(unsigned long long)(total>>20),(unsigned)(done*100/total));consoleUpdate(NULL);last_ui=now;
                        if(!appletMainLoop()){ok=0;break;}
                    }
                }
                if(ferror(f))ok=0;fclose(f);
            }
            sha256ContextGetHash(&hash,digest);hash_ok=bytes==asset->size&&!memcmp(digest,asset->sha,32);ok &= hash_ok;
        }
        if(log){fprintf(log,"asset=%s bytes=%llu sd_absent=%d size_match=%d sha256=%s\n",asset->package,(unsigned long long)asset->size,absent,size_ok,cached?"cached":hash_ok?"PASS":"FAIL");fflush(log);}
    }
    if(ok&&!cached){
        f=fopen(temporary,"wb");int wrote=0;
        if(f){wrote=fwrite(WA2_PACKAGE_ID,1,64,f)==64;if(fflush(f))wrote=0;if(fclose(f))wrote=0;}
        if(wrote){remove(marker);if(rename(temporary,marker))wrote=0;}
        ok &= wrote;
    }
    if(ok&&force&&remove(force_path))ok=0;
    wa2_package_check_ms=armTicksToNs(armGetSystemTick()-wa2_launch_started)/1000000;
    if(log){fprintf(log,"elapsed_ms=%llu\nresult=%s\n",(unsigned long long)wa2_package_check_ms,ok?"PASS":"FAIL");fclose(log);}
    f=fopen(WINE_NX_SD_ROOT "/package-history.log","a");if(f){fprintf(f,"version=0.3.1 mode=%s elapsed_ms=%llu result=%s\n",cached?"cached":"full",(unsigned long long)wa2_package_check_ms,ok?"PASS":"FAIL");fclose(f);}
    appletSetAutoSleepDisabled(false);
    if(ok){printf("\x1b[8;1HPackage ready. Starting WA2...\n");consoleUpdate(NULL);consoleExit(NULL);return 1;}
    printf("\x1b[8;1HPackage check FAILED. See package-mount.log.\nPress + to return to HOME.\n");
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(NULL);svcSleepThread(16000000);}
    consoleExit(NULL);return 0;
}
