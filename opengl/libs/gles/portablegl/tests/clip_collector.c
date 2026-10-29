/* Host regression: clang -O2 -fgnu89-inline clip_collector.c -lm -pthread -o /tmp/clip_collector */
#include "../src/portablegl.c"
#include <assert.h>

static vec4 captured[8192];
static unsigned captured_count, early_count;
static int consume;
static int collect(const glVertex *a, const glVertex *b, const glVertex *d, void *user)
{
    (void)user;
    const glVertex *v[] = {a,b,d};
    for (int i=0; i<3; ++i) {
        assert(captured_count < sizeof(captured)/sizeof(captured[0]));
        captured[captured_count++] = v[i]->clip_space;
        early_count += !v[i]->screen_space_valid;
    }
    return consume;
}
static void vs(float *out, vec4 *attr, Shader_Builtins *b, void *user)
{ (void)out; (void)user; b->gl_Position = attr[0]; }
static void executor(int worker, float *out, vec4 *attr, Shader_Builtins *b, void *user)
{ (void)worker; vs(out,attr,b,user); }
static void fs(float *in, Shader_Builtins *b, void *user)
{ (void)in; (void)user; b->gl_FragColor = make_v4(0.2f,0.7f,0.4f,1.0f); }
int main(void)
{
    glContext ctx;
    pix_t *fb = NULL, reference[64*64];
    vec4 positions[] = {{-0.8f,-0.8f,0,1},{0.8f,-0.8f,0,1},{0,0.8f,0,1},
                        {-1.8f,-0.7f,-1.4f,1},{0,0.8f,0,1},{0.8f,-0.8f,0,1}};
    GLushort indices[132];
    for (int i=0;i<132;i++) indices[i]=i%6;
    assert(init_glContext(&ctx,&fb,64,64));
    GLuint program=pglCreateProgram(vs,fs,0,NULL,GL_FALSE), vbo,ibo;
    pgl_UseProgram(program);
    pgl_GenBuffers(1,&vbo); pgl_BindBuffer(GL_ARRAY_BUFFER,vbo);
    pgl_BufferData(GL_ARRAY_BUFFER,sizeof positions,positions,GL_STATIC_DRAW);
    pgl_VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,0,0); pgl_EnableVertexAttribArray(0);
    pgl_GenBuffers(1,&ibo); pgl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    pgl_BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);
    setenv("PGL_VERTEX_THREADS","1",1);
    for(int workers=0;workers<2;workers++) for(int cull=0;cull<3;cull++)
    for(int handled=0;handled<2;handled++) for(int viewport=0;viewport<2;viewport++) {
        vec4 expected[8192]; unsigned expected_count;
        if(cull) {pgl_Enable(GL_CULL_FACE);pgl_CullFace(cull==1?GL_BACK:GL_FRONT);}
        else pgl_Disable(GL_CULL_FACE);
        pgl_Viewport(viewport*7,viewport*3,64-viewport*10,64-viewport*6);
        pglSetVertexExecutor(workers?executor:NULL,NULL);
        consume=handled;
        for(int clip_only=0;clip_only<2;clip_only++) {
            pglSetTriangleCallback(NULL,NULL);
            pgl_ClearColor(0,0,0,1); pgl_Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            captured_count=early_count=0;
            if(clip_only) pglSetClipTriangleCallback(collect,NULL);
            else pglSetTriangleCallback(collect,NULL);
            pgl_DrawElements(GL_TRIANGLES,132,GL_UNSIGNED_SHORT,0);
            assert(pgl_GetError()==GL_NO_ERROR);
            if(!clip_only) {
                expected_count=captured_count;
                memcpy(expected,captured,captured_count*sizeof(vec4));
                memcpy(reference,fb,sizeof reference);
            } else {
                assert(captured_count==expected_count && captured_count>0);
                assert(!memcmp(expected,captured,captured_count*sizeof(vec4)));
                assert(!memcmp(reference,fb,sizeof reference));
                if(!cull) assert(early_count>0);
            }
        }
    }
    pglSetTriangleCallback(NULL,NULL);
    pglStopVertexWorkers();
    free_glContext(&ctx);
    puts("clip collector: clipped geometry, culling, viewport changes, indexed cache and CPU replay match");
    return 0;
}
