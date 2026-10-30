#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
static uint32_t be(uint8_t*p){return __builtin_bswap32(*(uint32_t*)p);}
int main(){int fd=open("/home/nakas/gtav-linux/logs/hang3_guestmem/guest.bin",O_RDONLY);struct stat st;fstat(fd,&st);
uint8_t*p=mmap(0,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
for(size_t i=0;i+0x40<=st.st_size;i+=4){uint32_t n=be(p+i+0x20);if(n<1||n>7)continue;int ok=1;
 for(uint32_t k=0;k<n;k++){uint32_t h=be(p+i+4+4*k);if(h<0x82c00000||h>=0x83600000){ok=0;break;}}
 if(!ok||n<2)continue; for(uint32_t k=n;k<7;k++){ if(be(p+i+4+4*k)!=0 && (be(p+i+4+4*k)<0x82000000||be(p+i+4+4*k)>=0x84000000)){ok=0;break;}}
 if(!ok)continue;
 printf("%zx",i);for(uint32_t k=0;k<n;k++)printf(" %08x:%08x",be(p+i+4+4*k),be(p+i+0x24+4*k));printf("\n");}}
