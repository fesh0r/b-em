/*B-em 0.6 by Tom Walker*/
/*6502 emulation*/

#include <allegro.h>
#include <stdio.h>
#include <dir.h>

#include "b-em.h"
#include "8271.h"
#include "serial.h"

int adcconvert;
int shadowaddr[16]={0,0,0,0,0,0,0,0,0,0,1,0,1,1,0,0};

int countit=0;
unsigned char oldfa;
unsigned short oldpc,oldoldpc,pc3;

int output2=0;

int model=2; /*0=PAL B, 1=NTSC B, 2=B+*/

unsigned char currom;
/*6502 registers*/
unsigned char a,x,y,s;
unsigned short pc;
//struct
//{
//        int c,z,i,d,v,n;
//} p;

/*Memory structures*/
unsigned char *mem[0x100];
int memstat[0x100];
unsigned char *ram,*rom;
unsigned char os[0x4000];
int writeablerom[16];

int cycles=0;
int output=0;
int ins=0;

void dumpram()
{
        FILE *f=fopen("ram.dmp","wb");
        fwrite(ram,32768,1,f);
        fclose(f);
}

void initmem()
{
        int c;
        ram=(unsigned char *)malloc(0x10000);
        rom=(unsigned char *)malloc(0x40000);
        memset(ram,0,0x8000);
        for (c=0x00;c<0x080;c++) mem[c]=(unsigned char *)(ram+(c<<8));
        for (c=0x80;c<0x0C0;c++) mem[c]=(unsigned char *)(rom+((c&0x3F)<<8));
        for (c=0xC0;c<0x100;c++) mem[c]=(unsigned char *)(os+((c&0x3F)<<8));
        for (c=0x00;c<0x080;c++) memstat[c]=0;
        for (c=0x80;c<0x100;c++) memstat[c]=1;
        memstat[0xFE]=2;
//        atexit(dumpram);
}

void remaketables()
{
        int c;
        for (c=0x00;c<0x080;c++) mem[c]=(unsigned char *)(ram+(c<<8));
        for (c=0x80;c<0x0C0;c++) mem[c]=(unsigned char *)(rom+((c&0x3F)<<8));
        for (c=0xC0;c<0x100;c++) mem[c]=(unsigned char *)(os+((c&0x3F)<<8));
        for (c=0x00;c<0x080;c++) memstat[c]=0;
        for (c=0x80;c<0x100;c++) memstat[c]=1;
        memstat[0xFE]=2;
}

#if 0
typedef struct ffblk {
  char lfn_magic[6];    /* LFN: the magic "LFN32" signature */
  short lfn_handle;    /* LFN: the handle used by findfirst/findnext */
  unsigned short lfn_ctime; /* LFN: file creation time */
  unsigned short lfn_cdate; /* LFN: file creation date */
  unsigned short lfn_atime; /* LFN: file last access time (usually 0) */
  unsigned short lfn_adate; /* LFN: file last access date */
  char ff_reserved[5];      /* used to hold the state of the search */
  unsigned char ff_attrib;  /* actual attributes of the file found */
  unsigned short ff_ftime;  /* hours:5, minutes:6, (seconds/2):5 */
  unsigned short ff_fdate;  /* (year-1980):7, month:4, day:5 */
  unsigned long ff_fsize;   /* size of file */
  char ff_name[260];        /* name of file as ASCIIZ string */
} ffblk;
#endif

void loadroms()
{
        int addr=0x3C000;
        int c;
        int finished=0,romslot=0xF;
        FILE *f;
        struct al_ffblk ff;
        if (chdir("roms"))
        {
                perror("roms");
                exit(-1);
        }
        switch (model)
        {
                case 0: f=fopen("os","rb"); break;
                case 1: f=fopen("usos","rb"); break;
                case 2: f=fopen("bpos","rb"); break;
        }
        fread(os,0x4000,1,f);
        fclose(f);
        switch (model)
        {
                case 0: case 1: if (chdir("b")) { perror("b"); exit(-1); } break;
                case 2: if (chdir("bp")) { perror("bp"); exit(-1); } break;
        }
        c=al_findfirst("*.rom",&ff,FA_RDONLY|FA_HIDDEN|FA_SYSTEM|FA_LABEL|FA_DIREC|FA_ARCH);
//        printf("Results : %i %i\n",c,errno);
        for (c=0;c<16;c++) writeablerom[c]=1;
        memset(rom,0,0x40000);
        while (romslot >= 0 && !finished)
        {
//                printf("Loading %s into slot %01X offset %05X\n",ff.name,romslot,addr);
                f=fopen(ff.name,"rb");
                if (!f)
                {
                        finished=1;
                        break;
                }
                fread(rom+addr,16384,1,f);
                fclose(f);
                addr-=0x4000;
                writeablerom[romslot]=0;
                romslot--;
                finished = al_findnext(&ff);
        }
        if (chdir(".."))
        {
                perror("..");
                exit(-1);
        }
        if (chdir(".."))
        {
                perror("..");
                exit(-1);
        }
}

unsigned char acccon=0;

void writeacccon(unsigned char v)
{
        #if 0
        /*ALL WRONG. This is the Master 128 way of doing things*/
        int c;
        printf("ACCCON write %02X %04X\n",v,pc);
        if ((acccon&4)^(v&4)) /*Change 3000-7FFF mapping*/
        {
                if (v&4)
                {
                        for (c=0x30;c<0x080;c++)
                            mem[c]=(unsigned char *)(ram+((c|0x80)<<8));
                }
                else
                {
                        for (c=0x30;c<0x080;c++)
                            mem[c]=(unsigned char *)(ram+(c<<8));
                }
        }
        if ((acccon&8)^(v&8)) /*Change C000-DFFF mapping*/
        {
                if (v&8)
                {
                        for (c=0xC0;c<0xE0;c++)
                        {
                                mem[c]=(unsigned char *)(ram+((c&0x1F)<<8));
                                memstat[c]=0;
                        }
                }
                else
                {
                        for (c=0xC0;c<0xE0;c++)
                        {
                                mem[c]=(unsigned char *)(os+((c&0x3F)<<8));
                                memstat[c]=1;
                        }
                }
        }
//        if (v&0x80) interrupt|=8;
//        else        interrupt&=~8;
        #endif
        acccon=v;
        vidbank=(v&0x80)?0x8000:0;
        if (!vidbank) shadowram(0);
}

unsigned char readmeml(unsigned short addr)
{
        switch (addr&0xFFF8)
        {
                case 0xFE00: return readcrtc(addr);
                case 0xFE08: return readacia(addr);
                case 0xFE10: case 0xFE18: return readserial(addr);
                case 0xFE30: if (addr==0xFE34) return 0; break;
                case 0xFE40: case 0xFE48: case 0xFE50: case 0xFE58: return readsysvia(addr);
                case 0xFE60: case 0xFE68: case 0xFE70: case 0xFE78: return readuservia(addr);
                case 0xFE80: case 0xFE88: case 0xFE90: case 0xFE98: if (model==2) return read1770(addr); else return read8271(addr);
                case 0xFEA0: case 0xFEA8: return 0xFE; /*I wonder what Arcadians wants with Econet...*/
                case 0xFEC0: case 0xFEC8: case 0xFED0: case 0xFED8: return readadc(addr);
                case 0xFEE0: return 0;
        }
/*        printf("Error : Bad read from %04X\n",addr);
        dumpregs();
        exit(-1);*/
}

unsigned char writememl(unsigned short addr, unsigned char val, int line)
{
        int c;
        switch (addr&0xFFF8)
        {
                case 0xFE00: writecrtc(addr,val); return;
                case 0xFE08: writeacia(addr,val); return;
                case 0xFE10: case 0xFE18: writeserial(addr,val); return;
                case 0xFE20: writeula(addr,val,line); return;
                case 0xFE30:
                if (addr==0xFE34)
                {
                        if (model==2)
                        {
                                writeacccon(val);
                        }
                        return;
                }
                if (addr==0xFE32) return; /*??? - Time and Magik writes here*/
                if (addr==0xFE30)
                {
//                        if (output2) printf("Current ROM %02X at %04X %i %05X\n",val,pc,ins,(val&15)<<14);
                        currom=val;
                        for (c=0x80;c<0xC0;c++) mem[c]=(unsigned char *)(rom+((val&15)<<14)+((c&0x3F)<<8));
                        for (c=0x80;c<0xC0;c++) memstat[c]=(writeablerom[val&15])?0:1;
                        if (val&0x80 && model==2)
                        {
//                                printf("Mapping in 8000-AFFF at %04X\n",pc);
                                for (c=0x80;c<0xB0;c++) mem[c]=(unsigned char *)(ram+(c<<8));
                                for (c=0x80;c<0xB0;c++) memstat[c]=0;
                                shadowaddr[0xA]=1;
                        }
                        else if (model==2) shadowaddr[0xA]=0;
                        return;
                }
                break;
                case 0xFE38: return;
                case 0xFE40: case 0xFE48: case 0xFE50: case 0xFE58: writesysvia(addr,val,line); return;
                case 0xFE60: case 0xFE68: case 0xFE70: case 0xFE78: writeuservia(addr,val,line); return;
                case 0xFE80: case 0xFE88: case 0xFE90: case 0xFE98: if (model==2) write1770(addr,val); else write8271(addr,val); return;
                case 0xFEA0: return; /*Now Repton Infinity wants Econet as well!*/
                case 0xFEC0: case 0xFEC8: case 0xFED0: case 0xFED8: writeadc(addr,val); return;
                case 0xFEE0: return;
        }
        if (!(addr&0x8000))
        {
                ram[addr]=val;
                return;
        }
        if (addr<0xC000) return;
        printf("Error : Bad write to %04X data %02X\n",addr,val);
        dumpregs();
        exit(-1);
}

#define readmem(a) ((memstat[a>>8]==2)?readmeml(a):mem[a>>8][a&0xFF])
#define writemem(a,b) if (memstat[a>>8]==0) mem[a>>8][a&0xFF]=b; else if (memstat[a>>8]==2) writememl(a,b,lines)
#define getw() (readmem(pc)|(readmem(pc+1)<<8)); pc+=2

void reset6502()
{
        pc=readmem(0xFFFC)|(readmem(0xFFFD)<<8);
        p.i=1;
        nmi=oldnmi=nmilock=0;
}

void dumpregs()
{
        printf("6502 registers :\n");
        printf("A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n",a,x,y,s,pc);
        printf("Status : %c%c%c%c%c%c\n",(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ');
        printf("%i instructions executed\n",ins);
        printf("%04X %04X %04X\n",oldpc,oldoldpc,pc3);
        printf("%i\n",countit);
}

#define setzn(v) p.z=!(v); p.n=(v)&0x80

#define push(v) ram[0x100+(s--)]=v
#define pull()  ram[0x100+(++s)]

#define polltime(c) { cycles-=c; sysvia.t1c-=c; sysvia.t2c-=c; if (sysvia.t1c<-4 || sysvia.t2c<-4) { updatesystimers(); } uservia.t1c-=c; uservia.t2c-=c; if (uservia.t1c<-4 || uservia.t2c<-4) updateusertimers(); }

/*ADC/SBC temp variables*/
unsigned short tempw;
int tempv,hc,al,ah;
unsigned char tempb;

#define ADC(temp)       if (!p.d)                            \
                        {                                  \
                                tempw=(a+temp+(p.c?1:0));        \
                                p.v=(!((a^temp)&0x80)&&((a^tempw)&0x80));  \
                                a=tempw&0xFF;                  \
                                p.c=tempw&0x100;                  \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                ah=0;        \
                                tempb=a+temp+(p.c?1:0);                            \
                                if (!tempb)                                      \
                                   p.z=1;                                          \
                                al=(a&0xF)+(temp&0xF)+(p.c?1:0);                            \
                                if (al>9)                                        \
                                {                                                \
                                        al-=10;                                  \
                                        al&=0xF;                                 \
                                        ah=1;                                    \
                                }                                                \
                                ah+=((a>>4)+(temp>>4));                             \
                                if (ah&8) p.n=1;                                   \
                                p.v=(((ah << 4) ^ a) & 128) && !((a ^ temp) & 128);   \
                                p.c=0;                                             \
                                if (ah>9)                                        \
                                {                                                \
                                        p.c=1;                                     \
                                        ah-=10;                                  \
                                        ah&=0xF;                                 \
                                }                                                \
                                a=(al&0xF)|(ah<<4);                              \
                        }

#define SBC(temp)       if (!p.d)                            \
                        {                                  \
                                tempw=a-(temp+(p.c?0:1));    \
                                tempv=(short)a-(short)(temp+(p.c?0:1));            \
                                p.v=((a^(temp+(p.c?0:1)))&(a^(unsigned char)tempv)&0x80); \
                                p.c=tempv>=0;\
                                a=tempw&0xFF;              \
                                setzn(a);                  \
                        }                                  \
                        else                               \
                        {                                  \
                                hc=0;                               \
                                p.z=p.n=0;                            \
                                if (!((a-temp)-((p.c)?0:1)))            \
                                   p.z=1;                             \
                                al=(a&15)-(temp&15)-((p.c)?0:1);      \
                                if (al&16)                           \
                                {                                   \
                                        al-=6;                      \
                                        al&=0xF;                    \
                                        hc=1;                       \
                                }                                   \
                                ah=(a>>4)-(temp>>4);                \
                                if (hc) ah--;                       \
                                if ((a-(temp+((p.c)?0:1)))&0x80)        \
                                   p.n=1;                             \
                                p.v=(((a-(temp+((p.c)?0:1)))^temp)&128)&&((a^temp)&128); \
                                p.c=1; \
                                if (ah&16)                           \
                                {                                   \
                                        p.c=0; \
                                        ah-=6;                      \
                                        ah&=0xF;                    \
                                }                                   \
                                a=(al&0xF)|((ah&0xF)<<4);                 \
                        }

int inA=0;

void shadowram(int stat)
{
        int c;
        if (stat)
        {
                for (c=0x30;c<0x080;c++)
                    mem[c]=(unsigned char *)(ram+((c|0x80)<<8));
        }
        else
        {
                for (c=0x30;c<0x080;c++)
                    mem[c]=(unsigned char *)(ram+(c<<8));
        }
        inA=stat;
}

void exec6502(int lines, int cpl)
{
        unsigned char opcode;
        unsigned short addr;
        unsigned char temp,temp2;
        int tempi;
        signed char offset;
        int c;
        while (lines>=0)
        {
                if (lines==0) cycles+=(cpl>>1);
                else          cycles+=cpl;
                while (cycles>0)
                {
//                        pc3=oldoldpc;
//                        oldoldpc=oldpc;
//                        oldpc=pc;
                        if (model==2 && vidbank)
                        {
                                if (!inA && shadowaddr[pc>>12])     shadowram(1);
                                else if (inA && !shadowaddr[pc>>12]) shadowram(0);
                        }
                        opcode=readmem(pc); pc++;
//                        oldfa=ram[0xFA];
                        switch (opcode)
                        {
                                case 0x00: /*BRK*/
                                pc++;
                                push(pc>>8);
                                push(pc&0xFF);
                                temp=0x30;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.d) temp|=8; if (p.v) temp|=0x40;
                                if (p.n) temp|=0x80;
                                push(temp);
                                pc=readmem(0xFFFE)|(readmem(0xFFFF)<<8);
                                p.i=1;
                                polltime(7);
                                break;

                                case 0x01: /*ORA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a|=readmem(addr);
                                setzn(a);
                                polltime(6);
                                break;

                                case 0x05: /*ORA zp*/
                                addr=readmem(pc); pc++;
                                a|=ram[addr];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0x06: /*ASL zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x08: /*PHP*/
                                temp=0x30;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.i) temp|=4; if (p.d) temp|=8;
                                if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                                push(temp);
                                polltime(3);
                                break;

                                case 0x09: /*ORA imm*/
                                a|=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x0A: /*ASL A*/
                                p.c=a&0x80;
                                a<<=1;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x0B: /*ANC imm*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                p.c=p.n;
                                polltime(2);
                                break;

                                case 0x0D: /*ORA abs*/
                                addr=getw();
                                a|=readmem(addr);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x0E: /*ASL abs*/
                                addr=getw();
                                temp=readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(6);
                                break;

                                case 0x10: /*BPL*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (!p.n)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0x11: /*ORA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a|=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x15: /*ORA zp,x*/
                                addr=readmem(pc); pc++;
                                a|=ram[(addr+x)&0xFF];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0x16: /*ASL zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=ram[addr];
                                p.c=temp&0x80;
                                temp<<=1;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x18: /*CLC*/
                                p.c=0;
                                polltime(2);
                                break;

                                case 0x19: /*ORA abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a|=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x1D: /*ORA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                a|=readmem(addr+x);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x1E: /*ASL abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr);
                                p.c=temp&0x80;
                                temp<<=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                break;

                                case 0x20: /*JSR*/
                                addr=getw(); pc--;
                                push(pc>>8);
                                push(pc);
                                pc=addr;
                                polltime(6);
                                break;

                                case 0x21: /*AND (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a&=readmem(addr);
                                setzn(a);
                                polltime(6);
                                break;

                                case 0x24: /*BIT zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                polltime(3);
                                break;

                                case 0x25: /*AND zp*/
                                addr=readmem(pc); pc++;
                                a&=ram[addr];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0x26: /*ROL zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x28: /*PLP*/
                                temp=pull();
                                p.c=temp&1; p.z=temp&2;
                                p.i=temp&4; p.d=temp&8;
                                p.v=temp&0x40; p.n=temp&0x80;
                                polltime(4);
                                break;

                                case 0x29: /*AND*/
                                a&=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x2A: /*ROL A*/
                                tempi=p.c;
                                p.c=a&0x80;
                                a<<=1;
                                if (tempi) a|=1;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x2C: /*BIT abs*/
                                addr=getw();
                                temp=readmem(addr);
                                p.z=!(a&temp);
                                p.v=temp&0x40;
                                p.n=temp&0x80;
                                polltime(4);
                                break;

                                case 0x2D: /*AND abs*/
                                addr=getw();
                                a&=readmem(addr);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x2E: /*ROL abs*/
                                addr=getw();
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6);
                                break;

                                case 0x30: /*BMI*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (p.n)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0x31: /*AND (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a&=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x35: /*AND zp,x*/
                                addr=readmem(pc); pc++;
                                a&=ram[(addr+x)&0xFF];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0x36: /*ROL zp,x*/
                                addr=readmem(pc); pc++;
                                addr+=x; addr&=0xFF;
                                temp=ram[addr];
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x38: /*SEC*/
                                p.c=1;
                                polltime(2);
                                break;

                                case 0x39: /*AND abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a&=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x3D: /*AND abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                a&=readmem(addr+x);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x3E: /*ROL abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&0x80;
                                temp<<=1;
                                if (tempi) temp|=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                break;

                                case 0x40: /*RTI*/
                                temp=pull();
                                p.c=temp&1; p.z=temp&2;
                                p.i=temp&4; p.d=temp&8;
                                p.v=temp&0x40; p.n=temp&0x80;
                                pc=pull();
                                pc|=(pull()<<8);
                                polltime(6);
                                nmilock=0;
                                break;

                                case 0x41: /*EOR (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a^=readmem(addr);
                                setzn(a);
                                polltime(6);
                                break;

                                case 0x45: /*EOR zp*/
                                addr=readmem(pc); pc++;
                                a^=ram[addr];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0x46: /*LSR zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x48: /*PHA*/
                                push(a);
                                polltime(3);
                                break;

                                case 0x49: /*EOR*/
                                a^=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x4A: /*LSR A*/
                                p.c=a&1;
                                a>>=1;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x4C: /*JMP*/
                                addr=getw();
                                pc=addr;
                                polltime(3);
                                break;

                                case 0x4D: /*EOR abs*/
                                addr=getw();
                                a^=readmem(addr);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x4E: /*LSR abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                writemem(addr,temp);
                                polltime(6);
                                break;

                                case 0x50: /*BVC*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (!p.v)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0x51: /*EOR (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a^=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x55: /*EOR zp,x*/
                                addr=readmem(pc); pc++;
                                a^=ram[(addr+x)&0xFF];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0x56: /*LSR zp,x*/
                                addr=(readmem(pc)+x)&0xFF; pc++;
                                temp=ram[addr];
                                p.c=temp&1;
                                temp>>=1;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x58: /*CLI*/
                                p.i=0;
                                polltime(2);
                                break;

                                case 0x59: /*EOR abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a^=readmem(addr+y);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x5D: /*EOR abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                a^=readmem(addr+x);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x5E: /*LSR abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr);
                                p.c=temp&1;
                                temp>>=1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                break;

                                case 0x60: /*RTS*/
                                pc=pull();
                                pc|=(pull()<<8);
                                pc++;
                                polltime(6);
                                break;

                                case 0x61: /*ADC (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                ADC(temp);
                                polltime(6);
                                break;

                                case 0x65: /*ADC zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                ADC(temp);
                                polltime(3);
                                break;

                                case 0x66: /*ROR zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x68: /*PLA*/
                                a=pull();
                                setzn(a);
                                polltime(4);
                                break;

                                case 0x69: /*ADC imm*/
                                temp=readmem(pc); pc++;
                                ADC(temp);
                                polltime(2);
                                break;

                                case 0x6A: /*ROR A*/
                                tempi=p.c;
                                p.c=a&1;
                                a>>=1;
                                if (tempi) a|=0x80;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x6C: /*JMP ()*/
                                addr=getw();
                                pc=readmem(addr)|(readmem(addr+1)<<8);
                                polltime(5);
                                break;

                                case 0x6D: /*ADC abs*/
                                addr=getw();
                                temp=readmem(addr);
                                ADC(temp);
                                polltime(4);
                                break;

                                case 0x6E: /*ROR abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr);
                                polltime(1);
                                writemem(addr,temp);
                                polltime(1);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                writemem(addr,temp);
                                break;

                                case 0x70: /*BVS*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (p.v)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0x71: /*ADC (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                ADC(temp);
                                polltime(5);
                                break;

                                case 0x75: /*ADC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=ram[(addr+x)&0xFF];
                                ADC(temp);
                                polltime(4);
                                break;

                                case 0x76: /*ROR zp,x*/
                                addr=readmem(pc); pc++;
                                addr+=x; addr&=0xFF;
                                temp=ram[addr];
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                setzn(temp);
                                ram[addr]=temp;
                                polltime(5);
                                break;

                                case 0x78: /*SEI*/
                                p.i=1;
                                polltime(2);
//                                if (output2) printf("SEI at line %i %04X %02X %02X\n",lines,pc,ram[0x103+s],ram[0x104+s]);
                                break;

                                case 0x79: /*ADC abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                ADC(temp);
                                polltime(4);
                                break;

                                case 0x7D: /*ADC abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                ADC(temp);
                                polltime(4);
                                break;

                                case 0x7E: /*ROR abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr);
                                tempi=p.c;
                                p.c=temp&1;
                                temp>>=1;
                                if (tempi) temp|=0x80;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(7);
                                break;

                                case 0x81: /*STA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                writemem(addr,a);
                                polltime(6);
                                break;

                                case 0x84: /*STY zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]=y;
                                polltime(3);
                                break;

                                case 0x85: /*STA zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]=a;
                                polltime(3);
                                break;

                                case 0x86: /*STX zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]=x;
                                polltime(3);
                                break;

                                case 0x88: /*DEY*/
                                y--;
                                setzn(y);
                                polltime(2);
                                break;

                                case 0x8A: /*TXA*/
                                a=x;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x8C: /*STY abs*/
                                addr=getw();
                                polltime(3);
                                writemem(addr,y);
                                polltime(1);
                                break;

                                case 0x8D: /*STA abs*/
                                addr=getw();
                                polltime(3);
                                writemem(addr,a);
                                polltime(1);
                                break;

                                case 0x8E: /*STX abs*/
                                addr=getw();
                                polltime(3);
                                writemem(addr,x);
                                polltime(1);
                                break;

                                case 0x90: /*BCC*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (!p.c)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0x91: /*STA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8)+y;
                                writemem(addr,a);
                                polltime(6);
                                break;

                                case 0x94: /*STY zp,x*/
                                addr=readmem(pc); pc++;
                                ram[(addr+x)&0xFF]=y;
                                polltime(4);
                                break;

                                case 0x95: /*STA zp,x*/
                                addr=readmem(pc); pc++;
                                ram[(addr+x)&0xFF]=a;
                                polltime(4);
                                break;

                                case 0x96: /*STX zp,y*/
                                addr=readmem(pc); pc++;
                                ram[(addr+y)&0xFF]=x;
                                polltime(4);
                                break;

                                case 0x98: /*TYA*/
                                a=y;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x99: /*STA abs,y*/
                                addr=getw();
                                polltime(4);
                                writemem(addr+y,a);
                                polltime(1);
                                break;

                                case 0x9A: /*TXS*/
                                s=x;
                                polltime(2);
                                break;

                                case 0x9D: /*STA abs,x*/
                                addr=getw();
                                polltime(4);
                                writemem(addr+x,a);
                                polltime(1);
                                break;

                                case 0xA0: /*LDY imm*/
                                y=readmem(pc); pc++;
                                setzn(y);
                                polltime(2);
                                break;

                                case 0xA1: /*LDA (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                a=readmem(addr);
                                setzn(a);
                                polltime(6);
                                break;

                                case 0xA2: /*LDX imm*/
                                x=readmem(pc); pc++;
                                setzn(x);
                                polltime(2);
                                break;

                                case 0xA4: /*LDY zp*/
                                addr=readmem(pc); pc++;
                                y=ram[addr];
                                setzn(y);
                                polltime(3);
                                break;

                                case 0xA5: /*LDA zp*/
                                addr=readmem(pc); pc++;
                                a=ram[addr];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0xA6: /*LDX zp*/
                                addr=readmem(pc); pc++;
                                x=ram[addr];
                                setzn(x);
                                polltime(3);
                                break;

                                case 0xA8: /*TAY*/
                                y=a;
                                setzn(y);
                                break;

                                case 0xA9: /*LDA imm*/
                                a=readmem(pc); pc++;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0xAA: /*TAX*/
                                x=a;
                                setzn(x);
                                polltime(2);
                                break;

                                case 0xAC: /*LDY abs*/
                                addr=getw();
                                polltime(3);
                                y=readmem(addr);
                                setzn(y);
                                polltime(1);
                                break;

                                case 0xAD: /*LDA abs*/
                                addr=getw();
                                polltime(3);
                                a=readmem(addr);
                                setzn(a);
                                polltime(1);
                                break;

                                case 0xAE: /*LDX abs*/
                                addr=getw();
                                polltime(3);
                                x=readmem(addr);
                                setzn(x);
                                polltime(1);
                                break;

                                case 0xB0: /*BCS*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (p.c)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0xB1: /*LDA (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=readmem(addr+y);
                                setzn(a);
                                polltime(5);
                                break;

                                case 0xB4: /*LDY zp,x*/
                                addr=readmem(pc); pc++;
                                y=ram[(addr+x)&0xFF];
                                setzn(y);
                                polltime(3);
                                break;

                                case 0xB5: /*LDA zp,x*/
                                addr=readmem(pc); pc++;
                                a=ram[(addr+x)&0xFF];
                                setzn(a);
                                polltime(3);
                                break;

                                case 0xB6: /*LDX zp,y*/
                                addr=readmem(pc); pc++;
                                x=ram[(addr+y)&0xFF];
                                setzn(x);
                                polltime(3);
                                break;

                                case 0xB8: /*CLV*/
                                p.v=0;
                                polltime(2);
                                break;

                                case 0xB9: /*LDA abs,y*/
                                addr=getw();
                                polltime(3);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                a=readmem(addr+y);
                                setzn(a);
                                polltime(1);
                                break;

                                case 0xBA: /*TSX*/
                                x=s;
                                setzn(x);
                                polltime(2);
                                break;

                                case 0xBC: /*LDY abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                y=readmem(addr+x);
                                setzn(y);
                                polltime(4);
                                break;

                                case 0xBD: /*LDA abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                a=readmem(addr+x);
                                setzn(a);
                                polltime(4);
                                break;

                                case 0xBE: /*LDX abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                x=readmem(addr+y);
                                setzn(x);
                                polltime(4);
                                break;

                                case 0xC0: /*CPY imm*/
                                temp=readmem(pc); pc++;
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(2);
                                break;

                                case 0xC1: /*CMP (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(6);
                                break;

                                case 0xC4: /*CPY zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(3);
                                break;

                                case 0xC5: /*CMP zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(3);
                                break;

                                case 0xC6: /*DEC zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]--;
                                setzn(ram[addr]);
                                polltime(5);
                                break;

                                case 0xC8: /*INY*/
                                y++;
                                setzn(y);
                                polltime(2);
                                break;

                                case 0xC9: /*CMP imm*/
                                temp=readmem(pc); pc++;
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(2);
                                break;

                                case 0xCA: /*DEX*/
                                x--;
                                setzn(x);
                                polltime(2);
                                break;

                                case 0xCC: /*CPY abs*/
                                addr=getw();
                                temp=readmem(addr);
                                setzn(y-temp);
                                p.c=(y>=temp);
                                polltime(4);
                                break;

                                case 0xCD: /*CMP abs*/
                                addr=getw();
                                temp=readmem(addr);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                break;

                                case 0xCE: /*DEC abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr)-1;
                                polltime(1);
                                writemem(addr,temp+1);
                                polltime(1);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0xD0: /*BNE*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (!p.z)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0xD1: /*CMP (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(5);
                                break;

                                case 0xD5: /*CMP zp,x*/
                                addr=readmem(pc); pc++;
                                temp=ram[(addr+x)&0xFF];
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(3);
                                break;

                                case 0xD6: /*DEC zp,x*/
                                addr=readmem(pc); pc++;
                                ram[(addr+x)&0xFF]--;
                                setzn(ram[(addr+x)&0xFF]);
                                polltime(5);
                                break;

                                case 0xD8: /*CLD*/
                                p.d=0;
                                polltime(2);
                                break;

                                case 0xD9: /*CMP abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                break;

                                case 0xDD: /*CMP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                setzn(a-temp);
                                p.c=(a>=temp);
                                polltime(4);
                                break;

                                case 0xDE: /*DEC abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr)-1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6);
                                break;

                                case 0xE0: /*CPX imm*/
                                temp=readmem(pc); pc++;
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(3);
                                break;

                                case 0xE1: /*SBC (,x)*/
                                temp=readmem(pc)+x; pc++;
                                addr=readmem(temp)|(readmem(temp+1)<<8);
                                temp=readmem(addr);
                                SBC(temp);
                                polltime(6);
                                break;

                                case 0xE4: /*CPX zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(3);
                                break;

                                case 0xE5: /*SBC zp*/
                                addr=readmem(pc); pc++;
                                temp=ram[addr];
                                SBC(temp);
                                polltime(3);
                                break;

                                case 0xE6: /*INC zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]++;
                                setzn(ram[addr]);
                                polltime(5);
                                break;

                                case 0xE8: /*INX*/
                                x++;
                                setzn(x);
                                polltime(2);
                                break;

                                case 0xE9: /*SBC imm*/
                                temp=readmem(pc); pc++;
                                SBC(temp);
                                polltime(2);
                                break;

                                case 0xEA: /*NOP*/
                                polltime(2);
                                break;

                                case 0xEC: /*CPX abs*/
                                addr=getw();
                                temp=readmem(addr);
                                setzn(x-temp);
                                p.c=(x>=temp);
                                polltime(3);
                                break;

                                case 0xED: /*SBC abs*/
                                addr=getw();
                                temp=readmem(addr);
                                SBC(temp);
                                polltime(4);
                                break;

                                case 0xEE: /*DEC abs*/
                                addr=getw();
                                polltime(4);
                                temp=readmem(addr)+1;
                                polltime(1);
                                writemem(addr,temp-1);
                                polltime(1);
                                writemem(addr,temp);
                                setzn(temp);
                                break;

                                case 0xF0: /*BEQ*/
                                offset=(signed char)readmem(pc); pc++;
                                temp=2;
                                if (p.z)
                                {
                                        temp++;
                                        if ((pc&0xFF00)^((pc+offset)&0xFF00)) temp++;
                                        pc+=offset;
                                }
                                polltime(temp);
                                break;

                                case 0xF1: /*SBC (),y*/
                                temp=readmem(pc); pc++;
                                addr=readmem(temp)+(readmem(temp+1)<<8);
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                SBC(temp);
                                polltime(5);
                                break;

                                case 0xF5: /*SBC zp,x*/
                                addr=readmem(pc); pc++;
                                temp=ram[(addr+x)&0xFF];
                                SBC(temp);
                                polltime(3);
                                break;

                                case 0xF6: /*INC zp,x*/
                                addr=readmem(pc); pc++;
                                ram[(addr+x)&0xFF]++;
                                setzn(ram[(addr+x)&0xFF]);
                                polltime(5);
                                break;

                                case 0xF8: /*SED*/
                                p.d=1;
                                polltime(2);
                                break;

                                case 0xF9: /*SBC abs,y*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+y)&0xFF00)) polltime(1);
                                temp=readmem(addr+y);
                                SBC(temp);
                                polltime(4);
                                break;

                                case 0xFD: /*SBC abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                temp=readmem(addr+x);
                                SBC(temp);
                                polltime(4);
                                break;

                                case 0xFE: /*INC abs,x*/
                                addr=getw(); addr+=x;
                                temp=readmem(addr)+1;
                                writemem(addr,temp);
                                setzn(temp);
                                polltime(6);
                                break;

                                case 0x04: /*Undocumented - NOP zp*/
                                addr=readmem(pc); pc++;
                                polltime(3);
                                break;

                                case 0x07: /*Undocumented - SLO zp*/
                                addr=readmem(pc); pc++;
                                c=ram[addr]&0x80;
                                ram[addr]<<=1;
                                a|=ram[addr];
                                setzn(a);
                                polltime(5);
                                break;

                                case 0x23: /*Undocumented - RLA*/
                                break;     /*This was found in Repton 3 and
                                             looks like a mistake, so I'll
                                             ignore it for now*/

                                case 0x4B: /*Undocumented - ASR*/
                                a&=readmem(pc); pc++;
                                p.c=a&1;
                                a>>=1;
                                setzn(a);
                                polltime(2);
                                break;

                                case 0x67: /*Undocumented - RRA zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]>>=1;
                                if (p.c) ram[addr]|=1;
                                temp=ram[addr];
                                ADC(temp);
                                polltime(5);
                                break;

                                case 0x80: /*Undocumented - NOP imm*/
                                readmem(pc); pc++;
                                polltime(2);
                                break;

                                case 0x87: /*Undocumented - SAX zp*/
                                addr=readmem(pc); pc++;
                                ram[addr]=a&x;
                                polltime(3);
                                break;

                                case 0x9C: /*Undocumented - SHY abs,x*/
                                addr=getw();
                                writemem(addr+x,y&((addr>>8)+1));
                                polltime(5);
                                break;

                                case 0xDA: /*Undocumented - NOP*/
//                                case 0xFA:
                                polltime(2);
                                break;

                                case 0xDC: /*Undocumented - NOP abs,x*/
                                addr=getw();
                                if ((addr&0xFF00)^((addr+x)&0xFF00)) polltime(1);
                                readmem(addr+x);
                                polltime(4);
                                break;

                                case 0x02: /*TFS opcode - OSFSC*/
                                c=OSFSC();
                                if (c==6||c==8||c==0||c==5)
                                   pc=(pull()|(pull()<<8))+1;
                                if (c==0x80)
                                {
                                        temp=ram[pc++];
                                        c=(a>=temp);
                                        setzn(a-temp);
                                }
                                break;

                                case 0x92: /*TFS opcode - OSFILE*/
                                a=OSFILE();
                                if (a==0x80)
                                {
                                        push(a);
                                }
                                else if (a!=0x7F)
                                   pc=(pull()|(pull()<<8))+1;
                                break;

                                default:
                                allegro_exit();
                                printf("Error : Bad opcode %02X\n",opcode);
                                pc--;
                                dumpregs();
                                printf("Current ROM %02X\n",currom);
                                exit(-1);
                        }
//                        if (pc==0xF00F) printf("Enter key press %02X\n",x);
/*                        if (pc<0x8000 && (pc&0x0F00)!=0xD00 && !output2)
                        {*/
//                                output2=1;
/*                                printf("output2 at %04X %i\n",pc,ins);
                        }*/
//                        if (pc==0xFFF7) printf("FFF7 %02X %02X %02X\n",a,x,y);
//                        if (pc>0xE00 && pc<0x8000) output2=1;
//                        if (output && !p.i) printf("A=%02X X=%02X Y=%02X PC=%04X %c%c%c%c%c%c\n",a,x,y,pc,(p.n)?'N':' ',(p.v)?'V':' ',(p.d)?'D':' ',(p.i)?'I':' ',(p.z)?'Z':' ',(p.c)?'C':' ');
//                        if (ins==272000) output=1;
                        ins++;
                        if (interrupt && !p.i)
                        {
                                push(pc>>8);
                                push(pc&0xFF);
                                temp=0x20;
                                if (p.c) temp|=1; if (p.z) temp|=2;
                                if (p.d) temp|=8; if (p.v) temp|=0x40;
                                if (p.n) temp|=0x80;
                                push(temp);
                                pc=readmem(0xFFFE)|(readmem(0xFFFF)<<8);
                                p.i=1;
                                polltime(7);
//                                printf("Interrupt line %i %02X %02X %02X %02X\n",lines,sysvia.ifr&sysvia.ier,uservia.ifr&uservia.ier,uservia.ier,uservia.ifr);
                        }
                }
                oldnmi=nmi;
                if (discint)
                {
                        discint-=64;
                        if (discint<=0)
                        {
                                discint=0;
                                if (model==2) poll1770();
                                else          poll8271();
                        }
                }
                if (nmi && !oldnmi && !nmilock)
                {
                        push(pc>>8);
                        push(pc&0xFF);
                        temp=0x20;
                        if (p.c) temp|=1; if (p.z) temp|=2;
                        if (p.i) temp|=4; if (p.d) temp|=8;
                        if (p.v) temp|=0x40; if (p.n) temp|=0x80;
                        push(temp);
                        pc=readmem(0xFFFA)|(readmem(0xFFFB)<<8);
                        p.i=1;
                        polltime(7);
                        nmi=0;
                        nmilock=1;
//                        printf("NMI at line %i\n",lines);
                }
                logvols(lines);
                lines--;
/*                switch (lines)
                {
                        case 0: case 10: case 20: case 30: case 40:
                        case 50: case 60: case 70: case 80: case 90:
                        case 100: case 110: case 120: case 130: case 140:
                        case 150: case 160: case 170: case 180: case 190:
                        case 200: case 210: case 220: case 230: case 240:
                        case 250: case 260: case 270: case 280: case 290:
                        case 300:
                        pollacia();
                        break;
                }*/
                if (!(lines&0x1F)) pollacia();
                if (!(lines&0x3F) && adcconvert && !motor) polladc();
                if (lines!=-1) drawline(lines);
//                else           printf("line -2 - skipping\n");
        }
}