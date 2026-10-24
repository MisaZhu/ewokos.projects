// "hello" v2 — recreation of the macOS "Macintosh" screensaver, rebuilt from the
// original WallpaperMacintoshExtension assets: exact Geneva 9 glyphs, exact screen
// bitmaps (MacWriteEmpty / MacPaintHello), exact cursors (CURS resources) and the
// exact brush path with timings from MacPaintHello.program.

// ---------- helpers ----------
float ss(float a, float b, float x){ return smoothstep(a, b, x); }
float kf(float t, float a, float b){ return clamp((t-a)/(b-a), 0., 1.); }
float easeOut(float u){ return 1. - (1.-u)*(1.-u); }
float easeInOut(float u){ return u*u*(3. - 2.*u); }
vec3 hsl(float h, float s, float l){
  vec3 rgb = clamp(abs(mod(h*6. + vec3(0,4,2), 6.) - 3.) - 1., 0., 1.);
  return l + s*(rgb - 0.5)*(1. - abs(2.*l - 1.));
}
bool bitAt(int row, int x){ return ((row >> x) & 1) == 1; }
float sdSeg(vec2 p, vec2 a, vec2 b){
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa,ba)/dot(ba,ba), 0., 1.);
  return length(pa - ba*h);
}
int checker(ivec2 p){ return 1 - ((p.x + p.y) & 1); }

int scr1Bit(ivec2 p){        // MacWriteEmpty, x[0,128) y[0,96)
  return (SCR1[p.y*4 + (p.x >> 5)] >> (p.x & 31)) & 1;
}
int scr2Bit(ivec2 p){        // MacPaintHello, x[0,128) y[20,128)
  return (SCR2[(p.y - 20)*4 + (p.x >> 5)] >> (p.x & 31)) & 1;
}
int fontBit(int g, ivec2 q){ // Geneva 9, 12 rows per glyph
  if (q.x < 0 || q.x > 9 || q.y < 0 || q.y > 11) return 0;
  return bitAt(FONT_ROWS[g*12 + q.y], q.x) ? 1 : 0;
}

// typing: three keyboard bursts with 1 s pauses (from MacWriteTextInput.program)
float charTime(int i){
  float q = 0.14, t = 7.0 + float(i)*q + 0.03*sin(float(i)*2.7);
  if (i >= 26) t += 1.0;         // pause before "The misfits."
  if (i >= 39) t += 1.0;         // pause before "The rebels."
  return t;
}

// ---------- scene 1: MacWrite (full Mac screen, px coords) ----------
int SEL_ROW; float SEL_X;        // selection cursor cell, set per-frame in scene1
int screen1(vec2 c, float t){
  ivec2 p = ivec2(floor(c));
  if (p.x < 0 || p.x > 511 || p.y < 0 || p.y > 341) return 0;
  int fg = 0;
  if (p.x < 128 && p.y < 96) fg = scr1Bit(p);
  else {
    if (p.x <= 1) fg = checker(p);                     // desktop sliver
    if (p.x == 2) fg = 1;                              // window border
    if ((p.y == 88 || p.y == 89) && p.x >= 3) fg = 1;  // rule above the text
  }

  // typed text at 13,90 (Geneva 9, line height 12) + caret + selection.
  // a fragment only walks its own line: 12 px tall, glyphs start at LINE0[line]
  int line = (p.y - 90)/12;
  if (p.y >= 90 && line < LINE0.length() && p.x >= 3 && p.x < 128){
    float penX = 13., baseY = float(90 + line*12);
    float caretX = -99.;
    float selEnd = 13.;
    int i1 = line + 1 < LINE0.length() ? LINE0[line+1] - 1 : TXT.length();
    for (int i = LINE0[line]; i < i1; i++){
      int g = TXT[i];
      bool vis = t > charTime(i);
      float adv = float(ADVANCE[g]);
      if (vis && fontBit(g, p - ivec2(int(penX), int(baseY))) == 1) fg = 1;
      if (vis) caretX = penX + adv;
      penX += adv;
      // selection runs to the last glyph the I-beam has passed on this line
      if (line == 0 && (penX <= SEL_X || SEL_ROW > 0)) selEnd = penX;
      if (line == 1 && SEL_ROW > 0 && penX <= SEL_X) selEnd = penX;
    }
    // caret blinks at the end of the typed text
    if (t > 2.5 && t < 17.4 && fract(t*1.4) < 0.6 && caretX > 0. &&
        (line + 1 == LINE0.length() || t < charTime(i1)) &&
        p.x == int(caretX) && p.y >= int(baseY) && p.y < int(baseY) + 11) fg = 1;
    // selection invert, per character
    if (t > 17.5 && t < 26.6 && line <= 1 &&
        float(p.x) >= 13. && float(p.x) < selEnd) fg = 1 - fg;
  }
  return fg;
}

// ---------- scene 2: MacPaint (full Mac screen, px coords) ----------
int screen2(vec2 c, float t){
  ivec2 p = ivec2(floor(c));
  if (p.x < 0 || p.x > 511 || p.y < 0 || p.y > 341) return 0;
  int fg = 0;
  if (p.x < 128 && p.y >= 20 && p.y < 128) fg = scr2Bit(p);
  else if (p.y >= 20){
    // right of the bitmap: Hello window (top y29, right edge 497) on the desktop
    if (p.x > 497){ fg = (p.y <= 46) ? checker(p) : 0; }
    else if (p.y >= 20 && p.y <= 28) fg = checker(p);
    else if (p.y == 29 || p.y == 47) fg = 1;
    else if (p.y >= 33 && p.y <= 43 && ((p.y - 33) & 1) == 0 &&
             (p.x < 267 || p.x > 310)) fg = 1;             // title stripes
    if (p.y >= 33 && p.y <= 42 && p.x >= 273 && p.x <= 303) // "Hello"
      fg |= (HELLOW[p.y - 33] >> (p.x - 273)) & 1;
  }

  // selected tool inverts: pencil until the click, then brush (row 4: y95..115)
  float tc = t - 4.05;                                   // brush click moment
  int sx0 = tc < 0. ? 42 : 16, sx1 = tc < 0. ? 66 : 40;
  if (p.x >= sx0 && p.x <= sx1 && p.y >= 95 && p.y <= 115) fg = 1 - fg;

  // brush strokes (exact path from MacPaintHello.program), constant speed.
  // the path is split into groups of GS segments; a fragment only walks the
  // groups whose bounding box it falls into
  vec2 cc = floor(c) + 0.5;
  float drawU = kf(t, 5.4, 22.27);
  float visLen = drawU * PATH_LEN;
  vec2 pen = PTS[0];
  if (drawU > 0.){
    for (int g = 0; g < PGCUM.length(); g++){
      if (PGCUM[g] >= visLen) break;
      vec4 bb = PGBB[g];
      bool near = cc.x > bb.x - 2.5 && cc.x < bb.z + 2.5 && cc.y > bb.y - 2.5 && cc.y < bb.w + 2.5;
      bool last = g + 1 == PGCUM.length() || PGCUM[g+1] >= visLen;   // pen lives here
      if (!near && !last) continue;
      float cum = PGCUM[g];
      int i1 = min((g+1)*GS, PTS.length()-1);
      for (int i = g*GS; i < i1; i++){
        if (i == STROKE2 - 1){ pen = PTS[i+1]; continue; }           // pen lift
        vec2 a = PTS[i], b = PTS[i+1];
        float L = length(b - a);
        if (cum >= visLen) break;
        vec2 e = (cum + L <= visLen) ? b : mix(a, b, (visLen - cum)/L);
        if (near && sdSeg(cc, a, e) < 1.8) fg = 1;
        pen = e;
        cum += L;
      }
    }
  }
  // the final period: click at 214,91
  if (t > 22.9 && length(cc - vec2(214.5, 91.5)) < 1.8) fg = 1;

  // arrow cursor rides in and clicks the brush (scenario: to 38,112 then click)
  if (t > 2.0 && t < 4.3){
    float au = easeOut(kf(t, 2.2, 4.0));
    vec2 ap = mix(vec2(18, 162), vec2(38, 112), au);
    ap += vec2(16, -6)*sin(3.14159*au);
    ivec2 q = p - ivec2(ap) + ivec2(1, 1);               // hotspot 1,1
    if (q.x >= 0 && q.x < 16 && q.y >= 0 && q.y < 16 && bitAt(ARROW[q.y], q.x)) fg = 1 - fg;
  }
  // after the click the cursor is the brush nib
  if (t > 4.3 && t < 25.1){
    vec2 tip = pen;
    if (t > 22.27) tip = mix(pen, vec2(214.5, 91.5), easeInOut(kf(t, 22.3, 22.7)));
    if (t > 23.1)  tip = mix(vec2(214.5, 91.5), vec2(255, 128), easeOut(kf(t, 23.1, 25.1)));
    tip = mix(vec2(38, 112), tip, easeOut(ss(4.3, 5.4, t)));   // glides from the toolbox
    if (length(cc - tip) < 1.8) fg = 1;
  }
  return fg;
}

// ---------- dissolve fronts: scene1 sweeps down (ramp Down), scene2 by columns (ramp Right)
float cellScale(vec2 cell, float dIn, float dOut, vec2 prR, bool byX){
  float pr = byX ? cell.x : cell.y;
  float sc = 1.;
  if (dOut > 0.) sc *= ss(0., 22., pr - mix(prR.x, prR.y, dOut));
  if (dIn  < 1.) sc *= 1. - ss(0., 22., pr - mix(prR.x, prR.y, dIn));
  return sc;
}

// soft rounded tile; f in cell units, X stretched by the pixel aspect
float dotCov(vec2 f, float scale, float aa, float soft){
  if (scale <= 0.03) return 0.;
  float r = 0.10*scale;
  vec2 b = (vec2(0.5*ASPECT, 0.5) - 0.045)*scale - r;
  vec2 q = abs(f*vec2(ASPECT, 1.)) - b;
  float d = length(max(q, vec2(0))) + min(max(q.x, q.y), 0.) - r;
  return 1. - ss(0., aa + soft, d + (aa + soft)*0.5);
}

float hueAt(float t){
  const vec2 K[] = vec2[](vec2(0, 1.063), vec2(13.5, 1.04), vec2(28.5, 0.84), vec2(34.5, 0.65),
                          vec2(40.5, 0.50), vec2(46.5, 0.35), vec2(52.5, 0.16), vec2(57.01, 0.063));
  float h = 0.;
  for (int i = 0; i < 7; i++)
    if (t >= K[i].x && t <= K[i+1].x)
      h = mix(K[i].y, K[i+1].y, (t - K[i].x)/(K[i+1].x - K[i].x));
  return fract(h);
}

void mainImage(out vec4 O, in vec2 F){
  float t = mod(iTime, LOOP);
  vec2 R = iResolution.xy;
  vec2 uv = vec2(F.x - 0.5*R.x, 0.5*R.y - F.y)/R.y;   // y down

  int scene = t < 26.5 ? 0 : (t < 56.5 ? 1 : -1);
  float dIn = 1., dOut = 0.;
  float ts = t, t2 = t - 26.5;
  if (scene == 0){ dIn = kf(t, 0., 3.); dOut = kf(t, 23., 26.); }
  if (scene == 1){ dIn = kf(t2, 0., 3.); dOut = kf(t2, 27., 30.); }

  vec2 cam = vec2(0); float rot = 0.29, ZY = 95.8;
  if (scene == 0){
    float u = clamp(ts/27., 0., 1.);           // scenario: scroll 40,10 -> 40,113 easeout 27 s
    cam = vec2(40., mix(10., 113., easeOut(u)));
    cam += 1.2*vec2(sin(ts*0.23), cos(ts*0.19));
  } else if (scene == 1){
    float u = clamp(t2/30., 0., 1.);           // scenario: scroll 20,73 -> 220,73 linear
    cam = vec2(mix(16., 216., u), 75.5);
    ZY = 95.8;
  }
  float ZX = ZY/ASPECT;

  // selection drag state (scene 0): I-beam from approach + linear drag 13,91 -> 70,110
  float selU = easeInOut(kf(ts, 17.5, 20.5));
  vec2 selCur = mix(vec2(13, 91), vec2(70, 110), selU);
  SEL_ROW = selCur.y >= 102. ? 1 : 0;
  SEL_X = selCur.x;

  float hue = hueAt(t);
  float y01 = 1. - F.y/R.y;
  float l = mix(0.62, 0.47, y01) + 0.05*(1. - min(1., length((vec2(F.x/R.x, y01) - vec2(0.5, 0.12))*vec2(1.4, 1))));
  vec3 col = hsl(hue, 0.66, l);

  if (scene >= 0){
    float lean = rot + 0.24;
    mat2 M = inverse(mat2(cos(rot), sin(rot), -sin(lean), cos(lean)));
    vec2 uvp = uv / (1. + dot(uv, vec2(-0.08, 0.10)));
    vec2 c = cam + M*(uvp*vec2(ZX, ZY));
    float aa = max(ZY/R.y*1.4, 0.004);
    vec3 ink = hsl(hue, 0.60, 0.17);
    vec2 prR = scene == 0 ? vec2(-40., 165.) : vec2(-45., 290.);
    bool byX = scene == 1;

    // I-beam cursor on scene 0 is drawn in inverse video over the cells
    int fgMain = scene == 0 ? screen1(c, ts) : screen2(c, t2);
    if (scene == 0 && ts > 16.0 && ts < 26.5){
      float gi = easeOut(kf(ts, 16.0, 17.5));
      vec2 ib = mix(vec2(75, 135), vec2(13, 91), gi);
      ib += vec2(-8, 6)*sin(3.14159*gi);
      if (ts > 17.5) ib = selCur;
      ivec2 q = ivec2(floor(c)) - ivec2(ib) + ivec2(7, 4);   // hotspot 7,4
      if (q.x >= 0 && q.x < 16 && q.y >= 0 && q.y < 16 && bitAt(IBEAM[q.y], q.x)) fgMain = 1 - fgMain;
    }
    // shadow: sample the scene shifted up-left so the shadow falls down-right
    vec2 csh = c - vec2(0.30, 0.34);
    int fgSh = scene == 0 ? screen1(csh, ts) : screen2(csh, t2);
    if (scene == 0 && ts > 16.0 && ts < 26.5){
      float gi = easeOut(kf(ts, 16.0, 17.5));
      vec2 ib = mix(vec2(75, 135), vec2(13, 91), gi);
      ib += vec2(-8, 6)*sin(3.14159*gi);
      if (ts > 17.5) ib = selCur;
      ivec2 q = ivec2(floor(csh)) - ivec2(ib) + ivec2(7, 4);
      if (q.x >= 0 && q.x < 16 && q.y >= 0 && q.y < 16 && bitAt(IBEAM[q.y], q.x)) fgSh = 1 - fgSh;
    }
    if (fgSh == 1){
      float sh = dotCov(fract(csh) - 0.5, cellScale(floor(csh), dIn, dOut, prR, byX), aa, 0.40);
      col = mix(col, col*0.88, sh*0.55);
    }
    if (fgMain == 1){
      float cov = dotCov(fract(c) - 0.5, cellScale(floor(c), dIn, dOut, prR, byX), aa, 0.);
      col = mix(col, ink, cov);
    }
  }
  col *= 1. - 0.16*dot(uv, uv)*2.4;
  O = vec4(col, 1);
}
