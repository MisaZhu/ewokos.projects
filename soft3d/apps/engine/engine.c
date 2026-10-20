/*
 * engine.c - Interactive inline 4-stroke gasoline engine demo for EwokOS soft3d.
 *
 * An inline-four (DOHC) shown in cutaway through a semi-transparent cast
 * aluminum block, head and cam cover so every internal part stays visible:
 *   - 4 pistons with compression/oil rings, crown dome and wrist-pin bosses
 *   - 4 connecting rods with I-beam, big-end and small-end eyes
 *   - One forged crankshaft, flat-plane (pins 1&4 up, 2&3 down), counterweighted
 *   - 8 poppet valves (intake + exhaust per cylinder) with springs and retainers
 *   - Twin overhead camshafts with 4 egg-shaped lobes each, at half crank speed
 *   - 4 spark plugs with ceramic insulator, hex shell and centre/side electrodes
 *   - Intake runners (cool blue) and exhaust headers (warm) with visible flow
 *   - A mixture cloud per cylinder whose colour follows that cylinder's stroke
 *   - An expanding combustion flame front per cylinder (additive emissive pass)
 *
 * Firing order 1-3-4-2: a cylinder fires every 180 crank degrees.  The whole
 * four-stroke cycle (720 crank degrees) completes in 2 seconds.
 *   Intake       0..180    intake valve open, mixture drawn into cylinder
 *   Compression  180..360  both valves closed, mixture compressed
 *   Power        360..540  spark fires near 355, flame expands, piston driven down
 *   Exhaust      540..720  exhaust valve open, burnt gases pushed out
 *
 * Camera orbits continuously around the engine; materials are shaded per-vertex
 * (Blinn-style with a fixed rd^16 highlight) so polished steel, cast aluminum,
 * ceramic and copper each read differently under the light.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <glcommon/hud.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int win_width  = 960;
static int win_height = 600;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

/* ---------------- inline-4 layout ------------------------------------------ */
#define NCYL            4
#define BORE_R          1.20f   /* cylinder bore radius                       */
#define CRANK_R         1.10f   /* crank throw (half stroke)                  */
#define ROD_L           3.40f   /* connecting rod centre-to-centre length     */
#define PISTON_PIN_DY   0.00f   /* wrist pin at piston local origin           */
#define DECK_Y          5.30f   /* top of cylinder liner / bottom of head     */
#define HEAD_TOP_Y      6.80f   /* top of head casting (cam-cover deck)       */
#define CHAMBER_CEIL    5.90f   /* combustion chamber dome ceiling            */
#define VALVE_X         0.55f   /* valve centreline offset from bore axis     */
#define VALVE_STEM_R    0.09f
#define VALVE_HEAD_R    0.42f
#define VALVE_HEAD_T    0.11f
#define VALVE_SEAT_Y    5.45f   /* valve head underside when fully closed     */
#define MAX_LIFT        0.22f   /* = CAM_NOSE_R - CAM_BASE_R, so the drawn lobe
                                   lifts the bucket tappet by exactly this     */
#define SPRING_SEAT_Y   6.05f   /* machined spring seat in the head           */
#define CAM_Y           7.30f   /* camshaft axis height                       */
#define CAM_BASE_R      0.28f
#define CAM_NOSE_R      0.50f
#define PLUG_TIP_Y      5.60f   /* centre electrode tip inside chamber        */

#define BLOCK_HZ        4.30f   /* block/head half-length along Z (crank axis)*/
#define BLOCK_HX        1.95f   /* block/head half-width along X              */
#define BLOCK_BOT_Y     1.40f   /* bottom of the cylinder block               */

/* timing (crank degrees, one cycle = 720) */
#define INTAKE_PEAK     100.0f
#define EXHAUST_PEAK    620.0f
#define VALVE_DUR       230.0f
#define SPARK_START     352.0f
#define SPARK_END       368.0f
#define BURN_END        560.0f

/* 720 deg / 2 s => the four strokes complete in two seconds */
#define CRANK_DPS       360.0f
#define CAM_ORBIT_DPS   24.0f

/* External timing drive at the front (+Z): a crank sprocket and two camshaft
 * sprockets (2:1 reduction) linked by a toothed belt that runs round a
 * tensioner idler.  Cam sprocket pitch radius is twice the crank's. */
#define TIM_Z0          5.30f   /* sprocket inner face                        */
#define TIM_Z1          5.52f   /* sprocket outer face                        */
#define TIM_ZC          5.41f   /* belt centre plane                          */
#define SPR_CRANK_R     0.26f   /* crank sprocket pitch radius                */
#define SPR_CAM_R       0.52f   /* cam sprocket pitch radius (2x crank)       */
#define TENS_X          1.55f   /* belt tensioner idler centre                */
#define TENS_Y          3.60f
#define TENS_R          0.34f

/* Cylinder z centres, cycle-phase offsets (firing order 1-3-4-2) and crank-pin
 * phase (1&4 share the up throw, 2&3 share the down throw of a flat-plane
 * crank).  Pin world angle = crank_angle + cyl_pin[i]. */
static const float cyl_z[NCYL]   = { -3.30f, -1.10f, 1.10f, 3.30f };
static const float cyl_off[NCYL] = {   0.0f, 540.0f, 180.0f, 360.0f };
static const float cyl_pin[NCYL] = {   0.0f, 180.0f, 180.0f,   0.0f };

/* ---------------- small math helpers (column-major like gears.c) ----------- */
static GLfloat projection_matrix[16];
static GLfloat view_matrix[16];
static GLfloat light_world[3] = { 0.55f, 0.75f, 0.36f };
static GLfloat eye_world[3]   = { 0, 0, 20 };

static void m_ident(GLfloat *m)
{
    static const GLfloat I[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    memcpy(m, I, sizeof(I));
}

static void m_mul(GLfloat *m, const GLfloat *n)   /* m = m * n */
{
    GLfloat t[16];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            GLfloat s = 0;
            for (k = 0; k < 4; k++) s += m[k*4+j] * n[i*4+k];
            t[i*4+j] = s;
        }
    memcpy(m, t, sizeof(t));
}

static void m_translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat t[16]; m_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    m_mul(m, t);
}

static void m_rotate(GLfloat *m, GLfloat deg, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat c = cosf(deg * (GLfloat)M_PI / 180.0f);
    GLfloat s = sinf(deg * (GLfloat)M_PI / 180.0f);
    GLfloat r[16] = {
        x*x*(1-c)+c,   y*x*(1-c)+z*s, x*z*(1-c)-y*s, 0,
        x*y*(1-c)-z*s, y*y*(1-c)+c,   y*z*(1-c)+x*s, 0,
        x*z*(1-c)+y*s, y*z*(1-c)-x*s, z*z*(1-c)+c,   0,
        0,0,0,1
    };
    m_mul(m, r);
}

static void m_scale(GLfloat *m, GLfloat sx, GLfloat sy, GLfloat sz)
{
    GLfloat t[16]; m_ident(t);
    t[0] = sx; t[5] = sy; t[10] = sz;
    m_mul(m, t);
}

/* Proper inverse-transpose of the upper-left 3x3 of a modelview matrix so
 * non-uniform scales (spring compression, charge cloud, flame) shade right. */
static void m_normal_from_mv(GLfloat *out, const GLfloat *mv)
{
    float a00 = mv[0], a01 = mv[4], a02 = mv[8];
    float a10 = mv[1], a11 = mv[5], a12 = mv[9];
    float a20 = mv[2], a21 = mv[6], a22 = mv[10];
    float det = a00*(a11*a22 - a12*a21)
              - a01*(a10*a22 - a12*a20)
              + a02*(a10*a21 - a11*a20);
    if (fabsf(det) < 1e-9f) det = 1e-9f;
    float id = 1.0f / det;
    float i00 = (a11*a22 - a12*a21) * id, i01 = (a02*a21 - a01*a22) * id, i02 = (a01*a12 - a02*a11) * id;
    float i10 = (a12*a20 - a10*a22) * id, i11 = (a00*a22 - a02*a20) * id, i12 = (a02*a10 - a00*a12) * id;
    float i20 = (a10*a21 - a11*a20) * id, i21 = (a01*a20 - a00*a21) * id, i22 = (a00*a11 - a01*a10) * id;
    out[0]=i00; out[1]=i01; out[2]=i02; out[3]=0;
    out[4]=i10; out[5]=i11; out[6]=i12; out[7]=0;
    out[8]=i20; out[9]=i21; out[10]=i22; out[11]=0;
    out[12]=0; out[13]=0; out[14]=0; out[15]=1;
}

static void m_perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat zn, GLfloat zf)
{
    GLfloat t[16]; m_ident(t);
    GLfloat rad = fovy * 0.5f * (GLfloat)M_PI / 180.0f;
    GLfloat s = sinf(rad), c = cosf(rad), dz = zf - zn;
    if (s == 0 || dz == 0 || aspect == 0) { memcpy(m, t, sizeof(t)); return; }
    GLfloat cot = c / s;
    t[0] = cot / aspect; t[5] = cot;
    t[10] = -(zf + zn) / dz; t[11] = -1.0f;
    t[14] = -2.0f * zn * zf / dz; t[15] = 0.0f;
    memcpy(m, t, sizeof(t));
}

static void m_look_at(GLfloat *m, GLfloat ex, GLfloat ey, GLfloat ez,
                      GLfloat cx, GLfloat cy, GLfloat cz,
                      GLfloat ux, GLfloat uy, GLfloat uz)
{
    GLfloat f[3] = { cx-ex, cy-ey, cz-ez };
    GLfloat fl = sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); if (fl < 1e-6f) fl = 1e-6f;
    f[0]/=fl; f[1]/=fl; f[2]/=fl;
    GLfloat s[3] = { f[1]*uz - f[2]*uy, f[2]*ux - f[0]*uz, f[0]*uy - f[1]*ux };
    GLfloat sl = sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]); if (sl < 1e-6f) sl = 1e-6f;
    s[0]/=sl; s[1]/=sl; s[2]/=sl;
    GLfloat u[3] = { s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0] };
    GLfloat t[16] = {
        s[0], u[0], -f[0], 0,
        s[1], u[1], -f[1], 0,
        s[2], u[2], -f[2], 0,
        -(s[0]*ex + s[1]*ey + s[2]*ez),
        -(u[0]*ex + u[1]*ey + u[2]*ez),
         (f[0]*ex + f[1]*ey + f[2]*ez), 1
    };
    memcpy(m, t, sizeof(t));
}

static void m_xform_dir(const GLfloat *m, const GLfloat v[3], GLfloat out[3])
{
    out[0] = m[0]*v[0] + m[4]*v[1] + m[8] *v[2];
    out[1] = m[1]*v[0] + m[5]*v[1] + m[9] *v[2];
    out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2];
}

/* ---------------- mesh builder --------------------------------------------- */
typedef struct {
    GLfloat *data;      /* 6 floats per vertex: px py pz nx ny nz */
    int nverts, cap;
    GLuint vbo;
} Mesh;

static void mesh_init(Mesh *m) { m->data=NULL; m->nverts=0; m->cap=0; m->vbo=0; }

static void mesh_reserve(Mesh *m, int extra)
{
    if (m->nverts + extra <= m->cap) return;
    int nc = m->cap ? m->cap : 96;
    while (nc < m->nverts + extra) nc *= 2;
    GLfloat *nd = (GLfloat*)realloc(m->data, (size_t)nc * 6 * sizeof(GLfloat));
    if (!nd) return;
    m->data = nd; m->cap = nc;
}

static void mesh_v(Mesh *m, float x, float y, float z, float nx, float ny, float nz)
{
    mesh_reserve(m, 1);
    GLfloat *v = m->data + (size_t)m->nverts * 6;
    v[0]=x; v[1]=y; v[2]=z; v[3]=nx; v[4]=ny; v[5]=nz;
    m->nverts++;
}

static void mesh_tri(Mesh *m, float ax,float ay,float az, float bx,float by,float bz,
                     float cx,float cy,float cz)
{
    float ux=bx-ax, uy=by-ay, uz=bz-az, vx=cx-ax, vy=cy-ay, vz=cz-az;
    float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    float l = sqrtf(nx*nx+ny*ny+nz*nz);
    if (l < 1e-8f) return;
    nx/=l; ny/=l; nz/=l;
    mesh_reserve(m, 3);
    mesh_v(m, ax,ay,az, nx,ny,nz);
    mesh_v(m, bx,by,bz, nx,ny,nz);
    mesh_v(m, cx,cy,cz, nx,ny,nz);
}

static void mesh_quad(Mesh *m, float ax,float ay,float az, float bx,float by,float bz,
                      float cx,float cy,float cz, float dx,float dy,float dz)
{
    mesh_tri(m, ax,ay,az, bx,by,bz, cx,cy,cz);
    mesh_tri(m, ax,ay,az, cx,cy,cz, dx,dy,dz);
}

static void mesh_upload(Mesh *m)
{
    if (m->nverts <= 0) return;
    glGenBuffers(1, &m->vbo);
    if (m->vbo == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m->nverts*6*sizeof(GLfloat)),
                 m->data, GL_STATIC_DRAW);
}

/* ---------------- primitive builders --------------------------------------- */
static void p_box(Mesh *m, float cx,float cy,float cz, float hx,float hy,float hz)
{
    float x0=cx-hx,x1=cx+hx,y0=cy-hy,y1=cy+hy,z0=cz-hz,z1=cz+hz;
    mesh_quad(m, x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1);
    mesh_quad(m, x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0);
    mesh_quad(m, x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0);
    mesh_quad(m, x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1);
    mesh_quad(m, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1);
    mesh_quad(m, x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0);
}

static void p_cyl_y(Mesh *m, float cx,float cz, float ybot,float ytop, float r, int segs, int caps)
{
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a0=(float)(2*M_PI*i/segs), a1=(float)(2*M_PI*(i+1)/segs);
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
        float p0x=cx+r*c0,p0z=cz+r*s0,p1x=cx+r*c1,p1z=cz+r*s1;
        mesh_reserve(m,6);
        mesh_v(m,p0x,ybot,p0z, c0,0,s0); mesh_v(m,p1x,ybot,p1z, c1,0,s1); mesh_v(m,p1x,ytop,p1z, c1,0,s1);
        mesh_v(m,p0x,ybot,p0z, c0,0,s0); mesh_v(m,p1x,ytop,p1z, c1,0,s1); mesh_v(m,p0x,ytop,p0z, c0,0,s0);
    }
    if (caps & 1) for (int i=0;i<segs;i++){ float a0=(float)(2*M_PI*i/segs),a1=(float)(2*M_PI*(i+1)/segs);
        mesh_reserve(m,3); mesh_v(m,cx,ytop,cz,0,1,0);
        mesh_v(m,cx+r*cosf(a0),ytop,cz+r*sinf(a0),0,1,0); mesh_v(m,cx+r*cosf(a1),ytop,cz+r*sinf(a1),0,1,0); }
    if (caps & 2) for (int i=0;i<segs;i++){ float a0=(float)(2*M_PI*i/segs),a1=(float)(2*M_PI*(i+1)/segs);
        mesh_reserve(m,3); mesh_v(m,cx,ybot,cz,0,-1,0);
        mesh_v(m,cx+r*cosf(a1),ybot,cz+r*sinf(a1),0,-1,0); mesh_v(m,cx+r*cosf(a0),ybot,cz+r*sinf(a0),0,-1,0); }
}

static void p_cyl_z(Mesh *m, float cx,float cy, float zbot,float ztop, float r, int segs, int caps)
{
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a0=(float)(2*M_PI*i/segs), a1=(float)(2*M_PI*(i+1)/segs);
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
        float p0x=cx+r*c0,p0y=cy+r*s0,p1x=cx+r*c1,p1y=cy+r*s1;
        mesh_reserve(m,6);
        mesh_v(m,p0x,p0y,zbot, c0,s0,0); mesh_v(m,p0x,p0y,ztop, c0,s0,0); mesh_v(m,p1x,p1y,ztop, c1,s1,0);
        mesh_v(m,p0x,p0y,zbot, c0,s0,0); mesh_v(m,p1x,p1y,ztop, c1,s1,0); mesh_v(m,p1x,p1y,zbot, c1,s1,0);
    }
    if (caps & 1) for (int i=0;i<segs;i++){ float a0=(float)(2*M_PI*i/segs),a1=(float)(2*M_PI*(i+1)/segs);
        mesh_reserve(m,3); mesh_v(m,cx,cy,ztop,0,0,1);
        mesh_v(m,cx+r*cosf(a0),cy+r*sinf(a0),ztop,0,0,1); mesh_v(m,cx+r*cosf(a1),cy+r*sinf(a1),ztop,0,0,1); }
    if (caps & 2) for (int i=0;i<segs;i++){ float a0=(float)(2*M_PI*i/segs),a1=(float)(2*M_PI*(i+1)/segs);
        mesh_reserve(m,3); mesh_v(m,cx,cy,zbot,0,0,-1);
        mesh_v(m,cx+r*cosf(a1),cy+r*sinf(a1),zbot,0,0,-1); mesh_v(m,cx+r*cosf(a0),cy+r*sinf(a0),zbot,0,0,-1); }
}

/* Hollow tube along Y (outer r_out, inner r_in, open ends). */
static void p_tube_y(Mesh *m, float cx,float cz, float ybot,float ytop, float r_out,float r_in, int segs)
{
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a0=(float)(2*M_PI*i/segs), a1=(float)(2*M_PI*(i+1)/segs);
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
        mesh_reserve(m,12);
        mesh_v(m,cx+r_out*c0,ybot,cz+r_out*s0, c0,0,s0);
        mesh_v(m,cx+r_out*c1,ybot,cz+r_out*s1, c1,0,s1);
        mesh_v(m,cx+r_out*c1,ytop,cz+r_out*s1, c1,0,s1);
        mesh_v(m,cx+r_out*c0,ybot,cz+r_out*s0, c0,0,s0);
        mesh_v(m,cx+r_out*c1,ytop,cz+r_out*s1, c1,0,s1);
        mesh_v(m,cx+r_out*c0,ytop,cz+r_out*s0, c0,0,s0);
        mesh_v(m,cx+r_in*c0,ybot,cz+r_in*s0, -c0,0,-s0);
        mesh_v(m,cx+r_in*c0,ytop,cz+r_in*s0, -c0,0,-s0);
        mesh_v(m,cx+r_in*c1,ytop,cz+r_in*s1, -c1,0,-s1);
        mesh_v(m,cx+r_in*c0,ybot,cz+r_in*s0, -c0,0,-s0);
        mesh_v(m,cx+r_in*c1,ytop,cz+r_in*s1, -c1,0,-s1);
        mesh_v(m,cx+r_in*c1,ybot,cz+r_in*s1, -c1,0,-s1);
    }
}

/* Flat disc/annulus on Y. r_in may be 0. flip=1 faces -Y. */
static void p_disc_y(Mesh *m, float cx,float cy,float cz, float r_out,float r_in, int segs, int flip)
{
    float ny = flip ? -1.0f : 1.0f;
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a0=(float)(2*M_PI*i/segs), a1=(float)(2*M_PI*(i+1)/segs);
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
        mesh_reserve(m,6);
        if (r_in > 0.0f) {
            if (!flip) {
                mesh_v(m,cx+r_out*c0,cy,cz+r_out*s0,0,ny,0); mesh_v(m,cx+r_out*c1,cy,cz+r_out*s1,0,ny,0);
                mesh_v(m,cx+r_in*c1,cy,cz+r_in*s1,0,ny,0);
                mesh_v(m,cx+r_out*c0,cy,cz+r_out*s0,0,ny,0); mesh_v(m,cx+r_in*c1,cy,cz+r_in*s1,0,ny,0);
                mesh_v(m,cx+r_in*c0,cy,cz+r_in*s0,0,ny,0);
            } else {
                mesh_v(m,cx+r_out*c0,cy,cz+r_out*s0,0,ny,0); mesh_v(m,cx+r_in*c0,cy,cz+r_in*s0,0,ny,0);
                mesh_v(m,cx+r_in*c1,cy,cz+r_in*s1,0,ny,0);
                mesh_v(m,cx+r_out*c0,cy,cz+r_out*s0,0,ny,0); mesh_v(m,cx+r_in*c1,cy,cz+r_in*s1,0,ny,0);
                mesh_v(m,cx+r_out*c1,cy,cz+r_out*s1,0,ny,0);
            }
        } else {
            if (!flip) { mesh_v(m,cx,cy,cz,0,ny,0); mesh_v(m,cx+r_out*c0,cy,cz+r_out*s0,0,ny,0);
                         mesh_v(m,cx+r_out*c1,cy,cz+r_out*s1,0,ny,0); }
            else       { mesh_v(m,cx,cy,cz,0,ny,0); mesh_v(m,cx+r_out*c1,cy,cz+r_out*s1,0,ny,0);
                         mesh_v(m,cx+r_out*c0,cy,cz+r_out*s0,0,ny,0); }
        }
    }
}

/* Downward-facing combustion-chamber roof: a shallow dome revolved from the
 * bore rim (radius r_rim at height y_rim) up to the chamber ceiling y_top at
 * the centre.  Normals point down into the chamber so the roof is lit from
 * inside the cylinder.  This replaces the old full sphere that read as a ball
 * floating at the top of the bore. */
static void p_chamber_dome(Mesh *m, float cz, float y_rim, float y_top,
                           float r_rim, int segs, int steps)
{
    if (segs < 3) segs = 3;
    if (steps < 1) steps = 1;
    for (int s = 0; s < steps; s++) {
        float a0 = (float)s/steps*(float)M_PI*0.5f;
        float a1 = (float)(s+1)/steps*(float)M_PI*0.5f;
        float r0 = r_rim*cosf(a0), yy0 = y_rim + (y_top-y_rim)*sinf(a0);
        float r1 = r_rim*cosf(a1), yy1 = y_rim + (y_top-y_rim)*sinf(a1);
        for (int i = 0; i < segs; i++) {
            float b0=(float)(2*M_PI*i/segs), b1=(float)(2*M_PI*(i+1)/segs);
            float c0=cosf(b0),s0=sinf(b0),c1=cosf(b1),s1=sinf(b1);
            mesh_quad(m,
                r0*c0, yy0, cz+r0*s0,
                r0*c1, yy0, cz+r0*s1,
                r1*c1, yy1, cz+r1*s1,
                r1*c0, yy1, cz+r1*s0);
        }
    }
}

static void p_cone_y(Mesh *m, float cx,float cz, float ybot,float ytop, float r0,float r1, int segs)
{
    float dy=ytop-ybot, dr=r0-r1, sl=sqrtf(dy*dy+dr*dr);
    float ny_c=(sl<1e-6f)?1.0f:(dr/sl), nr_c=(sl<1e-6f)?0.0f:(dy/sl);
    if (segs < 3) segs = 3;
    for (int i = 0; i < segs; i++) {
        float a0=(float)(2*M_PI*i/segs), a1=(float)(2*M_PI*(i+1)/segs);
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
        mesh_reserve(m,6);
        mesh_v(m,cx+r0*c0,ybot,cz+r0*s0, nr_c*c0,ny_c,nr_c*s0);
        mesh_v(m,cx+r0*c1,ybot,cz+r0*s1, nr_c*c1,ny_c,nr_c*s1);
        mesh_v(m,cx+r1*c1,ytop,cz+r1*s1, nr_c*c1,ny_c,nr_c*s1);
        if (r1 > 1e-5f) {
            mesh_v(m,cx+r0*c0,ybot,cz+r0*s0, nr_c*c0,ny_c,nr_c*s0);
            mesh_v(m,cx+r1*c1,ytop,cz+r1*s1, nr_c*c1,ny_c,nr_c*s1);
            mesh_v(m,cx+r1*c0,ytop,cz+r1*s0, nr_c*c0,ny_c,nr_c*s0);
        }
    }
}

static void p_torus_y(Mesh *m, float cx,float cy,float cz, float R,float r, int su,int sv)
{
    if (su<3) su=3;
    if (sv<3) sv=3;
    for (int i=0;i<su;i++){
        float u0=(float)(2*M_PI*i/su),u1=(float)(2*M_PI*(i+1)/su);
        float cu0=cosf(u0),su0=sinf(u0),cu1=cosf(u1),su1=sinf(u1);
        for (int j=0;j<sv;j++){
            float v0=(float)(2*M_PI*j/sv),v1=(float)(2*M_PI*(j+1)/sv);
            float cv0=cosf(v0),sv0=sinf(v0),cv1=cosf(v1),sv1=sinf(v1);
            float r0=R+r*cv0, r1=R+r*cv1;
            mesh_reserve(m,6);
            mesh_v(m,cx+r0*cu0,cy+r*sv0,cz+r0*su0, cv0*cu0,sv0,cv0*su0);
            mesh_v(m,cx+r0*cu1,cy+r*sv0,cz+r0*su1, cv0*cu1,sv0,cv0*su1);
            mesh_v(m,cx+r1*cu1,cy+r*sv1,cz+r1*su1, cv1*cu1,sv1,cv1*su1);
            mesh_v(m,cx+r0*cu0,cy+r*sv0,cz+r0*su0, cv0*cu0,sv0,cv0*su0);
            mesh_v(m,cx+r1*cu1,cy+r*sv1,cz+r1*su1, cv1*cu1,sv1,cv1*su1);
            mesh_v(m,cx+r1*cu0,cy+r*sv1,cz+r1*su0, cv1*cu0,sv1,cv1*su0);
        }
    }
}

static void p_spring_y(Mesh *m, float cx,float cz, float ybot,float ytop, float R,float tube_r,
                       int turns,int su,int sv)
{
    int total = turns*su;
    if (total < 3) return;
    if (sv < 3) sv = 3;
    float dy = (ytop-ybot)/(float)total;
    for (int i=0;i<total;i++){
        float u0=(float)(2*M_PI*(i%su)/su), u1=(float)(2*M_PI*((i%su)+1)/su);
        float y0=ybot+dy*i, y1=ybot+dy*(i+1);
        float cu0=cosf(u0),su0=sinf(u0),cu1=cosf(u1),su1=sinf(u1);
        for (int j=0;j<sv;j++){
            float v0=(float)(2*M_PI*j/sv),v1=(float)(2*M_PI*(j+1)/sv);
            float cv0=cosf(v0),sv0=sinf(v0),cv1=cosf(v1),sv1=sinf(v1);
            float r0v=R+tube_r*cv0, r1v=R+tube_r*cv1;
            mesh_reserve(m,6);
            mesh_v(m,cx+r0v*cu0,y0+tube_r*sv0,cz+r0v*su0, cv0*cu0,sv0,cv0*su0);
            mesh_v(m,cx+r0v*cu1,y1+tube_r*sv0,cz+r0v*su1, cv0*cu1,sv0,cv0*su1);
            mesh_v(m,cx+r1v*cu1,y1+tube_r*sv1,cz+r1v*su1, cv1*cu1,sv1,cv1*su1);
            mesh_v(m,cx+r0v*cu0,y0+tube_r*sv0,cz+r0v*su0, cv0*cu0,sv0,cv0*su0);
            mesh_v(m,cx+r1v*cu1,y1+tube_r*sv1,cz+r1v*su1, cv1*cu1,sv1,cv1*su1);
            mesh_v(m,cx+r1v*cu0,y0+tube_r*sv1,cz+r1v*su0, cv1*cu0,sv1,cv1*su0);
        }
    }
}

/* Toothed sprocket wheel lying in the XY plane, extruded along Z from z0 to z1.
 * The rim carries trapezoidal teeth; a hub boss fills the centre bore so the
 * wheel reads as a real timing sprocket keyed to its shaft. */
static void p_sprocket_z(Mesh *m, float cx, float cy, float z0, float z1,
                         float r, int teeth, float hub_r)
{
    if (teeth < 6) teeth = 6;
    int segs = teeth * 4;                 /* root,tip,tip,root per tooth */
    float tip = r + 0.10f;
    for (int i = 0; i < segs; i++) {
        int j = (i + 1) % segs;
        float a0 = (float)(2*M_PI*i/segs), a1 = (float)(2*M_PI*j/segs);
        float r0 = ((i % 4) == 1 || (i % 4) == 2) ? tip : r;
        float r1 = ((j % 4) == 1 || (j % 4) == 2) ? tip : r;
        float x0 = cx + r0*cosf(a0), y0 = cy + r0*sinf(a0);
        float x1 = cx + r1*cosf(a1), y1 = cy + r1*sinf(a1);
        float hx0 = cx + hub_r*cosf(a0), hy0 = cy + hub_r*sinf(a0);
        float hx1 = cx + hub_r*cosf(a1), hy1 = cy + hub_r*sinf(a1);
        mesh_quad(m, x0,y0,z0, x1,y1,z0, x1,y1,z1, x0,y0,z1);   /* toothed band */
        mesh_quad(m, hx0,hy0,z1, x0,y0,z1, x1,y1,z1, hx1,hy1,z1);/* +Z web */
        mesh_quad(m, hx0,hy0,z0, hx1,hy1,z0, x1,y1,z0, x0,y0,z0);/* -Z web */
    }
    p_cyl_z(m, cx, cy, z0 - 0.06f, z1 + 0.06f, hub_r, 16, 3);   /* hub boss */
}

/* Closed external-tangent belt looped round n pulleys wound CCW.  Each pulley
 * contact arc sweeps CCW from its incoming to its outgoing tangent normal; the
 * centreline is sampled and extruded into a thin toothed ribbon. */
static void build_belt(Mesh *m, const float *bx, const float *by, const float *br,
                       int n, float zc, float width, float thick)
{
    if (n < 3 || n > 8) return;
    enum { MAXP = 512 };
    static float px[MAXP], py[MAXP], nnx[MAXP], nny[MAXP];
    float mox[8], moy[8], mix[8], miy[8];
    int np = 0;

    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float dx = bx[j]-bx[i], dy = by[j]-by[i];
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 1e-5f) continue;
        float ux = dx/dist, uy = dy/dist;
        float rx = uy, ry = -ux;                  /* right of travel */
        float k = (br[i]-br[j])/dist;
        float s = sqrtf(fmaxf(0.0f, 1.0f - k*k));
        mox[i] = k*ux + s*rx;  moy[i] = k*uy + s*ry;
        mix[j] = mox[i];       miy[j] = moy[i];   /* external tangent: shared normal */
    }

    float ro = thick * 0.5f;
    for (int i = 0; i < n; i++) {
        float a_in  = atan2f(miy[i], mix[i]);
        float a_out = atan2f(moy[i], mox[i]);
        float sweep = a_out - a_in;
        while (sweep <= 0.0f) sweep += (float)(2*M_PI);
        int steps = (int)(sweep / 0.12f) + 1;
        if (steps > 48) steps = 48;
        for (int s = 0; s <= steps && np < MAXP; s++) {
            float a = a_in + sweep * s/steps;
            float ca = cosf(a), sa = sinf(a);
            px[np] = bx[i] + br[i]*ca; py[np] = by[i] + br[i]*sa;
            nnx[np] = ca; nny[np] = sa; np++;
        }
        int j = (i + 1) % n;
        float sx = bx[i] + br[i]*mox[i], sy = by[i] + br[i]*moy[i];
        float ex = bx[j] + br[j]*mix[j], ey = by[j] + br[j]*miy[j];
        for (int s = 1; s <= 3 && np < MAXP; s++) {
            float t = (float)s/4.0f;
            px[np] = sx + (ex-sx)*t; py[np] = sy + (ey-sy)*t;
            nnx[np] = mox[i]; nny[np] = moy[i]; np++;
        }
    }

    float z0 = zc - width*0.5f, z1 = zc + width*0.5f;
    for (int i = 0; i < np; i++) {
        int j = (i + 1) % np;
        float ax = px[i] + nnx[i]*ro, ay = py[i] + nny[i]*ro;   /* outer A */
        float cxo = px[i] - nnx[i]*ro, cyo = py[i] - nny[i]*ro; /* inner A */
        float bxo = px[j] + nnx[j]*ro, byo = py[j] + nny[j]*ro; /* outer B */
        float bxi = px[j] - nnx[j]*ro, byi = py[j] - nny[j]*ro; /* inner B */
        mesh_quad(m, ax,ay,z0, bxo,byo,z0, bxo,byo,z1, ax,ay,z1);    /* outer */
        mesh_quad(m, cxo,cyo,z0, cxo,cyo,z1, bxi,byi,z1, bxi,byi,z0);/* inner */
        mesh_quad(m, ax,ay,z1, bxo,byo,z1, bxi,byi,z1, cxo,cyo,z1);  /* +Z side */
        mesh_quad(m, ax,ay,z0, cxo,cyo,z0, bxi,byi,z0, bxo,byo,z0);  /* -Z side */
    }
}


/* ---------------- parts ---------------------------------------------------- */
typedef struct {
    Mesh mesh;
    GLfloat color[4];
    GLfloat spec;
    GLfloat emissive;
    int dynamic;
} Part;

/* Shared structures spanning all four cylinders */
static Part P_block, P_head, P_camcover, P_crankcase, P_liner, P_baseplate, P_boltset;
static Part P_crank, P_cam_in, P_cam_ex;
static Part P_plug_body, P_plug_tip, P_port_in, P_port_ex;
static Part P_spr_crank, P_spr_cam_in, P_spr_cam_ex, P_tensioner, P_belt;
/* Per-cylinder templates (built once at z=0, instanced to each cylinder) */
static Part P_piston, P_rod, P_valve_in, P_valve_ex, P_spring_in, P_spring_ex;
static Part P_flow_in, P_flow_ex, P_charge, P_chamber_gas;

/* ---------------- material presets ----------------------------------------- */
static const GLfloat MAT_block[4]      = { 0.72f, 0.73f, 0.76f, 0.22f };
static const GLfloat MAT_head[4]       = { 0.68f, 0.70f, 0.74f, 0.22f };
static const GLfloat MAT_camcover[4]   = { 0.55f, 0.57f, 0.62f, 0.24f };
static const GLfloat MAT_crankcase[4]  = { 0.42f, 0.43f, 0.47f, 0.24f };
static const GLfloat MAT_liner[4]      = { 0.55f, 0.58f, 0.62f, 0.13f };  /* very clear so piston shows */
static const GLfloat MAT_piston[4]     = { 0.70f, 0.70f, 0.72f, 1.00f };
static const GLfloat MAT_rod[4]        = { 0.56f, 0.57f, 0.61f, 1.00f };
static const GLfloat MAT_crank[4]      = { 0.44f, 0.45f, 0.49f, 1.00f };
static const GLfloat MAT_valve_in[4]   = { 0.74f, 0.75f, 0.78f, 1.00f };
static const GLfloat MAT_valve_ex[4]   = { 0.64f, 0.56f, 0.48f, 1.00f };
static const GLfloat MAT_spring[4]     = { 0.30f, 0.32f, 0.36f, 1.00f };
static const GLfloat MAT_cam[4]        = { 0.52f, 0.54f, 0.58f, 1.00f };
static const GLfloat MAT_plug_cer[4]   = { 0.92f, 0.88f, 0.78f, 1.00f };
static const GLfloat MAT_copper[4]     = { 0.80f, 0.46f, 0.22f, 1.00f };
static const GLfloat MAT_port_in[4]    = { 0.34f, 0.42f, 0.56f, 1.00f };
static const GLfloat MAT_port_ex[4]    = { 0.56f, 0.38f, 0.31f, 1.00f };
static const GLfloat MAT_flow_in[4]    = { 0.35f, 0.65f, 1.00f, 0.55f };
static const GLfloat MAT_flow_ex[4]    = { 0.60f, 0.60f, 0.64f, 0.55f };
static const GLfloat MAT_base[4]       = { 0.28f, 0.29f, 0.32f, 1.00f };
static const GLfloat MAT_bolt[4]       = { 0.26f, 0.27f, 0.30f, 1.00f };
static const GLfloat MAT_sprocket[4]   = { 0.50f, 0.51f, 0.55f, 1.00f };
static const GLfloat MAT_belt[4]       = { 0.12f, 0.12f, 0.13f, 1.00f };
static GLfloat charge_color[4]         = { 0.35f, 0.55f, 0.95f, 0.35f };

static void part_set(Part *p, const GLfloat c[4], GLfloat spec, GLfloat emis, int dyn)
{
    memcpy(p->color, c, 4*sizeof(GLfloat));
    p->spec = spec; p->emissive = emis; p->dynamic = dyn;
}

/* ---------------- shared structure builders -------------------------------- */

/* Block: hollow rectangular jacket (open at the deck so the bores are visible
 * from above) plus four very transparent cylinder liners. */
static void build_block(void)
{
    Mesh *m = &P_block.mesh; mesh_init(m);
    float ymid = (BLOCK_BOT_Y + DECK_Y) * 0.5f;
    float yhalf = (DECK_Y - BLOCK_BOT_Y) * 0.5f;
    float t = 0.10f;
    /* four walls; the top is left open (the head seals it) */
    p_box(m, 0, ymid, -BLOCK_HZ, BLOCK_HX, yhalf, t);
    p_box(m, 0, ymid,  BLOCK_HZ, BLOCK_HX, yhalf, t);
    p_box(m, -BLOCK_HX, ymid, 0, t, yhalf, BLOCK_HZ);
    p_box(m,  BLOCK_HX, ymid, 0, t, yhalf, BLOCK_HZ);
    /* bottom flange meeting the crankcase */
    p_box(m, 0, BLOCK_BOT_Y - 0.08f, 0, BLOCK_HX, 0.08f, BLOCK_HZ);
    part_set(&P_block, MAT_block, 0.25f, 0.0f, 0);

    /* liners: one transparent tube per bore */
    mesh_init(&P_liner.mesh);
    for (int i = 0; i < NCYL; i++)
        p_tube_y(&P_liner.mesh, 0, cyl_z[i], BLOCK_BOT_Y + 0.05f, DECK_Y,
                 BORE_R + 0.06f, BORE_R, 32);
    part_set(&P_liner, MAT_liner, 0.60f, 0.0f, 0);
}

/* Head: a complete DOHC casting - deck face, domed combustion chambers, valve
 * seats and guides, a centre spark-plug tube, and camshaft bearing pedestals
 * carrying both camshafts up in the cam cover. */
static void build_head(void)
{
    Mesh *m = &P_head.mesh; mesh_init(m);
    float ymid = (DECK_Y + HEAD_TOP_Y) * 0.5f;
    float yhalf = (HEAD_TOP_Y - DECK_Y) * 0.5f;
    float t = 0.10f;
    /* outer casting walls */
    p_box(m, 0, ymid, -BLOCK_HZ, BLOCK_HX, yhalf, t);
    p_box(m, 0, ymid,  BLOCK_HZ, BLOCK_HX, yhalf, t);
    p_box(m, -BLOCK_HX, ymid, 0, t, yhalf, BLOCK_HZ);
    p_box(m,  BLOCK_HX, ymid, 0, t, yhalf, BLOCK_HZ);
    /* cam-cover mounting face (top) and deck/gasket face (bottom) */
    p_box(m, 0, HEAD_TOP_Y, 0, BLOCK_HX, 0.08f, BLOCK_HZ);
    p_box(m, 0, DECK_Y - 0.05f, 0, BLOCK_HX, 0.05f, BLOCK_HZ);

    for (int i = 0; i < NCYL; i++) {
        float z = cyl_z[i];
        /* combustion-chamber roof: a shallow dome, not a sphere */
        p_chamber_dome(m, z, DECK_Y, CHAMBER_CEIL, BORE_R - 0.06f, 26, 6);
        /* valve seat rings inserted into the chamber roof */
        p_tube_y(m, -VALVE_X, z, VALVE_SEAT_Y - 0.05f, VALVE_SEAT_Y + 0.10f,
                 VALVE_HEAD_R + 0.05f, VALVE_HEAD_R - 0.02f, 18);
        p_tube_y(m,  VALVE_X, z, VALVE_SEAT_Y - 0.05f, VALVE_SEAT_Y + 0.10f,
                 VALVE_HEAD_R + 0.05f, VALVE_HEAD_R - 0.02f, 18);
        /* valve guides: stems pass up through the roof into the head */
        p_tube_y(m, -VALVE_X, z, VALVE_SEAT_Y + 0.12f, HEAD_TOP_Y - 0.05f,
                 0.16f, VALVE_STEM_R + 0.02f, 12);
        p_tube_y(m,  VALVE_X, z, VALVE_SEAT_Y + 0.12f, HEAD_TOP_Y - 0.05f,
                 0.16f, VALVE_STEM_R + 0.02f, 12);
        /* spark-plug tube through the centre of the chamber + top boss */
        p_tube_y(m, 0, z, CHAMBER_CEIL - 0.05f, HEAD_TOP_Y + 0.05f, 0.26f, 0.15f, 14);
        p_cyl_y(m, 0, z, HEAD_TOP_Y + 0.05f, HEAD_TOP_Y + 0.30f, 0.30f, 14, 3);
        /* spring seats: the valve springs rest on these pads in the head */
        p_disc_y(m, -VALVE_X, SPRING_SEAT_Y - 0.02f, z, 0.34f, 0.17f, 18, 0);
        p_disc_y(m,  VALVE_X, SPRING_SEAT_Y - 0.02f, z, 0.34f, 0.17f, 18, 0);
    }

    /* camshaft bearing pedestals + caps sit between the lobes (both cams) */
    {
        static const float bear_z[3] = { -2.20f, 0.0f, 2.20f };
        for (int b = 0; b < 3; b++)
            for (int s = 0; s < 2; s++) {
                float cx = (s == 0 ? -VALVE_X : VALVE_X);
                float bz = bear_z[b];
                p_box(m, cx, (HEAD_TOP_Y + CAM_Y)*0.5f, bz,
                      0.34f, (CAM_Y - HEAD_TOP_Y)*0.5f, 0.30f);
                p_cyl_z(m, cx, CAM_Y, bz - 0.30f, bz + 0.30f, 0.38f, 14, 0);
            }
    }
    part_set(&P_head, MAT_head, 0.30f, 0.0f, 0);

    /* cam cover: sits on the head top deck and encloses cams + bucket tappets */
    mesh_init(&P_camcover.mesh);
    p_box(&P_camcover.mesh, 0, 7.45f, 0, 1.55f, 0.65f, BLOCK_HZ);
    p_box(&P_camcover.mesh, 0, HEAD_TOP_Y + 0.08f, 0, 1.45f, 0.10f, BLOCK_HZ - 0.05f);
    part_set(&P_camcover, MAT_camcover, 0.20f, 0.0f, 0);

    /* head bolts along both rails */
    mesh_init(&P_boltset.mesh);
    for (int i = 0; i < NCYL; i++)
        for (int s = 0; s < 2; s++)
            for (int k = 0; k < 2; k++) {
                float bx = (s == 0 ? -1 : 1) * (BORE_R + 0.45f);
                float bz = cyl_z[i] + (k == 0 ? -0.70f : 0.70f);
                p_cyl_y(&P_boltset.mesh, bx, bz, DECK_Y - 0.05f, HEAD_TOP_Y + 0.12f, 0.12f, 6, 3);
            }
    part_set(&P_boltset, MAT_bolt, 0.40f, 0.0f, 0);
}

/* Crankcase + oil pan around the crank. */
static void build_crankcase(void)
{
    Mesh *m = &P_crankcase.mesh; mesh_init(m);
    float ymid = -0.30f, yhalf = 1.70f, t = 0.10f;
    p_box(m, 0, ymid, -BLOCK_HZ - 0.1f, BLOCK_HX, yhalf, t);
    p_box(m, 0, ymid,  BLOCK_HZ + 0.1f, BLOCK_HX, yhalf, t);
    p_box(m, -BLOCK_HX, ymid, 0, t, yhalf, BLOCK_HZ + 0.1f);
    p_box(m,  BLOCK_HX, ymid, 0, t, yhalf, BLOCK_HZ + 0.1f);
    part_set(&P_crankcase, MAT_crankcase, 0.20f, 0.0f, 0);

    mesh_init(&P_baseplate.mesh);
    p_box(&P_baseplate.mesh, 0, -2.10f, 0, BLOCK_HX + 0.05f, 0.15f, BLOCK_HZ + 0.15f);
    p_box(&P_baseplate.mesh, 0, -2.45f, 0, BLOCK_HX - 0.35f, 0.22f, BLOCK_HZ - 0.5f);
    part_set(&P_baseplate, MAT_base, 0.25f, 0.0f, 0);
}

/* Crankshaft: flat-plane inline-4. Pins 1&4 throw to +Y, 2&3 to -Y in the local
 * frame; the whole shaft is rotated by -crank_angle about Z each frame. */
static void build_crank(void)
{
    Mesh *m = &P_crank.mesh; mesh_init(m);
    float zmin = -BLOCK_HZ - 0.5f, zmax = BLOCK_HZ + 0.5f;
    p_cyl_z(m, 0, 0, zmin, zmax, 0.34f, 20, 3);   /* main journal */
    for (int i = 0; i < NCYL; i++) {
        float sgn = (cyl_pin[i] == 0.0f) ? 1.0f : -1.0f;
        float py = sgn * CRANK_R, zc = cyl_z[i], zh = 0.30f, wh = 0.11f;
        p_cyl_z(m, 0, py, zc - zh, zc + zh, 0.30f, 18, 3);   /* crank pin */
        for (int s = 0; s < 2; s++) {
            float zw = zc + (s == 0 ? -(zh + wh) : (zh + wh));
            /* web plate from journal to pin */
            p_box(m, 0, sgn * CRANK_R * 0.5f, zw, 0.36f, CRANK_R * 0.5f + 0.34f, wh);
            /* counterweight: half disc opposite the pin */
            float a_start = (sgn > 0) ? (float)M_PI : 0.0f;
            int N = 14;
            for (int j = 0; j < N; j++) {
                float ang0 = a_start + (float)M_PI * j / N;
                float ang1 = a_start + (float)M_PI * (j+1) / N;
                float r = 0.95f;
                float q0x = r*cosf(ang0), q0y = r*sinf(ang0);
                float q1x = r*cosf(ang1), q1y = r*sinf(ang1);
                mesh_tri(m, 0,0,zw-wh, q1x,q1y,zw-wh, q0x,q0y,zw-wh);
                mesh_tri(m, 0,0,zw+wh, q0x,q0y,zw+wh, q1x,q1y,zw+wh);
                mesh_quad(m, q0x,q0y,zw-wh, q0x,q0y,zw+wh, q1x,q1y,zw+wh, q1x,q1y,zw-wh);
            }
        }
    }
    /* flywheel (-Z) and nose pulley (+Z) */
    p_cyl_z(m, 0, 0, zmin - 0.45f, zmin, 1.15f, 28, 3);
    p_torus_y(m, 0, 0, zmin - 0.45f, 1.15f, 0.07f, 24, 6);
    p_cyl_z(m, 0, 0, zmax, zmax + 0.35f, 0.78f, 24, 3);
    p_torus_y(m, 0, 0, zmax + 0.35f, 0.78f, 0.06f, 20, 6);
    p_cyl_z(m, 0, 0, zmax + 0.35f, TIM_Z1, 0.24f, 12, 0);  /* nose carrying the timing sprocket */
    part_set(&P_crank, MAT_crank, 0.55f, 0.0f, 1);
}

/* Cam lobe radius profile: smooth cos^2 nose centred at local angle -pi/2. */
static float cam_bump(float a)
{
    float half_arc = (VALVE_DUR * 0.25f) * (float)M_PI / 180.0f;
    float da = a + (float)M_PI * 0.5f;
    while (da >  (float)M_PI) da -= (float)(2*M_PI);
    while (da < -(float)M_PI) da += (float)(2*M_PI);
    if (fabsf(da) >= half_arc) return 0.0f;
    float t = da / half_arc, c = cosf(t * (float)M_PI * 0.5f);
    return c * c;
}

/* Build one camshaft (long journal + 4 pre-phased lobes). exhaust picks the
 * lobe phase so the nose points down at EXHAUST_PEAK instead of INTAKE_PEAK. */
static void build_camshaft(Mesh *m, int exhaust)
{
    mesh_init(m);
    p_cyl_z(m, 0, 0, -BLOCK_HZ - 0.2f, BLOCK_HZ + 0.2f, CAM_BASE_R, 18, 3);
    p_cyl_z(m, 0, 0, BLOCK_HZ + 0.2f, TIM_Z1, CAM_BASE_R, 12, 0);  /* nose to the timing sprocket */
    float peak = exhaust ? EXHAUST_PEAK : INTAKE_PEAK;
    int segs = 40;
    for (int i = 0; i < NCYL; i++) {
        /* lobe nose must point -Y when cam_rot = crank/2 reaches (peak+off)/2 */
        float beta = (-90.0f - (peak + cyl_off[i]) * 0.5f) * (float)M_PI / 180.0f;
        float zc = cyl_z[i], z0 = zc - 0.30f, z1 = zc + 0.30f;
        for (int s = 0; s < segs; s++) {
            float a0 = beta + (float)(2*M_PI*s/segs);
            float a1 = beta + (float)(2*M_PI*(s+1)/segs);
            float r0 = CAM_BASE_R + (CAM_NOSE_R - CAM_BASE_R) * cam_bump(a0);
            float r1 = CAM_BASE_R + (CAM_NOSE_R - CAM_BASE_R) * cam_bump(a1);
            float p0x = r0*cosf(a0), p0y = r0*sinf(a0);
            float p1x = r1*cosf(a1), p1y = r1*sinf(a1);
            float n0x = cosf(a0), n0y = sinf(a0), n1x = cosf(a1), n1y = sinf(a1);
            mesh_reserve(m, 6);
            mesh_v(m, p0x,p0y,z0, n0x,n0y,0); mesh_v(m, p0x,p0y,z1, n0x,n0y,0); mesh_v(m, p1x,p1y,z1, n1x,n1y,0);
            mesh_v(m, p0x,p0y,z0, n0x,n0y,0); mesh_v(m, p1x,p1y,z1, n1x,n1y,0); mesh_v(m, p1x,p1y,z0, n1x,n1y,0);
            mesh_tri(m, 0,0,z1, p0x,p0y,z1, p1x,p1y,z1);
            mesh_tri(m, 0,0,z0, p1x,p1y,z0, p0x,p0y,z0);
        }
    }
}

static void build_cams(void)
{
    build_camshaft(&P_cam_in.mesh, 0);
    part_set(&P_cam_in, MAT_cam, 0.55f, 0.0f, 1);
    build_camshaft(&P_cam_ex.mesh, 1);
    part_set(&P_cam_ex, MAT_cam, 0.55f, 0.0f, 1);
}

/* Spark plugs (4) and their electrodes. */
static void build_plugs(void)
{
    mesh_init(&P_plug_body.mesh);
    Mesh *m = &P_plug_body.mesh;
    mesh_init(&P_plug_tip.mesh);
    Mesh *t = &P_plug_tip.mesh;
    for (int i = 0; i < NCYL; i++) {
        float z = cyl_z[i];
        float base = HEAD_TOP_Y + 0.25f;
        p_cyl_y(m, 0, z, base - 0.80f, base - 0.10f, 0.13f, 10, 0);   /* threads */
        p_cyl_y(m, 0, z, base - 0.10f, base + 0.18f, 0.21f, 6, 3);    /* hex shell */
        p_cone_y(m, 0, z, base + 0.18f, base + 0.78f, 0.17f, 0.09f, 14); /* ceramic */
        p_cyl_y(m, 0, z, base + 0.78f, base + 1.00f, 0.09f, 10, 3);   /* terminal */
        /* centre electrode down into the chamber + L-shaped side electrode */
        p_cyl_y(t, 0, z, PLUG_TIP_Y - 0.05f, base - 0.80f, 0.045f, 8, 3);
        p_box(t, 0.12f, PLUG_TIP_Y + 0.05f, z, 0.028f, 0.10f, 0.028f);
        p_box(t, 0.06f, PLUG_TIP_Y - 0.05f, z, 0.085f, 0.023f, 0.028f);
    }
    part_set(&P_plug_body, MAT_plug_cer, 0.35f, 0.0f, 0);
    part_set(&P_plug_tip, MAT_copper, 0.60f, 0.0f, 0);
}

/* Intake runners (-X) and exhaust headers (+X), one per cylinder. */
static void build_ports(void)
{
    mesh_init(&P_port_in.mesh);
    mesh_init(&P_port_ex.mesh);
    mesh_init(&P_flow_in.mesh);
    mesh_init(&P_flow_ex.mesh);
    for (int i = 0; i < NCYL; i++) {
        float z = cyl_z[i];
        /* intake runner */
        for (int k = 0; k < 8; k++) {
            float t0 = k/8.0f, t1 = (k+1)/8.0f;
            float y0 = HEAD_TOP_Y - 0.05f + t0*1.25f, y1 = HEAD_TOP_Y - 0.05f + t1*1.25f;
            float x0 = -VALVE_X - t0*1.05f, x1 = -VALVE_X - t1*1.05f, r = 0.26f;
            int sg = 12;
            for (int s = 0; s < sg; s++) {
                float a0=(float)(2*M_PI*s/sg), a1=(float)(2*M_PI*(s+1)/sg);
                float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
                mesh_quad(&P_port_in.mesh, x0+r*c0,y0,z+r*s0, x0+r*c1,y0,z+r*s1,
                          x1+r*c1,y1,z+r*s1, x1+r*c0,y1,z+r*s0);
            }
        }
        p_box(&P_port_in.mesh, -VALVE_X-1.05f, HEAD_TOP_Y+1.22f, z, 0.40f, 0.07f, 0.40f);
        /* exhaust header */
        for (int k = 0; k < 8; k++) {
            float t0 = k/8.0f, t1 = (k+1)/8.0f;
            float y0 = HEAD_TOP_Y - 0.05f + t0*1.25f, y1 = HEAD_TOP_Y - 0.05f + t1*1.25f;
            float x0 = VALVE_X + t0*1.05f, x1 = VALVE_X + t1*1.05f, r = 0.24f;
            int sg = 12;
            for (int s = 0; s < sg; s++) {
                float a0=(float)(2*M_PI*s/sg), a1=(float)(2*M_PI*(s+1)/sg);
                float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
                mesh_quad(&P_port_ex.mesh, x0+r*c0,y0,z+r*s0, x1+r*c0,y1,z+r*s0,
                          x1+r*c1,y1,z+r*s1, x0+r*c1,y0,z+r*s1);
            }
        }
        p_box(&P_port_ex.mesh, VALVE_X+1.05f, HEAD_TOP_Y+1.22f, z, 0.40f, 0.07f, 0.40f);
    }
    part_set(&P_port_in, MAT_port_in, 0.30f, 0.0f, 0);
    part_set(&P_port_ex, MAT_port_ex, 0.30f, 0.0f, 0);

    /* flow tubes built at z=0, instanced per cylinder */
    for (int k = 0; k < 6; k++) {
        float t0=k/6.0f, t1=(k+1)/6.0f;
        float y0=HEAD_TOP_Y+0.15f+t0*1.00f, y1=HEAD_TOP_Y+0.15f+t1*1.00f;
        float x0=-VALVE_X-t0*0.85f, x1=-VALVE_X-t1*0.85f, r=0.18f;
        int sg=10;
        for (int s=0;s<sg;s++){ float a0=(float)(2*M_PI*s/sg),a1=(float)(2*M_PI*(s+1)/sg);
            float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
            mesh_quad(&P_flow_in.mesh, x0+r*c0,y0,r*s0, x0+r*c1,y0,r*s1, x1+r*c1,y1,r*s1, x1+r*c0,y1,r*s0); }
    }
    for (int k = 0; k < 6; k++) {
        float t0=k/6.0f, t1=(k+1)/6.0f;
        float y0=HEAD_TOP_Y+0.15f+t0*1.00f, y1=HEAD_TOP_Y+0.15f+t1*1.00f;
        float x0=VALVE_X+t0*0.85f, x1=VALVE_X+t1*0.85f, r=0.16f;
        int sg=10;
        for (int s=0;s<sg;s++){ float a0=(float)(2*M_PI*s/sg),a1=(float)(2*M_PI*(s+1)/sg);
            float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
            mesh_quad(&P_flow_ex.mesh, x0+r*c0,y0,r*s0, x1+r*c0,y1,r*s0, x1+r*c1,y1,r*s1, x0+r*c1,y0,r*s1); }
    }
    part_set(&P_flow_in, MAT_flow_in, 0.10f, 0.40f, 1);
    part_set(&P_flow_ex, MAT_flow_ex, 0.10f, 0.30f, 1);
}

/* ---------------- per-cylinder template builders --------------------------- */

static void build_piston(void)
{
    Mesh *m = &P_piston.mesh; mesh_init(m);
    p_cyl_y(m, 0, 0, -0.70f, 0.60f, BORE_R - 0.03f, 28, 0);
    p_disc_y(m, 0, 0.60f, 0, BORE_R - 0.03f, 0, 28, 0);
    p_disc_y(m, 0, -0.70f, 0, BORE_R - 0.03f, 0.34f, 28, 1);
    for (int i = 0; i < 3; i++)
        p_torus_y(m, 0, 0.36f - 0.17f*i, 0, BORE_R - 0.03f, 0.045f, 22, 6);
    p_cyl_z(m, 0, 0, -0.55f, 0.55f, 0.19f, 14, 0);   /* wrist pin bosses */
    p_cyl_z(m, 0, 0, -0.72f, 0.72f, 0.14f, 14, 3);   /* wrist pin */
    part_set(&P_piston, MAT_piston, 0.40f, 0.0f, 1);
}

static void build_rod(void)
{
    Mesh *m = &P_rod.mesh; mesh_init(m);
    p_cyl_z(m, 0, 0, -0.30f, 0.30f, 0.55f, 22, 0);
    p_torus_y(m, 0, 0, -0.30f, 0.55f, 0.09f, 18, 6);
    p_torus_y(m, 0, 0,  0.30f, 0.55f, 0.09f, 18, 6);
    p_disc_y(m, 0, 0, -0.30f, 0.55f, 0.36f, 22, 1);
    p_disc_y(m, 0, 0,  0.30f, 0.55f, 0.36f, 22, 0);
    p_cyl_z(m, 0, ROD_L, -0.22f, 0.22f, 0.28f, 18, 0);
    p_torus_y(m, 0, ROD_L, -0.22f, 0.28f, 0.07f, 16, 6);
    p_torus_y(m, 0, ROD_L,  0.22f, 0.28f, 0.07f, 16, 6);
    p_disc_y(m, 0, ROD_L, -0.22f, 0.28f, 0.17f, 18, 1);
    p_disc_y(m, 0, ROD_L,  0.22f, 0.28f, 0.17f, 18, 0);
    for (int i = 0; i < 12; i++) {
        float t0 = i/12.0f, t1 = (i+1)/12.0f;
        float y0 = 0.45f + t0*(ROD_L-0.90f), y1 = 0.45f + t1*(ROD_L-0.90f);
        float w0 = 0.34f - 0.09f*t0, h0 = 0.13f - 0.04f*t0;
        p_box(m, 0, (y0+y1)*0.5f, 0, w0*0.45f, (y1-y0)*0.5f, h0);
        p_box(m, 0, (y0+y1)*0.5f,  h0+0.04f, w0*0.5f, (y1-y0)*0.5f, 0.04f);
        p_box(m, 0, (y0+y1)*0.5f, -h0-0.04f, w0*0.5f, (y1-y0)*0.5f, 0.04f);
    }
    part_set(&P_rod, MAT_rod, 0.45f, 0.0f, 1);
}

/* Valve + spring retainer + bucket tappet, in valve-local coordinates.  The
 * valve origin sits at the seat (world VALVE_SEAT_Y - lift), so the bucket top
 * face at local 1.57f reaches world CAM_Y - CAM_BASE_R when closed: the cam
 * base circle rests on the bucket and the nose pushes it down by MAX_LIFT. */
static void build_valve(Part *p, const GLfloat color[4])
{
    Mesh *m = &p->mesh; mesh_init(m);
    p_cone_y(m, 0, 0, 0.0f, VALVE_HEAD_T, VALVE_HEAD_R, VALVE_HEAD_R*0.55f, 22);
    p_disc_y(m, 0, 0.0f, 0, VALVE_HEAD_R, 0, 22, 1);
    p_cyl_y(m, 0, 0, VALVE_HEAD_T, 1.55f, VALVE_STEM_R, 10, 0);   /* stem */
    p_cyl_y(m, 0, 0, 1.18f, 1.30f, 0.30f, 16, 3);                 /* spring retainer */
    p_tube_y(m, 0, 0, 1.32f, 1.57f, 0.26f, 0.20f, 16);            /* bucket tappet cup */
    p_disc_y(m, 0, 1.57f, 0, 0.26f, 0, 16, 0);                    /* bucket top / cam pad */
    part_set(p, color, 0.65f, 0.0f, 1);
}

static void build_springs(void)
{
    /* Springs seat on the head at SPRING_SEAT_Y and push up on the retainer
     * (world VALVE_SEAT_Y + 1.18 = SPRING_SEAT_Y + 0.58), so they compress by
     * exactly the valve lift and stay clear of the camshaft. */
    mesh_init(&P_spring_in.mesh);
    p_spring_y(&P_spring_in.mesh, 0, 0, SPRING_SEAT_Y, SPRING_SEAT_Y + 0.58f, 0.21f, 0.042f, 6, 16, 6);
    part_set(&P_spring_in, MAT_spring, 0.50f, 0.0f, 1);
    mesh_init(&P_spring_ex.mesh);
    p_spring_y(&P_spring_ex.mesh, 0, 0, SPRING_SEAT_Y, SPRING_SEAT_Y + 0.58f, 0.21f, 0.042f, 6, 16, 6);
    part_set(&P_spring_ex, MAT_spring, 0.50f, 0.0f, 1);
}

static void build_charge_flame(void)
{
    /* The mixture AND the combustion are drawn as GAS that conforms to the real
     * cylinder volume - never a sphere.  Two pieces per cylinder:
     *   P_charge      : a cylindrical column in the bore, scaled each frame from
     *                   the moving piston crown up to the deck (DECK_Y)
     *   P_chamber_gas : the fixed domed combustion-chamber volume above the deck
     * Together they fill from the crown up into the domed chamber roof. */
    mesh_init(&P_charge.mesh);
    p_cyl_y(&P_charge.mesh, 0, 0, -0.5f, 0.5f, BORE_R - 0.06f, 22, 3);
    part_set(&P_charge, charge_color, 0.05f, 0.35f, 1);

    /* fixed domed chamber volume (template at z=0, instanced per cylinder) */
    mesh_init(&P_chamber_gas.mesh);
    p_chamber_dome(&P_chamber_gas.mesh, 0, DECK_Y, CHAMBER_CEIL, BORE_R - 0.06f, 26, 6);
    p_disc_y(&P_chamber_gas.mesh, 0, DECK_Y, 0, BORE_R - 0.06f, 0, 26, 1);
    part_set(&P_chamber_gas, charge_color, 0.05f, 0.35f, 1);
}

/* External timing drive at the front (+Z) of the engine: a small crank sprocket
 * and two camshaft sprockets at twice the diameter (2:1 reduction, so the cams
 * turn at half crank speed) linked by a toothed belt round a tensioner idler. */
static void build_timing(void)
{
    mesh_init(&P_spr_crank.mesh);
    p_sprocket_z(&P_spr_crank.mesh, 0, 0, TIM_Z0, TIM_Z1, SPR_CRANK_R, 16, 0.13f);
    part_set(&P_spr_crank, MAT_sprocket, 0.55f, 0.0f, 1);

    mesh_init(&P_spr_cam_in.mesh);
    p_sprocket_z(&P_spr_cam_in.mesh, 0, 0, TIM_Z0, TIM_Z1, SPR_CAM_R, 32, 0.20f);
    part_set(&P_spr_cam_in, MAT_sprocket, 0.55f, 0.0f, 1);

    mesh_init(&P_spr_cam_ex.mesh);
    p_sprocket_z(&P_spr_cam_ex.mesh, 0, 0, TIM_Z0, TIM_Z1, SPR_CAM_R, 32, 0.20f);
    part_set(&P_spr_cam_ex, MAT_sprocket, 0.55f, 0.0f, 1);

    mesh_init(&P_tensioner.mesh);
    p_sprocket_z(&P_tensioner.mesh, 0, 0, TIM_Z0, TIM_Z1, TENS_R, 16, 0.12f);
    part_set(&P_tensioner, MAT_sprocket, 0.45f, 0.0f, 1);

    /* belt looped CCW: crank -> tensioner -> exhaust cam -> intake cam */
    static const float bx[4] = { 0.0f, TENS_X,  VALVE_X, -VALVE_X };
    static const float by[4] = { 0.0f, TENS_Y,  CAM_Y,     CAM_Y   };
    static const float br[4] = { SPR_CRANK_R, TENS_R, SPR_CAM_R, SPR_CAM_R };
    mesh_init(&P_belt.mesh);
    build_belt(&P_belt.mesh, bx, by, br, 4, TIM_ZC, 0.20f, 0.09f);
    part_set(&P_belt, MAT_belt, 0.15f, 0.0f, 1);
}

static void build_all_parts(void)
{
    build_block();
    build_head();
    build_crankcase();
    build_crank();
    build_cams();
    build_plugs();
    build_ports();
    build_piston();
    build_rod();
    build_valve(&P_valve_in, MAT_valve_in);
    build_valve(&P_valve_ex, MAT_valve_ex);
    build_springs();
    build_charge_flame();
    build_timing();

    Part *all[] = {
        &P_block,&P_head,&P_camcover,&P_crankcase,&P_liner,&P_baseplate,&P_boltset,
        &P_crank,&P_cam_in,&P_cam_ex,&P_plug_body,&P_plug_tip,&P_port_in,&P_port_ex,
        &P_spr_crank,&P_spr_cam_in,&P_spr_cam_ex,&P_tensioner,&P_belt,
        &P_piston,&P_rod,&P_valve_in,&P_valve_ex,&P_spring_in,&P_spring_ex,
        &P_flow_in,&P_flow_ex,&P_charge,&P_chamber_gas
    };
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++)
        mesh_upload(&all[i]->mesh);
}


/* ---------------- shaders --------------------------------------------------- */
static const char *engine_vs =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uNormalMat;\n"
    "uniform vec4 uMaterial;\n"
    "uniform vec4 uLightEye;\n"
    "uniform vec4 uShade;\n"
    "uniform float uEmissive;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vec3 N = normalize((uNormalMat * vec4(aNormal, 0.0)).xyz);\n"
    "    vec3 L = normalize(uLightEye.xyz);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    vec3 R = reflect(-L, N);\n"
    "    float rd = max(dot(R, vec3(0.0, 0.0, 1.0)), 0.0);\n"
    "    float rd2 = rd * rd;\n"
    "    float rd4 = rd2 * rd2;\n"
    "    float rd8 = rd4 * rd4;\n"
    "    float rd16 = rd8 * rd8;\n"
    "    float spec = rd16 * uShade.z;\n"
    "    vec3 col = uMaterial.rgb * (uShade.x + uShade.y * diff + uEmissive) + vec3(spec);\n"
    "    vColor = vec4(clamp(col, 0.0, 1.0), uMaterial.a);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char *engine_fs =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

static GLuint program = 0;
static GLint loc_mvp=-1, loc_normal=-1, loc_material=-1, loc_light=-1, loc_shade=-1, loc_emissive=-1;

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        klog("engine: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* ---------------- animation state ------------------------------------------ */
static GLfloat crank_angle = 0.0f;     /* 0..720 */
static GLfloat camera_angle = 25.0f;
static GLfloat camera_elev = 10.0f;
static GLfloat camera_dist = 20.0f;

/* Per-cylinder cycle phase (0..720) from the shared crank angle. */
static float cyl_phase(int i, float crank)
{
    float p = fmodf(crank - cyl_off[i], 720.0f);
    if (p < 0.0f) p += 720.0f;
    return p;
}

static float valve_lift(float phase, float peak, float dur, float max_lift)
{
    float d = phase - peak;
    while (d >  360.0f) d -= 720.0f;
    while (d < -360.0f) d += 720.0f;
    float half = dur * 0.5f;
    if (fabsf(d) >= half) return 0.0f;
    float t = d / half, c = cosf(t * (float)M_PI * 0.5f);
    return max_lift * c * c;
}

static float burn_intensity(float phase)
{
    if (phase < SPARK_START) return 0.0f;
    if (phase <= SPARK_END) { float t = (phase-SPARK_START)/(SPARK_END-SPARK_START); return t*t; }
    if (phase <= BURN_END)  { float t = (phase-SPARK_END)/(BURN_END-SPARK_END); float f=1.0f-t; return f*f; }
    return 0.0f;
}

static float spark_intensity(float phase)
{
    if (phase < SPARK_START || phase > SPARK_END) return 0.0f;
    float t = (phase-SPARK_START)/(SPARK_END-SPARK_START);
    return sinf(t * (float)M_PI);
}

/* Mixture colour by stroke for one cylinder's phase. */
static void charge_color_for(float phase, float burn, GLfloat out[4])
{
    if (burn > 0.01f) {
        out[0]=1.00f; out[1]=0.40f+0.20f*(1.0f-burn); out[2]=0.12f; out[3]=0.30f*(1.0f-burn*0.6f);
    } else if (phase < 180.0f) {
        float t = phase/180.0f;
        out[0]=0.30f; out[1]=0.55f; out[2]=0.95f; out[3]=0.16f+0.20f*t;
    } else if (phase < 360.0f) {
        float t = (phase-180.0f)/180.0f;
        out[0]=0.30f+0.25f*t; out[1]=0.55f-0.10f*t; out[2]=0.95f-0.30f*t; out[3]=0.36f+0.16f*t;
    } else if (phase < 540.0f) {
        float t = (phase-360.0f)/180.0f;
        out[0]=0.75f-0.20f*t; out[1]=0.55f-0.10f*t; out[2]=0.35f; out[3]=0.34f-0.14f*t;
    } else {
        float t = (phase-540.0f)/180.0f;
        out[0]=0.55f; out[1]=0.55f; out[2]=0.58f; out[3]=0.28f*(1.0f-t);
    }
}

/* ---------------- drawing -------------------------------------------------- */
static void bind_part(Part *p)
{
    glBindBuffer(GL_ARRAY_BUFFER, p->mesh.vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(GLfloat), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(GLfloat), (void*)(3*sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
}

static void draw_part(Part *p, const GLfloat model[16])
{
    if (p->mesh.vbo == 0 || p->mesh.nverts <= 0) return;
    GLfloat mv[16], nm[16], mvp[16];
    memcpy(mv, view_matrix, sizeof(mv));
    m_mul(mv, model);
    memcpy(mvp, projection_matrix, sizeof(mvp));
    m_mul(mvp, mv);
    m_normal_from_mv(nm, mv);
    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(loc_normal, 1, GL_FALSE, nm);
    glUniform4f(loc_material, p->color[0], p->color[1], p->color[2], p->color[3]);
    glUniform1f(loc_emissive, p->emissive);
    bind_part(p);
    glDrawArrays(GL_TRIANGLES, 0, p->mesh.nverts);
}

static void draw_at(Part *p, float tx,float ty,float tz, float rot,float rx,float ry,float rz,
                    float sx,float sy,float sz)
{
    GLfloat model[16]; m_ident(model);
    m_translate(model, tx, ty, tz);
    if (rot != 0.0f) m_rotate(model, rot, rx, ry, rz);
    if (sx!=1.0f || sy!=1.0f || sz!=1.0f) m_scale(model, sx, sy, sz);
    draw_part(p, model);
}

static void ident_model(GLfloat *m) { m_ident(m); }

/* ---------------- GL init -------------------------------------------------- */
static void init_gl(void)
{
    GLuint vs, fs; GLint ok = 0;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    vs = compile_shader(GL_VERTEX_SHADER, engine_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, engine_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aNormal");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        klog("engine: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs); glDeleteShader(fs);
    glUseProgram(program);

    loc_mvp      = glGetUniformLocation(program, "uMVP");
    loc_normal   = glGetUniformLocation(program, "uNormalMat");
    loc_material = glGetUniformLocation(program, "uMaterial");
    loc_light    = glGetUniformLocation(program, "uLightEye");
    loc_shade    = glGetUniformLocation(program, "uShade");
    loc_emissive = glGetUniformLocation(program, "uEmissive");

    float ll = sqrtf(light_world[0]*light_world[0]+light_world[1]*light_world[1]+light_world[2]*light_world[2]);
    if (ll > 0) { light_world[0]/=ll; light_world[1]/=ll; light_world[2]/=ll; }
    glUniform4f(loc_shade, 0.24f, 0.85f, 0.55f, 0.0f);

    m_perspective(projection_matrix, 50.0f, (GLfloat)win_width/(GLfloat)win_height, 0.5f, 300.0f);
    glViewport(0, 0, win_width, win_height);

    build_all_parts();
    hud_init();
}

static void reshape(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    win_width = w; win_height = h;
    glViewport(0, 0, w, h);
    m_perspective(projection_matrix, 50.0f, (GLfloat)w/(GLfloat)h, 0.5f, 300.0f);
}

static void compute_camera(void)
{
    float ca = camera_angle*(float)M_PI/180.0f, ce = camera_elev*(float)M_PI/180.0f;
    float ex = sinf(ca)*cosf(ce)*camera_dist;
    float ey = sinf(ce)*camera_dist + 3.4f;
    float ez = cosf(ca)*cosf(ce)*camera_dist;
    eye_world[0]=ex; eye_world[1]=ey; eye_world[2]=ez;
    m_look_at(view_matrix, ex,ey,ez, 0.0f,3.2f,0.0f, 0.0f,1.0f,0.0f);
    GLfloat ld[3];
    m_xform_dir(view_matrix, light_world, ld);
    glUniform4f(loc_light, ld[0], ld[1], ld[2], 0.0f);
}

/* ---------------- render passes -------------------------------------------- */

static void draw_opaque(void)
{
    GLfloat model[16];

    /* crankshaft (one rigid body) */
    ident_model(model);
    m_rotate(model, -crank_angle, 0, 0, 1);
    draw_part(&P_crank, model);

    /* camshafts (one rigid body each, half crank speed) */
    float cam_rot = crank_angle * 0.5f;
    draw_at(&P_cam_in,  -VALVE_X, CAM_Y, 0, cam_rot, 0,0,1, 1,1,1);
    draw_at(&P_cam_ex,   VALVE_X, CAM_Y, 0, cam_rot, 0,0,1, 1,1,1);

    /* external timing drive: sprockets locked to their shafts + static belt */
    draw_at(&P_spr_crank,  0, 0, 0,            -crank_angle,       0,0,1, 1,1,1);
    draw_at(&P_spr_cam_in, -VALVE_X, CAM_Y, 0,  cam_rot,           0,0,1, 1,1,1);
    draw_at(&P_spr_cam_ex,  VALVE_X, CAM_Y, 0,  cam_rot,           0,0,1, 1,1,1);
    draw_at(&P_tensioner,   TENS_X, TENS_Y, 0, -crank_angle*1.6f,  0,0,1, 1,1,1);
    ident_model(model);
    draw_part(&P_belt, model);

    /* static hardware */
    ident_model(model);
    draw_part(&P_plug_body, model);
    draw_part(&P_plug_tip, model);
    draw_part(&P_boltset, model);
    draw_part(&P_baseplate, model);
    draw_part(&P_port_in, model);
    draw_part(&P_port_ex, model);

    /* per-cylinder moving parts */
    for (int i = 0; i < NCYL; i++) {
        float z = cyl_z[i];
        float pin_ang = (crank_angle + cyl_pin[i]) * (float)M_PI / 180.0f;
        float sa = sinf(pin_ang), ca = cosf(pin_ang);
        float pin_y = CRANK_R*ca + sqrtf(ROD_L*ROD_L - CRANK_R*CRANK_R*sa*sa);

        /* piston */
        ident_model(model);
        m_translate(model, 0, pin_y, z);
        draw_part(&P_piston, model);

        /* connecting rod: big end on the crank pin, small end on the wrist pin */
        float cpx = CRANK_R*sa, cpy = CRANK_R*ca;
        float rod_ang = asinf(CRANK_R*sa/ROD_L)*180.0f/(float)M_PI;
        ident_model(model);
        m_translate(model, cpx, cpy, z);
        m_rotate(model, rod_ang, 0, 0, 1);
        draw_part(&P_rod, model);

        /* valves + springs */
        float ph = cyl_phase(i, crank_angle);
        float li = valve_lift(ph, INTAKE_PEAK,  VALVE_DUR, MAX_LIFT);
        float le = valve_lift(ph, EXHAUST_PEAK, VALVE_DUR, MAX_LIFT);
        draw_at(&P_valve_in, -VALVE_X, VALVE_SEAT_Y - li, z, 0,0,0,0, 1,1,1);
        draw_at(&P_valve_ex,  VALVE_X, VALVE_SEAT_Y - le, z, 0,0,0,0, 1,1,1);
        {
            float h0 = 0.58f, ybot = SPRING_SEAT_Y;
            ident_model(model);
            m_translate(model, -VALVE_X, ybot, z);
            m_scale(model, 1, (h0-li)/h0, 1);
            m_translate(model, 0, -ybot, 0);
            draw_part(&P_spring_in, model);
            ident_model(model);
            m_translate(model, VALVE_X, ybot, z);
            m_scale(model, 1, (h0-le)/h0, 1);
            m_translate(model, 0, -ybot, 0);
            draw_part(&P_spring_ex, model);
        }
    }
}

/* Translucent gas (one per cylinder): a bore column from the crown to the deck
 * plus the fixed domed chamber above it, drawn with blending so the piston
 * crown stays visible through the mixture. */
static void draw_charges(void)
{
    GLfloat model[16];
    for (int i = 0; i < NCYL; i++) {
        float pin_ang = (crank_angle + cyl_pin[i])*(float)M_PI/180.0f;
        float sa = sinf(pin_ang), ca = cosf(pin_ang);
        float pin_y = CRANK_R*ca + sqrtf(ROD_L*ROD_L - CRANK_R*CRANK_R*sa*sa);
        float crown = pin_y + 0.60f;
        float h = DECK_Y - crown;              /* bore column: crown -> deck */
        if (h < 0.05f) h = 0.05f;
        float ph = cyl_phase(i, crank_angle);
        float burn = burn_intensity(ph);
        charge_color_for(ph, burn, charge_color);
        float emis = 0.22f + 0.45f*charge_color[3];
        P_charge.color[0]=charge_color[0]; P_charge.color[1]=charge_color[1];
        P_charge.color[2]=charge_color[2]; P_charge.color[3]=charge_color[3];
        P_charge.emissive = emis;
        P_chamber_gas.color[0]=charge_color[0]; P_chamber_gas.color[1]=charge_color[1];
        P_chamber_gas.color[2]=charge_color[2]; P_chamber_gas.color[3]=charge_color[3];
        P_chamber_gas.emissive = emis;
        ident_model(model);
        m_translate(model, 0, crown + h*0.5f, cyl_z[i]);
        m_scale(model, 1, h, 1);
        draw_part(&P_charge, model);
        ident_model(model);
        m_translate(model, 0, 0, cyl_z[i]);
        draw_part(&P_chamber_gas, model);
    }
}

static void draw_shells(void)
{
    GLfloat model[16]; ident_model(model);
    /* far side then near side for plausible transparency sorting */
    glCullFace(GL_FRONT);
    draw_part(&P_crankcase, model);
    draw_part(&P_block, model);
    draw_part(&P_liner, model);
    draw_part(&P_head, model);
    draw_part(&P_camcover, model);
    glCullFace(GL_BACK);
    draw_part(&P_crankcase, model);
    draw_part(&P_block, model);
    draw_part(&P_liner, model);
    draw_part(&P_head, model);
    draw_part(&P_camcover, model);
}

static void draw_flows(void)
{
    for (int i = 0; i < NCYL; i++) {
        float ph = cyl_phase(i, crank_angle);
        float li = valve_lift(ph, INTAKE_PEAK,  VALVE_DUR, MAX_LIFT);
        float le = valve_lift(ph, EXHAUST_PEAK, VALVE_DUR, MAX_LIFT);
        GLfloat model[16]; ident_model(model);
        m_translate(model, 0, 0, cyl_z[i]);
        if (li > 0.01f) {
            P_flow_in.color[3] = 0.55f*(li/MAX_LIFT);
            P_flow_in.emissive = 0.45f*(li/MAX_LIFT);
            draw_part(&P_flow_in, model);
        }
        if (le > 0.01f) {
            P_flow_ex.color[3] = 0.55f*(le/MAX_LIFT);
            P_flow_ex.emissive = 0.35f*(le/MAX_LIFT);
            draw_part(&P_flow_ex, model);
        }
    }
}

/* Combustion drawn as additive hot GAS over the same two volumes (bore column +
 * domed chamber), so the explosion is always inside the cylinder and conforms
 * to the chamber roof - never a free-floating ball.  It flashes white-hot at
 * the spark and cools to orange as the burn expands. */
static void draw_combustion(void)
{
    for (int i = 0; i < NCYL; i++) {
        float ph = cyl_phase(i, crank_angle);
        float burn = burn_intensity(ph);
        float sp = spark_intensity(ph);
        float glow = (burn > sp) ? burn : sp;
        if (glow <= 0.001f) continue;
        float pin_ang = (crank_angle + cyl_pin[i])*(float)M_PI/180.0f;
        float sa = sinf(pin_ang), ca = cosf(pin_ang);
        float pin_y = CRANK_R*ca + sqrtf(ROD_L*ROD_L - CRANK_R*CRANK_R*sa*sa);
        float crown = pin_y + 0.60f;
        float h = DECK_Y - crown;
        if (h < 0.05f) h = 0.05f;
        GLfloat c1 = 0.55f + 0.40f*sp, c2 = 0.18f + 0.55f*sp, c3 = 0.90f*glow;
        P_charge.color[0]=1.00f; P_charge.color[1]=c1;
        P_charge.color[2]=c2; P_charge.color[3]=c3;
        P_charge.emissive = 1.00f;
        P_chamber_gas.color[0]=1.00f; P_chamber_gas.color[1]=c1;
        P_chamber_gas.color[2]=c2; P_chamber_gas.color[3]=c3;
        P_chamber_gas.emissive = 1.00f;
        GLfloat model[16]; ident_model(model);
        m_translate(model, 0, crown + h*0.5f, cyl_z[i]);
        m_scale(model, 1, h, 1);
        draw_part(&P_charge, model);
        ident_model(model);
        m_translate(model, 0, 0, cyl_z[i]);
        draw_part(&P_chamber_gas, model);
    }
}

/* ---------------- frame ---------------------------------------------------- */
static int fps=0, count=0;
static uint32_t last_tic=0, last_frame_tic=0;

static void update_fps(void)
{
    uint32_t low; kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) { last_tic = low; fps = count; count = 0; }
    count++;
}

static const char *stroke_name(float phase)
{
    if (phase < 180.0f) return "INTAKE";
    if (phase < 360.0f) return "COMPR";
    if (phase < 540.0f) return "POWER";
    return "EXHAUST";
}

static void render_frame(void)
{
    EGLint w=0,h=0; uint32_t cur; float dt;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w>0 && h>0 && (w!=win_width || h!=win_height)) reshape(w,h);

    kernel_tic32(NULL, NULL, &cur);
    if (last_frame_tic == 0) last_frame_tic = cur;
    dt = (cur - last_frame_tic)/1000000.0f;
    last_frame_tic = cur;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    crank_angle += CRANK_DPS*dt;
    while (crank_angle >= 720.0f) crank_angle -= 720.0f;
    camera_angle += CAM_ORBIT_DPS*dt;
    if (camera_angle >= 360.0f) camera_angle -= 360.0f;

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    compute_camera();

    /* pass 1: opaque internals */
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    draw_opaque();

    /* pass 2: translucent (charge, shells, port flow) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    draw_charges();
    draw_shells();
    draw_flows();

    /* pass 3: additive emissive (flame + spark) */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    draw_combustion();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);

    {
        char hud[128];
        int gpu = glGpuActiveEWOK();
        float ph1 = cyl_phase(0, crank_angle);
        snprintf(hud, sizeof(hud), "FPS:%d %s  inline-4  1-3-4-2  crank:%3d  cyl1:%s",
                 fps, gpu ? "GPU" : "CPU", (int)crank_angle, stroke_name(ph1));
        hud_draw(program, win_width, win_height, 8, 8, hud);
    }
    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

/* ---------------- window / EGL --------------------------------------------- */
static void on_event(xwin_t* xwin, xevent_t* ev)
{
    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        if (key == 27) xwin_close(xwin);
        else if (key == ' ') camera_dist = (camera_dist == 20.0f) ? 13.0f : 20.0f;  /* zoom */
    }
}

static void loop(void* p) { (void)p; render_frame(); usleep(1000); }

static int init_egl(xwin_t* xwin)
{
    EGLint major, minor, ncfg = 0;
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg = NULL;
    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) return 0;
    if (!eglInitialize(egl_dpy, &major, &minor)) return 0;
    if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) return 0;
    egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) return 0;
    egl_surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)xwin, NULL);
    if (egl_surf == EGL_NO_SURFACE) return 0;
    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) return 0;
    return 1;
}

int main(int argc, char* argv[])
{
    x_t x; xwin_t* xwin;
    (void)argc; (void)argv;
    x_init(&x, NULL);
    x.on_loop = loop;
    xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Inline-4 Engine", XWIN_STYLE_NORMAL);
    if (!xwin) { printf("Failed to open window\n"); return 1; }
    xwin->on_event = on_event;
    if (!init_egl(xwin)) { printf("Failed to initialize EGL\n"); return 1; }
    init_gl();
    xwin_set_visible(xwin, true);
    x_run(&x, xwin);
    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);
    return 0;
}
