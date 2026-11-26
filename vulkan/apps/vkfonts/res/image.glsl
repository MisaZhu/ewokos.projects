/*
    > See "Common" Tab for usage detail.
    
    This shader is an improved version of my previous text utility (see fork) that was designed
    to facilitate string manipulation in shaders. This version greatly improves performance.
    
    === What's new ? ===
    
    In short, I'm now using int[] arrays to declare strings. I only create 1 function per string
    instead of creating 1 function per character like the previous version did.
    The idea for this new approach came from @jiang on my recent code-golfing challenge (https://www.shadertoy.com/view/lXt3zf)
    
    1. One Texture Call Per Line
    
        The performance impact of the previous version scaled with the total number of characters in
        each string. For every character that was defined, there was one associated texture sample.
        
        In this new version, there is only 1 texture sample per string definition, greatly reducing 
        the performance impact. A 20-character string now contains 20x less texture samples.
        
    2. No "For" Loop
    
        The floating point debug of my previous algorithm was using 3 for-loops to print a number,
        and had precision issues.
        
        This is also greatly improved in this version as this shader doesn't rely on any loop whatsoever.
        Moreover, the algorithm now supports integer debugging (which wasn't possible in the previous
        version), and the precision is slightly improved.
        
    3. Fast Compile Time
    
        In the previous version, each character was calling a function to calculate
        its position in the alphabet texture in order to sample the texture at that position.
        This was done using well-crafted #defines for every character, but could lead to 
        excessive compile times.
        
        In this version, the #defines are much simpler as I'm using integer arrays instead
        of complex define trickery.
        
        Previously, "_A" was defined as: #define _A ); u.x -= CHAR_SPACING; d += _char(FONT_TEXTURE, u, 65
        
        Now, "_A" simply is "65," (a single element in an int[] array) and the compilation time is greatly improved.
        
        Moreover, I managed to keep each int[] array as a compile-time constant (const) in order to
        maximize performance.
        
    ===================================================================================================
*/

/* Basic string definition: makeStr(name) _A _B _C _end
 *
 * Will create a function:  float name(vec2 uv);
 */
makeStr (printBetter) _B _e _t _t _e _r __ _W _a _y __ _T _o __ _P _r _i _n _t __ _T _e _x _t __ _i _n __ _S _h _a _d _e _r _s _EX _end

/* String + integer debug : makeStrI(name) _A _num_ _B _C _endNum
 *
 * Will create a function:  float name(vec2 uv, int num);
 * The input variable will be displayed at the position of the "_num_" keyword.
 */
makeStrI(printFrame)  _D _e _b _u _g __ _I _n _t _e _g _e _r _s _COL __ _i _F _r _a _m _e __ _EQ __ _num_ __ _f _r _a _m _e _s _endNum

/* String + float debug :   makeStrF(name) _A _B _num_ _C _endNum
 *
 * Will create a function:  float name(vec2 uv, float num, int decimal_count);
 * The input variable will be displayed at the position of the "_num_" keyword.
 */
makeStrF(printTime)   _D _e _b _u _g __ _F _l _o _a _t _s _COL __ _s _i _n _LPR _i _T _i _m _e _RPR __ _EQ __ _num_ __ _a _s __ _f _3 _2 _endNum             

/* Quick int/float debug: debugInt & debugFloat
 *
 * Defining these two helpers allow to quickly debug int/float variables, 
 * without the need to create a full string definition each time with makeStr().
 *
 *     color += debugInt(uv, 42);
 *     color += debugFloat(uv, 3.14, 2);
 */
makeStrI(debugInt) _num_ _endNum
makeStrF(debugFloat) _num_ _endNum

// Other string definitions
makeStr (print1)      _1 _DOT __ _O _n _e __ _T _e _x _t _u _r _e __ _C _a _l _l __ _P _e _r __ _l _i _n _e _end
makeStr (print2)      _2 _DOT __ _N _o __ _DBQ _F _o _r _DBQ __ _L _o _o _p _end
makeStrI(print3)      _num_ _DOT __ _F _a _s _t __ _C _o _m _p _i _l _e __ _T _i _m _e _endNum
makeStr (printJS)     _ADD __ _J _a _v _a _s _c _r _i _p _t __ _S _t _r _i _n _g __ _G _e _n _e _r _a _t _o _r _end
makeStr (printUse)    _B _a _s _i _c __ _U _s _a _g _e _COL _end
makeStr (printCode1)  _m _a _k _e _S _t _r _LPR _m _y _UN _s _t _r _i _n _g _RPR __ _UN _H __ _UN _e __ _UN _l __ _UN _l __ _UN _o __ _UN _UN __ _UN _W __ _UN _o __ _UN _r __ _UN _l __ _UN _d __ _UN _e _n _d _end
makeStr (printCode2)  _f _r _a _g _C _o _l _o _r __ _ADD _EQ __ _m _y _UN _s _t _r _i _n _g _LPR _u _v _RPR _SEM _end

// Color definitions
#define RED     vec3( 1,.3,.4)
#define BLUE    vec3(.5, 1, 1)
#define YELLOW  vec3( 1, 1,.4)
#define ORANGE  (1.+cos(uv.y*12. + .7 + vec3(0,1,2)))
#define RAINBOW abs(cos(uv.x*4. - iTime*2. + vec3(5,6,1)))

void mainImage( out vec4 fragColor, vec2 fragCoord ) {
    vec2 uv = fragCoord/iResolution.y; // uv in [0, 1] range
    vec3 col = vec3(0);                // final color

    uv.y -= .9;                   // Start writing from top
    uv = uv * .8 - vec2(.02, .0); // Font size + padding left
    
    col += ORANGE  * printBetter(uv*.81); // Better Way To Print Text in Shaders!
    
    col += BLUE    * printFrame(uv + vec2(0, .1), iFrame);           // Debug Integers: iFrame
     
    col += BLUE    * printTime(uv+vec2(0,.18), sin(iTime)*1200., 3); // Debug Floats: sin(iTime)
    
    col += RED/.8  * print1(uv + vec2(-.2, .3));                     // 1. One texture Call Per Line
    
    col += RED/.5  * print2(uv + vec2(-.2, .37));                    // 2. No "For" Loop
    
    col += RED/.4  * print3(uv + vec2(-.2, .44), iFrame%100-50);     // 3. Fast Compile time
            
    col += RAINBOW * printJS(uv*1.5 + vec2(-1.1, .78));              // + Javascript String Generator
    
    col += YELLOW  * printUse(uv*1.35 + vec2(0, .75)); // Basic Usage:
    
    col += printCode1(uv*1.35 + vec2(-.1, .84));       // makeStr(my_string) _H _e _l _l _o __ _W _o _r _l _d _end
    
    col += printCode2(uv*1.35 + vec2(-.1, .92));       // fragColor += my_string(uv);
    
    // Use debugInt & debugFloat to directly display variables 
    // without the need to create additional string definitions:
    //
    //     col += debugInt(uv*.2 + vec2(0, .05), 42);    
    //     col += debugFloat(uv*.2 + vec2(0, .1), 3.1415, 4);
    
    fragColor = vec4(col, 1);
}