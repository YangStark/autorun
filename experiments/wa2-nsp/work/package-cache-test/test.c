#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
typedef uint64_t u64; typedef unsigned Result; typedef int PadState;
#define WINE_NX_SD_ROOT "test-root"
#define InfoType_ProgramId 0
#define CUR_PROCESS_HANDLE 0
#define HidNpadStyleSet_NpadStandard 0
#define HidNpadButton_Plus 1
#define R_SUCCEEDED(x) (!(x))
static u64 ticks; static u64 armGetSystemTick(void){return ++ticks;}
static u64 armTicksToNs(u64 n){return n;}
static Result svcGetInfo(u64 *p,int a,int b,int c){*p=0x0500A17E00060000ULL;return 0;}
static int romfsMountFromCurrentProcess(const char *p){return 0;}
static void consoleInit(void *p){} static void consoleExit(void *p){} static void consoleUpdate(void *p){}
static void appletSetAutoSleepDisabled(bool b){} static int appletMainLoop(void){return 0;}
static void padConfigureInput(int a,int b){} static void padInitializeDefault(PadState *p){}
static void padUpdate(PadState *p){} static int padGetButtonsDown(PadState *p){return 1;}
static void svcSleepThread(u64 a){}
extern void *EVP_MD_CTX_new(void);extern const void *EVP_sha256(void);
extern int EVP_DigestInit_ex(void *,const void *,void *);extern int EVP_DigestUpdate(void *,const void *,size_t);
extern int EVP_DigestFinal_ex(void *,unsigned char *,unsigned int *);extern void EVP_MD_CTX_free(void *);
typedef struct {void *p;} Sha256Context;
static void sha256ContextCreate(Sha256Context *c){c->p=EVP_MD_CTX_new();if(!c->p||!EVP_DigestInit_ex(c->p,EVP_sha256(),NULL))abort();}
static void sha256ContextUpdate(Sha256Context *c,const void *p,size_t n){if(!EVP_DigestUpdate(c->p,p,n))abort();}
static void sha256ContextGetHash(Sha256Context *c,void *p){unsigned n;if(!EVP_DigestFinal_ex(c->p,p,&n)||n!=32)abort();EVP_MD_CTX_free(c->p);}
#include "../../../../wine-nx-probe/source/package_asset_mount.h"
int main(void){return wa2_package_mount()?0:1;}
