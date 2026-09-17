#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>
int wmain(int argc,wchar_t **argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=4){std::fprintf(stderr,"Expected DLL, level, seedHex");return 2;}
    wchar_t *end=nullptr;const auto level=std::wcstoul(argv[2],&end,10);
    if(!end||*end||(level!=1&&level!=3&&level!=0x11)){std::fprintf(stderr,"Unsupported security level");return 3;}
    const auto n=std::wcslen(argv[3]);
    if(!n||n%32||n>8192){std::fprintf(stderr,"Seed must contain complete 16-byte blocks");return 4;}
    std::vector<unsigned char> seed(n/2),key(8192,0);
    auto nibble=[](wchar_t c){if(c>=L'0'&&c<=L'9')return int(c-L'0');if(c>=L'a'&&c<=L'f')return int(c-L'a'+10);if(c>=L'A'&&c<=L'F')return int(c-L'A'+10);return -1;};
    for(size_t i=0;i<seed.size();++i){int a=nibble(argv[3][2*i]),b=nibble(argv[3][2*i+1]);if(a<0||b<0)return 4;seed[i]=static_cast<unsigned char>(a*16+b);}
    auto library=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!library){std::fprintf(stderr,"LoadLibrary error %lu",GetLastError());return 5;}
    using Generate=int (__cdecl *)(unsigned char*,unsigned int,unsigned int,char*,unsigned char*,unsigned int,unsigned int&);
    auto generate=reinterpret_cast<Generate>(GetProcAddress(library,"GenerateKeyEx"));
    if(!generate){std::fprintf(stderr,"GenerateKeyEx export missing");FreeLibrary(library);return 6;}
    unsigned int length=0;char variant[]="";
    const int result=generate(seed.data(),static_cast<unsigned int>(seed.size()),static_cast<unsigned int>(level),variant,key.data(),static_cast<unsigned int>(key.size()),length);
    if(result||length!=16){std::fprintf(stderr,"GenerateKeyEx status %d, length %u",result,length);FreeLibrary(library);return 7;}
    for(unsigned int i=0;i<length;++i)std::printf("%02x",key[i]);std::puts("");
    FreeLibrary(library);return 0;
}
