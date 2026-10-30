#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
// usage: scan lo hi [max]  -> prints file offset (hex) and value of aligned BE words in [lo,hi)
int main(int c,char**v){int fd=open("/home/nakas/gtav-linux/logs/hang3_guestmem/guest.bin",O_RDONLY);struct stat st;fstat(fd,&st);
uint8_t*p=mmap(0,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);uint32_t lo=strtoul(v[1],0,16),hi=strtoul(v[2],0,16);long mx=c>3?atol(v[3]):100000,n=0;
for(size_t i=0;i+4<=st.st_size;i+=4){uint32_t w=__builtin_bswap32(*(uint32_t*)(p+i));if(w>=lo&&w<hi){printf("%zx %08x\n",i,w);if(++n>=mx)break;}}}
