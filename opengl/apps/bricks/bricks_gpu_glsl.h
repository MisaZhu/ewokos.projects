/* GPU image pass derived from res/image.glsl (iq, CC BY-NC-SA 3.0).
 * Buffer A remains the faithful 14x14 CPU simulation. Its float state is
 * marshalled through uniforms so no RGBA32F texture or FBO extension is needed.
 * A negative brick hit time denotes a live brick; nonnegative values are the
 * destruction times written by the simulation. */
static const char bricks_gpu_fs_glsl[] = R"GLSL(
precision highp float;
precision highp int;
uniform vec3 iResolution;
uniform float iTime;
uniform float uBrickHits[156];
uniform vec4 uGame0;
uniform vec4 uGame1;

const float ballRadius = 0.035;
const float paddleSize = 0.30;
const float paddleWidth = 0.06;
const float paddlePosY = -0.90;
const float brickW = 2.0/13.0;
const float brickH = 1.0/15.0;
const vec2 shadowOffset = vec2(-0.03,0.03);

float udSegment(in vec2 p, in vec2 a, in vec2 b)
{
    vec2 pa = p-a, ba = b-a;
    float h = clamp(dot(pa,ba)/dot(ba,ba), 0.0, 1.0);
    return length(pa-ba*h);
}

float udHorizontalSegment(in vec2 p, in float xa, in float xb, in float y)
{
    vec2 pa = p-vec2(xa,y);
    float ba = xb-xa;
    pa.x -= ba*clamp(pa.x/ba, 0.0, 1.0);
    return length(pa);
}

float udRoundBox(in vec2 p, in vec2 c, in vec2 b, in float r)
{
    return length(max(abs(p-c)-b,0.0))-r;
}

float hash1(in float n)
{
    return fract(sin(n)*138.5453123);
}

int digitFont(in int n)
{
    if (n==0) return 0x75557;
    if (n==1) return 0x22222;
    if (n==2) return 0x74717;
    if (n==3) return 0x74747;
    if (n==4) return 0x11574;
    if (n==5) return 0x71747;
    if (n==6) return 0x71757;
    if (n==7) return 0x74444;
    if (n==8) return 0x75757;
    return 0x75747;
}

int PrintInt(in vec2 uv, in int value)
{
    const int maxDigits = 3;
    if (abs(uv.y-0.5)<0.5) {
        int iu = int(floor(uv.x));
        if (iu>=0 && iu<maxDigits) {
            int n = value;
            if (iu==0) n /= 100;
            else if (iu==1) n /= 10;
            n %= 10;
            uv.x = fract(uv.x);
            ivec2 p = ivec2(floor(uv*vec2(4.0,5.0)));
            return (digitFont(n) >> (p.x+p.y*4)) & 1;
        }
    }
    return 0;
}

float doBrick(in ivec2 id, out vec3 col, out float glo, out vec2 cen)
{
    float alp = 0.0;
    glo = 0.0;
    col = vec3(0.0);
    cen = vec2(0.0);
    if (id.x>0 && id.x<13 && id.y>=0 && id.y<12) {
        float hitTime = uBrickHits[id.y*13+id.x];
        alp = 1.0;
        if (hitTime >= 0.0) {
            float t = max(0.0,iTime-hitTime-0.1);
            alp = exp(-2.0*t);
            glo = exp(-4.0*t);
        }
        if (alp>0.001) {
            float fid = hash1(float(id.x*3+id.y*16));
            col = vec3(0.5,0.5,0.6) +
                  0.4*sin(fid*2.0+4.5+vec3(0.0,1.0,1.0));
            if (hash1(fid*13.1)>0.85) {
                col = 1.0-0.9*col;
                col.xy += 0.2;
            }
        }
        cen = vec2(-1.0+float(id.x)*brickW+0.5*brickW,
                    1.0-float(id.y)*brickH-0.5*brickH);
    }
    return alp;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (2.0*fragCoord-iResolution.xy)/iResolution.y;
    float px = 2.0/iResolution.y;
    vec2 ballPos = uGame0.xy;
    float paddlePos = uGame0.z;
    float points = uGame0.w;
    float state = uGame1.x;
    vec3 lastHit = uGame1.yzw;
    vec3 col = vec3(0.0);
    vec3 emi = vec3(0.0);

    {
        col = 0.6*vec3(0.4,0.6,0.7)*(1.0-0.4*length(uv));
        col *= 1.0-0.1*smoothstep(0.0,1.0,sin(uv.x*80.0)*sin(uv.y*80.0)) *
               (1.0-smoothstep(1.0,1.01,abs(uv.x)));
    }

    {
        float b = brickW*0.17;
        {
            vec2 st = uv+shadowOffset;
            ivec2 id = ivec2(floor(vec2((1.0+st.x)/brickW,
                                       (1.0-st.y)/brickH)));
            vec3 bcol;
            vec2 bcen;
            float bglo;
            float sha = 0.0;
            for (int j=-1; j<=1; j++)
            for (int i=-1; i<=1; i++) {
                ivec2 idr = id+ivec2(i,j);
                float alp = doBrick(idr,bcol,bglo,bcen);
                float f = udRoundBox(st,bcen,0.5*vec2(brickW,brickH)-b,b);
                float s = 1.0-smoothstep(-brickH*0.5,brickH*1.0,f);
                s = mix(0.0,s,alp);
                sha = max(sha,s);
            }
            col = mix(col,col*0.4,sha);
        }

        ivec2 id = ivec2(floor(vec2((1.0+uv.x)/brickW,
                                   (1.0-uv.y)/brickH)));
        {
            vec3 bcol;
            vec2 bcen;
            float bglo;
            float alp = doBrick(id,bcol,bglo,bcen);
            if (alp>0.0001) {
                float f = udRoundBox(uv,bcen,0.5*vec2(brickW,brickH)-b,b);
                bglo += 0.6*smoothstep(-4.0*px,0.0,f);
                bcol *= 0.7+0.3*smoothstep(-4.0*px,-2.0*px,f);
                bcol *= 0.5+1.7*bglo;
                col = mix(col,bcol,alp*(1.0-smoothstep(-px,px,f)));
            }
        }

        for (int j=-1; j<=1; j++)
        for (int i=-1; i<=1; i++) {
            ivec2 idr = id+ivec2(i,j);
            vec3 bcol = vec3(0.0);
            vec2 bcen;
            float bglo;
            float alp = doBrick(idr,bcol,bglo,bcen);
            float f = udRoundBox(uv,bcen,0.5*vec2(brickW,brickH)-b,b);
            emi += bcol*bglo*exp(-600.0*f*f);
        }
    }

    {
        float hit = exp(-4.0*(iTime-lastHit.y));
        float f = 1.0-smoothstep(ballRadius*0.5,ballRadius*2.0,
                                 length(uv-ballPos+shadowOffset));
        col = mix(col,col*0.4,f);
        f = length(uv-ballPos)-ballRadius;
        vec3 bcol = vec3(1.0,0.6,0.2);
        bcol *= 1.0+0.7*smoothstep(-3.0*px,-1.0*px,f);
        bcol *= 0.7+0.3*hit;
        col = mix(col,bcol,1.0-smoothstep(0.0,px,f));
        emi += bcol*0.75*hit*exp(-500.0*f*f);
    }

    {
        float hit = exp(-4.0*(iTime-lastHit.x))*sin(20.0*(iTime-lastHit.x));
        float hit2 = exp(-4.0*(iTime-lastHit.x));
        float y = uv.y+0.04*hit *
                  (1.0-pow(abs(uv.x-paddlePos)/(paddleSize*0.5),2.0));
        float f = udHorizontalSegment(vec2(uv.x,y)+shadowOffset,
                                      paddlePos-paddleSize*0.5,
                                      paddlePos+paddleSize*0.5,paddlePosY);
        f = 1.0-smoothstep(paddleWidth*0.5*0.5,paddleWidth*0.5*2.0,f);
        col = mix(col,col*0.4,f);
        f = udHorizontalSegment(vec2(uv.x,y),paddlePos-paddleSize*0.5,
                                paddlePos+paddleSize*0.5,paddlePosY)-paddleWidth*0.5;
        vec3 bcol = vec3(1.0,0.6,0.2);
        bcol *= 1.0+0.7*smoothstep(-3.0*px,-1.0*px,f);
        bcol *= 0.7+0.3*hit2;
        col = mix(col,bcol,1.0-smoothstep(-px,px,f));
        emi += bcol*0.75*hit2*exp(-500.0*f*f);
    }

    {
        float f = abs(abs(uv.x)-1.02);
        f = min(f,udHorizontalSegment(uv,-1.0,1.0,1.0));
        f *= 2.0;
        float a = 0.8+0.2*sin(2.6*iTime)+0.1*sin(4.0*iTime);
        float hit = exp(-4.0*(iTime-lastHit.z));
        a *= 1.0-0.3*hit;
        col += a*0.5*vec3(0.6,0.30,0.1)*exp(-30.0*f*f);
        col += a*0.5*vec3(0.6,0.35,0.2)*exp(-150.0*f*f);
        col += a*1.7*vec3(0.6,0.50,0.3)*exp(-900.0*f*f);
    }

    {
        float f = float(PrintInt((uv-vec2(-1.5,0.8))*10.0,int(points)));
        col = mix(col,vec3(1.0),f);
    }
    col += emi;
    col = mix(col,vec3(1.0,0.5,0.2),
              state*(0.5+0.5*sin(30.0*iTime)));
    fragColor = vec4(col,1.0);
}

void main(void)
{
    mainImage(gl_FragColor,gl_FragCoord.xy);
}
)GLSL";
