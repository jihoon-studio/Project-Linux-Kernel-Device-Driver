#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define DEVICE "/dev/my_proj"
#define I2C_ADDR 0x3C

uint8_t bcd2dec(uint8_t v){ return (v>>4)*10 + (v&0x0F); }
uint8_t dec2bcd(uint8_t v){ return ((v/10)<<4) | (v%10); }

/* OLED 폰트 */
static const unsigned char font[][5] = {
['0']={0x3E,0x51,0x49,0x45,0x3E}, ['1']={0x00,0x42,0x7F,0x40,0x00},
['2']={0x42,0x61,0x51,0x49,0x46}, ['3']={0x21,0x41,0x45,0x4B,0x31},
['4']={0x18,0x14,0x12,0x7F,0x10}, ['5']={0x27,0x45,0x45,0x45,0x39},
['6']={0x3C,0x4A,0x49,0x49,0x30}, ['7']={0x01,0x71,0x09,0x05,0x03},
['8']={0x36,0x49,0x49,0x49,0x36}, ['9']={0x06,0x49,0x49,0x29,0x1E},
[':']={0x00,0x36,0x36,0x00,0x00}, ['/']={0x20,0x10,0x08,0x04,0x02},
[' ']={0x00,0x00,0x00,0x00,0x00}, ['T']={0x01,0x01,0x7F,0x01,0x01},
['H']={0x7F,0x08,0x08,0x08,0x7F}, ['C']={0x3E,0x41,0x41,0x41,0x22},
['%']={0x23,0x13,0x08,0x64,0x62}
};

void oled_cmd(int fd,uint8_t c){ uint8_t b[2]={0x00,c}; write(fd,b,2); }
void oled_dat(int fd,uint8_t d){ uint8_t b[2]={0x40,d}; write(fd,b,2); }

void oled_init(int fd){
    oled_cmd(fd,0xAE); oled_cmd(fd,0xA1); oled_cmd(fd,0xC8);
    oled_cmd(fd,0x8D); oled_cmd(fd,0x14); oled_cmd(fd,0xAF);
}

void oled_clear(int fd){
    for(int p=0; p<8; p++){
        oled_cmd(fd,0xB0+p); oled_cmd(fd,0x00); oled_cmd(fd,0x10);
        for(int i=0;i<128;i++) oled_dat(fd,0);
    }
}

void oled_puts(int fd,int page,int col,const char*s){
    while(*s){
        oled_cmd(fd,0xB0+page); oled_cmd(fd,col&0x0F); oled_cmd(fd,0x10|(col>>4));
        for(int i=0;i<5;i++) oled_dat(fd,font[(int)*s][i]);
        oled_dat(fd,0); col+=6; s++;
    }
}

void oled_put_big(int fd,int page,int col,const char*s){
    while(*s){
        for(int y=0;y<2;y++){
            oled_cmd(fd,0xB0+page+y); oled_cmd(fd,col&0x0F); oled_cmd(fd,0x10|(col>>4));
            for(int i=0;i<5;i++){
                uint8_t b=font[(int)*s][i];
                uint8_t o=(y==0)?
                    ((b&0x01)?0x03:0)|((b&0x02)?0x0C:0)|((b&0x04)?0x30:0)|((b&0x08)?0xC0:0):
                    ((b&0x10)?0x03:0)|((b&0x20)?0x0C:0)|((b&0x40)?0x30:0)|((b&0x80)?0xC0:0);
                oled_dat(fd,o); oled_dat(fd,o);
            }
        }
        col+=12; s++;
    }
}

int main(){
    int fd=open(DEVICE,O_RDWR);
    int i2c=open("/dev/i2c-1",O_RDWR);
    ioctl(i2c,I2C_SLAVE,I2C_ADDR);

    oled_init(i2c);
    oled_clear(i2c);

    char buf[128], disp[32];
    int yr, mo, dy, h, m, s, t, hu, enc, mode;
    int adj_h, adj_m, adj_s, last_enc=0, blink=0;

    while(1){
        lseek(fd,0,SEEK_SET);
        int r=read(fd,buf,sizeof(buf)-1);
        if(r<=0) continue;
        buf[r]=0;

        int ry, rmo, rdy, rhh, rmm, rss, renc, rmode, rt, rhu;
        sscanf(buf,"%x,%x,%x,%x,%x,%x,%d,%d,%d,%d",
               &ry, &rmo, &rdy, &rhh, &rmm, &rss, &renc, &rmode, &rt, &rhu);

        int diff = renc - last_enc; last_enc = renc;

        if(rmode == 0){
            yr = bcd2dec(ry); mo = bcd2dec(rmo); dy = bcd2dec(rdy);
            h = bcd2dec(rhh); m = bcd2dec(rmm); s = bcd2dec(rss);
            adj_h = h; adj_m = m; adj_s = s;
        } else {
            if(diff != 0){
                if(rmode == 1) adj_h = (adj_h + diff + 24) % 24;
                if(rmode == 2) adj_m = (adj_m + diff + 60) % 60;
                if(rmode == 3) adj_s = (adj_s + diff + 60) % 60;
                sprintf(buf, "%02X,%02X,%02X,%02X,%02X,%02X", dec2bcd(yr%100), dec2bcd(mo), dec2bcd(dy), dec2bcd(adj_h), dec2bcd(adj_m), dec2bcd(adj_s));
                write(fd, buf, strlen(buf));
                usleep(10000);
            }
            h = adj_h; m = adj_m; s = adj_s;
        }
        t = rt; hu = rhu; blink = !blink;

        sprintf(disp,"20%02d/%02d/%02d      ", yr, mo, dy); oled_puts(i2c,0,0,disp);
        
        char hh_s[3], mm_s[3], ss_s[3];
        sprintf(hh_s, "%02d", h); sprintf(mm_s, "%02d", m); sprintf(ss_s, "%02d", s);
        if(rmode != 0 && blink){
            if(rmode==1) strcpy(hh_s, "  ");
            else if(rmode==2) strcpy(mm_s, "  ");
            else if(rmode==3) strcpy(ss_s, "  ");
        }
        sprintf(disp,"%s:%s:%s", hh_s, mm_s, ss_s); oled_put_big(i2c,2,20,disp);
        sprintf(disp, "T:%2dC H:%2d%%       ", t, hu); oled_puts(i2c, 6, 0, disp);
        
        usleep(150000);
    }
}