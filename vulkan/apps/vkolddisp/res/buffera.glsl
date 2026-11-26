float text(vec2 uv) {
    float col = 0.0; // Used in character macros
    
    /* Credits */
    TEXT_MODE = NORMAL;
    print_pos = charPos(LASTCOL-18,LASTLINE-1);
    
    _I _n _s _p _i _r _e _d _ _b _y _ GLOW_ON _F _l _y _g _u _y GLOW_OFF
    
    TEXT_MODE = NORMAL;
    print_pos = charPos(LASTCOL-18,LASTLINE);
    
    _E _x _t _e _n _d _e _d _ _b _y _ GLOW_ON _J _o _o _s _t _A _B GLOW_OFF
    
    /* Modes */
    INDENT = 0;
        
    TEXT_MODE = NORMAL;
    setCursor(0,0);
    
    _M _o _d _e _s _col _
    
    INDENT = 7;
   
    _N _o _r _m _a _l
    
    CRLF
    
    UNDERLINE_ON _U _n _d _e _r _l _i _n _e UNDERLINE_OFF

    CRLF
    
    BLINK_ON _B _l _i _n _k BLINK_OFF
    
    CRLF
    
    INVERT_ON _I _n _v _e _r _t INVERT_OFF
    
    CRLF
    
    BLINVERT_ON _B _l _i _n _v _e _r _t BLINVERT_OFF
    
    CRLF
    
    GLOW_ON _G _l _o _w GLOW_OFF
    
    /* Title */
    INDENT = 0;
    setCursor((LASTCOL/2)-8,LASTLINE/2);
    
    GLOW_ON UNDERLINE_ON _9 _6 _ _b _i _t _ _8 _x _1 _2 _ _F _o _n _t GLOW_OFF UNDERLINE_OFF
    
    /* Demo text */
    setCursor((LASTCOL/2)-22,(LASTLINE/2)+2);
    
    MODE_NORMAL
    _T _h _e _ _q _u _i _c _k _ _b _r _o _w _n _ _f _o _x _
    _j _u _m _p _s _  _o _v _e _r _  _t _h _e _  _l _a _z _y _  _d _o _g _per
    
    /* Clock */
    float hour = floor(iDate.w/60.0/60.0);
    float minute = floor(mod(iDate.w/60.0,60.0));
    float second = floor(mod(iDate.w,60.0));
    
    print_pos = charPos(LASTCOL-18,0);
    INT(iDate.z,2) _lsl INT(iDate.y+1.0,2) _lsl INT(iDate.x,4) _
    INT(hour,2) _col INT(minute,2) _col INT(second,2)
    
    return col;
    
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 uv = initFont(iResolution, fragCoord, iTime);
    
    // Get state of current pixel
	float pixel = text(floor(uv));
    
    // Create the character pixels
    vec3 col = vec3(1.0) * mix(vec3(0.2),vec3(0,1,0),pixel);
    
	fragColor = vec4(vec3(col), 1.0);
}