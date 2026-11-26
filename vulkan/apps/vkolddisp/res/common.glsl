// Fork of "96-bit 8x12 Font" by Flyguy. https://shadertoy.com/view/Mt2GWD
// 2024-11-26 09:24:55

// The font is 8x12 pixels. So scaling down actually means multiplying that.
// E.g. a scale down of 2.0 means you get characters 16 pixels wide and 24 pixels high.
#define DOWN_SCALE 2.0

// The maximum number of digits to be shown when using the print_integer function 
#define MAX_INT_DIGITS 4

// Several font effects. Assign these values to TEXT_MODE to have effect on the 
// text that follows.
// These can be combined by simply adding them. 
// E.g. TEXT_MODE=UNDERLINE+GLOW results in glowing underlined text.

#define NORMAL    0x00 
#define INVERT    0x01 
#define UNDERLINE 0x02 
#define BLINVERT  0x04
#define BLINK     0x08
#define GLOW      0x10

// Some shortcuts for simple text modes. Looks better when used in the text
#define MODE_NORMAL    TEXT_MODE=NORMAL;
#define MODE_INVERT    TEXT_MODE=INVERT;
#define MODE_UNDERLINE TEXT_MODE=UNDERLINE;
#define MODE_BLINVERT  TEXT_MODE=BLINVERT;
#define MODE_BLINK     TEXT_MODE=BLINK;
#define MODE_GLOW      TEXT_MODE=GLOW;

// Adds or removes a textmode fromk the current mode
#define INVERT_ON TEXT_MODE+=INVERT;
#define INVERT_OFF TEXT_MODE-=INVERT;
#define UNDERLINE_ON TEXT_MODE+=UNDERLINE;
#define UNDERLINE_OFF TEXT_MODE-=UNDERLINE;
#define BLINVERT_ON TEXT_MODE+=BLINVERT;
#define BLINVERT_OFF TEXT_MODE-=BLINVERT;
#define BLINK_ON TEXT_MODE+=BLINK;
#define BLINK_OFF TEXT_MODE-=BLINK;
#define GLOW_ON TEXT_MODE+=GLOW;
#define GLOW_OFF TEXT_MODE-=GLOW;

// Speed of blinking. Result is 1.0/BLINKRATE seconds
#define BLINKRATE 2.0

// The acitve text mode
int TEXT_MODE = NORMAL;

// Indentation of following lines
// Next CR will return to this position
int INDENT = 0;

// Next character will be printed in this position
vec2 print_pos = vec2(0);

vec2 res = vec2(0);
float itime = 0.0;

// Size of the characters. Used in calculations so don't touch ;)
#define CHAR_SIZE vec2(8, 12) 

#define CR print_pos.x=float(INDENT)*CHAR_SIZE.x; // Carriage Return
#define LF print_pos.y-=CHAR_SIZE.y;              // Line feed
#define CRLF CR;LF;                               // Carriage Return/Line feed
#define LASTCOL (int(res.x/CHAR_SIZE.x)-1)        // Last visible column 
#define LASTLINE (int(res.y/CHAR_SIZE.y)-1)       // Last visible line

// Sets the cursor at the specified column/line position
#define setCursor(c,l) print_pos=charPos(c,l)     

/*
How the font is defined:

For example, the ampersand &

--------
-###----
##-##---
##-##---
-###----
#####-#-
##-####-
##--##--
##-###--
-###-##-
--------
--------

00000000
01110000
11011000
11011000
01110000
11111010
11011110
11001100
11011100
01110110
00000000
00000000

//Broken up into 4 8x3 (24 bit) chunks for each component of the vec4.
//Hexadecimal is being used to reduce clutter in the code but decimal still works.

00000000
01110000 -> 00000000 01110000 11011000 -> 0x0070D8
11011000

11011000
01110000 -> 11011000 01110000 11111010 -> 0xD870FA
11111010

11011110
11001100 -> 11011110 11001100 11011100 -> 0xDECCDC
11011100

01110110
00000000 -> 01110110 00000000 00000000 -> 0x760000
00000000

ch_amp ivec4(0x0070D8,0xD870FA,0xDECCDC,0x760000)
*/

// Automatically generated from the 8x12 font sheet here:
// http://www.massmind.org/techref/datafile/charset/extractor/charset_extractor.htm
// and then turned into ivecs
#define ch_spc ivec4(0x000000,0x000000,0x000000,0x000000)
#define ch_exc ivec4(0x003078,0x787830,0x300030,0x300000)
#define ch_quo ivec4(0x006666,0x662400,0x000000,0x000000)
#define ch_hsh ivec4(0x006C6C,0xFE6C6C,0x6CFE6C,0x6C0000)
#define ch_dol ivec4(0x30307C,0xC0C078,0x0C0CF8,0x303000)
#define ch_pct ivec4(0x000000,0xC4CC18,0x3060CC,0x8C0000)
#define ch_amp ivec4(0x0070D8,0xD870FA,0xDECCDC,0x760000)
#define ch_apo ivec4(0x003030,0x306000,0x000000,0x000000)
#define ch_lbr ivec4(0x000C18,0x306060,0x603018,0x0C0000)
#define ch_rbr ivec4(0x006030,0x180C0C,0x0C1830,0x600000)
#define ch_ast ivec4(0x000000,0x663CFF,0x3C6600,0x000000)
#define ch_crs ivec4(0x000000,0x18187E,0x181800,0x000000)
#define ch_com ivec4(0x000000,0x000000,0x000038,0x386000)
#define ch_dsh ivec4(0x000000,0x0000FE,0x000000,0x000000)
#define ch_per ivec4(0x000000,0x000000,0x000038,0x380000)
#define ch_lsl ivec4(0x000002,0x060C18,0x3060C0,0x800000)
//#define ch_0   ivec4(0x007CC6,0xD6D6D6,0xD6D6C6,0x7C0000) // Orininal with filling
#define ch_0  ivec4(0x007CC6,0xCEDEF6,0xE6C6C6,0x7C0000) // With diagonal line
//#define ch_0   ivec4(0x007CC6,0xC6C6C6,0xC6C6C6,0x7C0000) // Empty 0
#define ch_1   ivec4(0x001030,0xF03030,0x303030,0xFC0000)
#define ch_2   ivec4(0x0078CC,0xCC0C18,0x3060CC,0xFC0000)
#define ch_3   ivec4(0x0078CC,0x0C0C38,0x0C0CCC,0x780000)
#define ch_4   ivec4(0x000C1C,0x3C6CCC,0xFE0C0C,0x1E0000)
#define ch_5   ivec4(0x00FCC0,0xC0C0F8,0x0C0CCC,0x780000)
#define ch_6   ivec4(0x003860,0xC0C0F8,0xCCCCCC,0x780000)
#define ch_7   ivec4(0x00FEC6,0xC6060C,0x183030,0x300000)
#define ch_8   ivec4(0x0078CC,0xCCEC78,0xDCCCCC,0x780000)
#define ch_9   ivec4(0x0078CC,0xCCCC7C,0x181830,0x700000)
#define ch_col ivec4(0x000000,0x383800,0x003838,0x000000)
#define ch_scl ivec4(0x000000,0x383800,0x003838,0x183000)
#define ch_les ivec4(0x000C18,0x3060C0,0x603018,0x0C0000)
#define ch_equ ivec4(0x000000,0x007E00,0x7E0000,0x000000)
#define ch_grt ivec4(0x006030,0x180C06,0x0C1830,0x600000)
#define ch_que ivec4(0x0078CC,0x0C1830,0x300030,0x300000)
#define ch_ats ivec4(0x007CC6,0xC6DEDE,0xDEC0C0,0x7C0000)
#define ch_A   ivec4(0x003078,0xCCCCCC,0xFCCCCC,0xCC0000)
#define ch_B   ivec4(0x00FC66,0x66667C,0x666666,0xFC0000)
#define ch_C   ivec4(0x003C66,0xC6C0C0,0xC0C666,0x3C0000)
#define ch_D   ivec4(0x00F86C,0x666666,0x66666C,0xF80000)
#define ch_E   ivec4(0x00FE62,0x60647C,0x646062,0xFE0000)
#define ch_F   ivec4(0x00FE66,0x62647C,0x646060,0xF00000)
#define ch_G   ivec4(0x003C66,0xC6C0C0,0xCEC666,0x3E0000)
#define ch_H   ivec4(0x00CCCC,0xCCCCFC,0xCCCCCC,0xCC0000)
#define ch_I   ivec4(0x007830,0x303030,0x303030,0x780000)
#define ch_J   ivec4(0x001E0C,0x0C0C0C,0xCCCCCC,0x780000)
#define ch_K   ivec4(0x00E666,0x6C6C78,0x6C6C66,0xE60000)
#define ch_L   ivec4(0x00F060,0x606060,0x626666,0xFE0000)
#define ch_M   ivec4(0x00C6EE,0xFEFED6,0xC6C6C6,0xC60000)
#define ch_N   ivec4(0x00C6C6,0xE6F6FE,0xDECEC6,0xC60000)
#define ch_O   ivec4(0x00386C,0xC6C6C6,0xC6C66C,0x380000)
#define ch_P   ivec4(0x00FC66,0x66667C,0x606060,0xF00000)
#define ch_Q   ivec4(0x00386C,0xC6C6C6,0xCEDE7C,0x0C1E00)
#define ch_R   ivec4(0x00FC66,0x66667C,0x6C6666,0xE60000)
#define ch_S   ivec4(0x0078CC,0xCCC070,0x18CCCC,0x780000)
#define ch_T   ivec4(0x00FCB4,0x303030,0x303030,0x780000)
#define ch_U   ivec4(0x00CCCC,0xCCCCCC,0xCCCCCC,0x780000)
#define ch_V   ivec4(0x00CCCC,0xCCCCCC,0xCCCC78,0x300000)
#define ch_W   ivec4(0x00C6C6,0xC6C6D6,0xD66C6C,0x6C0000)
#define ch_X   ivec4(0x00CCCC,0xCC7830,0x78CCCC,0xCC0000)
#define ch_Y   ivec4(0x00CCCC,0xCCCC78,0x303030,0x780000)
#define ch_Z   ivec4(0x00FECE,0x981830,0x6062C6,0xFE0000)
#define ch_lsb ivec4(0x003C30,0x303030,0x303030,0x3C0000)
#define ch_rsl ivec4(0x000080,0xC06030,0x180C06,0x020000)
#define ch_rsb ivec4(0x003C0C,0x0C0C0C,0x0C0C0C,0x3C0000)
#define ch_pow ivec4(0x10386C,0xC60000,0x000000,0x000000)
#define ch_usc ivec4(0x000000,0x000000,0x000000,0x00FF00)
#define ch_a   ivec4(0x000000,0x00780C,0x7CCCCC,0x760000)
#define ch_b   ivec4(0x00E060,0x607C66,0x666666,0xDC0000)
#define ch_c   ivec4(0x000000,0x0078CC,0xC0C0CC,0x780000)
#define ch_d   ivec4(0x001C0C,0x0C7CCC,0xCCCCCC,0x760000)
#define ch_e   ivec4(0x000000,0x0078CC,0xFCC0CC,0x780000)
#define ch_f   ivec4(0x00386C,0x6060F8,0x606060,0xF00000)
#define ch_g   ivec4(0x000000,0x0076CC,0xCCCC7C,0x0CCC78)
#define ch_h   ivec4(0x00E060,0x606C76,0x666666,0xE60000)
#define ch_i   ivec4(0x001818,0x007818,0x181818,0x7E0000)
#define ch_j   ivec4(0x000C0C,0x003C0C,0x0C0C0C,0xCCCC78)
#define ch_k   ivec4(0x00E060,0x60666C,0x786C66,0xE60000)
#define ch_l   ivec4(0x007818,0x181818,0x181818,0x7E0000)
#define ch_m   ivec4(0x000000,0x00FCD6,0xD6D6D6,0xC60000)
#define ch_n   ivec4(0x000000,0x00F8CC,0xCCCCCC,0xCC0000)
#define ch_o   ivec4(0x000000,0x0078CC,0xCCCCCC,0x780000)
#define ch_p   ivec4(0x000000,0x00DC66,0x666666,0x7C60F0)
#define ch_q   ivec4(0x000000,0x0076CC,0xCCCCCC,0x7C0C1E)
#define ch_r   ivec4(0x000000,0x00EC6E,0x766060,0xF00000)
#define ch_s   ivec4(0x000000,0x0078CC,0x6018CC,0x780000)
#define ch_t   ivec4(0x000020,0x60FC60,0x60606C,0x380000)
#define ch_u   ivec4(0x000000,0x00CCCC,0xCCCCCC,0x760000)
#define ch_v   ivec4(0x000000,0x00CCCC,0xCCCC78,0x300000)
#define ch_w   ivec4(0x000000,0x00C6C6,0xD6D66C,0x6C0000)
#define ch_x   ivec4(0x000000,0x00C66C,0x38386C,0xC60000)
#define ch_y   ivec4(0x000000,0x006666,0x66663C,0x0C18F0)
#define ch_z   ivec4(0x000000,0x00FC8C,0x1860C4,0xFC0000)
#define ch_lpa ivec4(0x001C30,0x3060C0,0x603030,0x1C0000)
#define ch_bar ivec4(0x001818,0x181800,0x181818,0x180000)
#define ch_rpa ivec4(0x00E030,0x30180C,0x183030,0xE00000)
#define ch_tid ivec4(0x0073DA,0xCE0000,0x000000,0x000000)
#define ch_lar ivec4(0x000000,0x10386C,0xC6C6FE,0x000000)


// _Some shortcuts to make life easier. 
// To write 'My string' just do: _M _y _  _s _t _r _i _n _g
// Just make sure that the variables 'col' and 'uv' are in scope.
#define _spc col+=char(ch_spc,uv);
#define _ _spc // Just because I'm lazy ;)
#define _exc col+=char(ch_exc,uv);
#define _quo col+=char(ch_quo,uv);
#define _hsh col+=char(ch_hsh,uv);
#define _dol col+=char(ch_dol,uv);
#define _pct col+=char(ch_pct,uv);
#define _amp col+=char(ch_amp,uv);
#define _apo col+=char(ch_apo,uv);
#define _lbr col+=char(ch_lbr,uv);
#define _rbr col+=char(ch_rbr,uv);
#define _ast col+=char(ch_ast,uv);
#define _crs col+=char(ch_crs,uv);
#define _com col+=char(ch_com,uv);
#define _dsh col+=char(ch_dsh,uv);
#define _per col+=char(ch_per,uv);
#define _lsl col+=char(ch_lsl,uv);
#define _0   col+=char(ch_0  ,uv);
#define _1   col+=char(ch_1  ,uv);
#define _2   col+=char(ch_2  ,uv);
#define _3   col+=char(ch_3  ,uv);
#define _4   col+=char(ch_4  ,uv);
#define _5   col+=char(ch_5  ,uv);
#define _6   col+=char(ch_6  ,uv);
#define _7   col+=char(ch_7  ,uv);
#define _8   col+=char(ch_8  ,uv);
#define _9   col+=char(ch_9  ,uv);
#define _col col+=char(ch_col,uv);
#define _scl col+=char(ch_scl,uv);
#define _les col+=char(ch_les,uv);
#define _equ col+=char(ch_equ,uv);
#define _grt col+=char(ch_grt,uv);
#define _que col+=char(ch_que,uv);
#define _ats col+=char(ch_ats,uv);
#define _A   col+=char(ch_A  ,uv);
#define _B   col+=char(ch_B  ,uv);
#define _C   col+=char(ch_C  ,uv);
#define _D   col+=char(ch_D  ,uv);
#define _E   col+=char(ch_E  ,uv);
#define _F   col+=char(ch_F  ,uv);
#define _G   col+=char(ch_G  ,uv);
#define _H   col+=char(ch_H  ,uv);
#define _I   col+=char(ch_I  ,uv);
#define _J   col+=char(ch_J  ,uv);
#define _K   col+=char(ch_K  ,uv);
#define _L   col+=char(ch_L  ,uv);
#define _M   col+=char(ch_M  ,uv);
#define _N   col+=char(ch_N  ,uv);
#define _O   col+=char(ch_O  ,uv);
#define _P   col+=char(ch_P  ,uv);
#define _Q   col+=char(ch_Q  ,uv);
#define _R   col+=char(ch_R  ,uv);
#define _S   col+=char(ch_S  ,uv);
#define _T   col+=char(ch_T  ,uv);
#define _U   col+=char(ch_U  ,uv);
#define _V   col+=char(ch_V  ,uv);
#define _W   col+=char(ch_W  ,uv);
#define _X   col+=char(ch_X  ,uv);
#define _Y   col+=char(ch_Y  ,uv);
#define _Z   col+=char(ch_Z  ,uv);
#define _lsb col+=char(ch_lsb,uv);
#define _rel col+=char(ch_rsl,uv);
#define _rsb col+=char(ch_rsb,uv);
#define _pow col+=char(ch_pow,uv);
#define _usc col+=char(ch_usc,uv);
#define _a   col+=char(ch_a  ,uv);
#define _b   col+=char(ch_b  ,uv);
#define _c   col+=char(ch_c  ,uv);
#define _d   col+=char(ch_d  ,uv);
#define _e   col+=char(ch_e  ,uv);
#define _f   col+=char(ch_f  ,uv);
#define _g   col+=char(ch_g  ,uv);
#define _h   col+=char(ch_h  ,uv);
#define _i   col+=char(ch_i  ,uv);
#define _j   col+=char(ch_j  ,uv);
#define _k   col+=char(ch_k  ,uv);
#define _l   col+=char(ch_l  ,uv);
#define _m   col+=char(ch_m  ,uv);
#define _n   col+=char(ch_n  ,uv);
#define _o   col+=char(ch_o  ,uv);
#define _p   col+=char(ch_p  ,uv);
#define _q   col+=char(ch_q  ,uv);
#define _r   col+=char(ch_r  ,uv);
#define _s   col+=char(ch_s  ,uv);
#define _t   col+=char(ch_t  ,uv);
#define _u   col+=char(ch_u  ,uv);
#define _v   col+=char(ch_v  ,uv);
#define _w   col+=char(ch_w  ,uv);
#define _x   col+=char(ch_x  ,uv);
#define _y   col+=char(ch_y  ,uv);
#define _z   col+=char(ch_z  ,uv);
#define _lpa col+=char(ch_lpa,uv);
#define _bar col+=char(ch_bar,uv);
#define _rpa col+=char(ch_rpa,uv);
#define _tid col+=char(ch_tid,uv);
#define _lar col+=char(ch_lar,uv);

// Shortcuts to print numbers
#define INT(i,d) col+=print_integer(i,d,uv); 
#define NUM(n,d) col+=print_number(n,d,uv); 

//Extracts bit b from the given number.
//Shifts bits right (num / 2^bit) then ANDs the result with 1 (mod(result,2.0)).
int extract_bit(int n, int b) {
    return (n >> clamp(b,-1,24)) & 1;
}

//Returns the pixel at uv in the given bit-packed sprite.
float sprite(ivec4 spr, vec2 size, vec2 uv) {
    uv = floor(uv);
    
    //Calculate the bit to extract (x + y * width) (flipped on x-axis)
    int bit = int((size.x-uv.x-1.0) + uv.y * size.x);
    
    //Clipping bound to remove garbage outside the sprite's boundaries.
    bool bounds = all(greaterThanEqual(uv,vec2(0))) && all(lessThan(uv,size));
    
    int pixels = 0;

    if (bounds) {
        pixels += extract_bit(spr.x, bit - 72);
        pixels += extract_bit(spr.y, bit - 48);
        pixels += extract_bit(spr.z, bit - 24);
        pixels += extract_bit(spr.w, bit - 00);
    }
    
    return float(pixels);

}

vec2 charPos(int col, int line) {
    vec2 cp = vec2(0.0);
    cp.x = float(col + INDENT)*CHAR_SIZE.x;
    cp.y = res.y - (float(line+1)*CHAR_SIZE.y);
    return cp;
}

//Prints a character and moves the print position forward by 1 character width.
float char(ivec4 ch, vec2 uv) {
    bool inv = (TEXT_MODE&INVERT)>0;

    if ((TEXT_MODE & UNDERLINE) > 0) {
        ch.w = (ch.w/256)*256 + 255;  
    }
    
    if ((TEXT_MODE & BLINVERT)>0) {
        if (mod(floor(itime*BLINKRATE),2.)==0.0) {
            inv = !inv;
        }
    }
    
    if (inv) {
        ch = 0xFFFFFF-ch;
    }
    
    float px = sprite(ch, CHAR_SIZE, uv - print_pos);
    
    if ((TEXT_MODE & GLOW)>0) {
        px *= 2.0;
    }
    
    if ((TEXT_MODE & BLINK)>0) {
        if (mod(floor(itime*BLINKRATE),2.)==0.0) {
            px = 0.0;
        }
    }
    
    print_pos.x += CHAR_SIZE.x;
    return px;
}


//Returns the digit sprite for the given number.
ivec4 get_digit(int d) {
    if(d == 0) return ch_0;
    if(d == 1) return ch_1;
    if(d == 2) return ch_2;
    if(d == 3) return ch_3;
    if(d == 4) return ch_4;
    if(d == 5) return ch_5;
    if(d == 6) return ch_6;
    if(d == 7) return ch_7;
    if(d == 8) return ch_8;
    if(d == 9) return ch_9;
    return ch_spc;
}

//Prints out the given float number starting at pos with up to 
// the specified number of decimals.
float print_number(float number, int decimals, vec2 uv) {
    float result = 0.0;
    
	for(int i = 3;i >= -decimals;i--) {
        int digit = int(mod( number / pow(10.0, float(i)) , 10.0));
        
        if(i == -1) {
            //Add a decimal point.
            result += char(ch_per,uv);
        }
        
        if(abs(number) > pow(10.0, float(i)) || i == 0) {
            //Clip off leading zeros.
        	result += char(get_digit(digit),uv);
        }
    }   
    return result;
}

// Prints an integer number, up to MAX_INT_DIGITS digits.
// The zeros argument determines the maximum leading zeros
float print_integer(float number, int zeros, vec2 uv) {
    float result = 0.0;
    
	for(int i = MAX_INT_DIGITS; i >= 0; i--) {
        int digit = int(mod( number / pow(10.0, float(i)) , 10.0));

        if(abs(number) > pow(10.0, float(i)) || zeros > i  || i == 0) {
            //Clip off leading zeros.
        	result += char(get_digit(digit),uv);
        }
    }   
    return result;
}

// Overloaded print_integer to accept integers
float print_integer(int number, int zeros, vec2 uv) {
    return print_integer(float(number),zeros,uv) ;
}

// Initializes the res and itime variables, and returnes a scaled uv vector
vec2 initFont(vec3 resolution, vec2 fragCoord, float time) {
    res = resolution.xy / DOWN_SCALE;
    itime = time;
    vec2 uv = fragCoord.xy / DOWN_SCALE;
    return uv;
}