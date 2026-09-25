/* Small no-CRT x86 program exercising per-game registry and writable paths. */
#include <windows.h>
#include <shlobj.h>
#include "../../../include/wine/nx_root.h"

void *memset(void *out,int value,size_t n){unsigned char *p=out;while(n--)*p++=(unsigned char)value;return out;}
void *memcpy(void *out,const void *in,size_t n){unsigned char *d=out;const unsigned char *s=in;while(n--)*d++=*s++;return out;}
static BOOL write_bytes(const char *path,const void *data,DWORD size){
    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    DWORD done=0;BOOL ok=WriteFile(f,data,size,&done,NULL)&&done==size;
    if(ok)ok=FlushFileBuffers(f);
    CloseHandle(f);return ok;
}
static BOOL has_text(const char *path,const char *expected){
    char data[128]={0};DWORD got=0;
    HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    BOOL ok=ReadFile(f,data,sizeof(data)-1,&got,NULL);
    CloseHandle(f);
    while(got && (data[got-1]=='\r'||data[got-1]=='\n'))data[--got]=0;
    return ok&&!lstrcmpA(data,expected);
}
void __stdcall start(void){
    static char docs[MAX_PATH]={0},dir[MAX_PATH]={0},counter_path[MAX_PATH]={0},module[MAX_PATH]={0};
    static char message[2048],details[1536];
    DWORD registry_count=0,file_count=0,type=0,bytes=sizeof(DWORD),next;
    HKEY key=NULL;BOOL registry_ok=FALSE,file_ok=TRUE;LONG status;
    HRESULT docs_hr=SHGetFolderPathA(NULL,CSIDL_PERSONAL|CSIDL_FLAG_CREATE,NULL,SHGFP_TYPE_CURRENT,docs);
    GetModuleFileNameA(NULL,module,sizeof(module));
    status=RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\AutorunRuntimeProbe",0,NULL,0,KEY_READ|KEY_WRITE,NULL,&key,NULL);
    if(status==ERROR_SUCCESS){
        status=RegQueryValueExA(key,"RunCount",NULL,&type,(BYTE*)&registry_count,&bytes);
        registry_ok=status==ERROR_FILE_NOT_FOUND || (status==ERROR_SUCCESS && type==REG_DWORD && bytes==4);
        if(status==ERROR_FILE_NOT_FOUND)registry_count=0;
    }
    if(SUCCEEDED(docs_hr) && lstrlenA(docs)<MAX_PATH-48){
        wsprintfA(dir,"%s\\AutorunRuntimeProbe",docs);
        if(!CreateDirectoryA(dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)file_ok=FALSE;
        wsprintfA(counter_path,"%s\\count.dat",dir);
        HANDLE f=CreateFileA(counter_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if(f!=INVALID_HANDLE_VALUE){
            DWORD got=0;file_ok=ReadFile(f,&file_count,sizeof(file_count),&got,NULL)&&got==sizeof(file_count);
            CloseHandle(f);
        }else if(GetLastError()!=ERROR_FILE_NOT_FOUND)file_ok=FALSE;
    }else file_ok=FALSE;
    BOOL persisted=registry_ok&&file_ok&&registry_count==file_count;
    next=registry_count+1;
    if(registry_ok)registry_ok=RegSetValueExA(key,"RunCount",0,REG_DWORD,(const BYTE*)&next,sizeof(next))==ERROR_SUCCESS;
    if(key)RegCloseKey(key);
    if(file_ok)file_ok=write_bytes(counter_path,&next,sizeof(next));
    BOOL setup_ok=has_text("Z:" WINE_NX_ROOT "/registry/components-2.done","version 2, exit code 0x0");
    BOOL asset_ok=has_text("C:\\Game\\cache-probe.dat","Autorun game asset probe v0.3.0");
    BOOL ok=persisted&&registry_ok&&file_ok&&setup_ok&&asset_ok;
    wsprintfA(details,"Autorun runtime probe 0.3.0\r\nResult: %s\r\nRun count: %lu\r\nPrevious registry/file: %lu / %lu\r\nDocuments: %s\r\nModule: %s\r\nRegistry: %s\r\nDocument file: %s\r\nComponents setup: %s\r\nGame asset: %s\r\nDocuments HRESULT: 0x%08lX\r\n",ok?"PASS":"FAIL",next,registry_count,file_count,docs,module,registry_ok?"PASS":"FAIL",file_ok?"PASS":"FAIL",setup_ok?"PASS":"FAIL",asset_ok?"PASS":"FAIL",(DWORD)docs_hr);
    BOOL report_ok=write_bytes("C:\\RuntimeProbe\\probe-result.txt",details,(DWORD)lstrlenA(details));
    ok=ok&&report_ok;
    wsprintfA(message,"%s\r\nReport file: %s\r\n\r\nClose this dialog to return to HOME.\r\nLaunch again: count should increase by one.",details,report_ok?"PASS":"FAIL");
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message,(DWORD)lstrlenA(message),&written,NULL);
    MessageBoxA(NULL,message,ok?"Autorun Runtime Probe - PASS":"Autorun Runtime Probe - FAIL",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
    ExitProcess(ok?0:1);
}
