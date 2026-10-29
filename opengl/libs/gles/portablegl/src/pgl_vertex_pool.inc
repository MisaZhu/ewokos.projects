/* Persistent workers only parallelize vertex evaluation. Rasterization and GL
 * state changes stay on the calling thread. No interpreter arena is shared. */
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
/* q8b has eight application cores. The calling render thread participates in
 * the vertex range, so eight total workers means seven persistent threads and
 * no idle application core during the vertex stage. PGL_VERTEX_THREADS can
 * lower this for CPU-only workloads. */
#define PGL_VERTEX_WORKERS 8
static pgl_vertex_executor pgl_vertex_exec;
static void *pgl_vertex_user;
static struct {
    pthread_t thread;
    sem_t start, done;
    int index;
} pgl_vworkers[PGL_VERTEX_WORKERS-1];
static int pgl_vworker_count, pgl_vpool_tried, pgl_vpool_stop;
static GLuint *pgl_vcache_words;
static glVertex *pgl_vcache_vertices;
static float *pgl_vcache_outputs;
static size_t pgl_vcache_wordcap,pgl_vcache_vertcap,pgl_vcache_floatcap;
static int pgl_vertex_min_cached = -1;

/* Indexed draws keep the established 64-vertex cutoff because index reuse
 * makes their worker setup cheap at that size. Non-indexed RenderWare draws
 * have no reuse to amortize interpreter cost, so admit them at 128 vertices.
 * PGL_VERTEX_MIN is a process-wide board-side A/B tuning knob for the latter. */
static int pgl_vertex_parallel_min(GLenum type)
{
    const char *env;
    int n;
    if(type) return 64;
    if(pgl_vertex_min_cached >= 0) return pgl_vertex_min_cached;
    env=getenv("PGL_VERTEX_MIN");
    n=env ? atoi(env) : 128;
    if(n<1) n=1;
    pgl_vertex_min_cached=n;
    return n;
}
static struct {
    glVertex_Attrib *attr;
    int *enabled, n_enabled;
    const void *indices;
    int count;
    GLenum type;
    int tasks;
    glVertex *vertices;
    float *outputs;
} pgl_vjob;
PGLDEF void pglSetVertexExecutor(pgl_vertex_executor fn,void *user)
{ pgl_vertex_exec=fn; pgl_vertex_user=user; }
PGLDEF int pglVertexWorkerCount(void)
{ return pgl_vworker_count ? pgl_vworker_count + 1 : 0; }
static void pgl_vertex_range(int worker)
{
    vec4 attrs[GL_MAX_VERTEX_ATTRIBS];
    Shader_Builtins builtins=c->builtins;
    int total=pgl_vjob.tasks;
    int begin=pgl_vjob.count*worker/total,end=pgl_vjob.count*(worker+1)/total;
    memcpy(attrs,c->vertex_attribs_vs,sizeof attrs);
    for(int vert=begin;vert<end;vert++) {
        int i;
        if(!pgl_vjob.type) i=(int)(GLsizeiptr)pgl_vjob.indices+vert;
        else if(pgl_vjob.type==GL_UNSIGNED_BYTE) i=((const GLubyte*)pgl_vjob.indices)[vert];
        else if(pgl_vjob.type==GL_UNSIGNED_SHORT) i=((const GLushort*)pgl_vjob.indices)[vert];
        else i=((const GLuint*)pgl_vjob.indices)[vert];
        for(int j=0;j<pgl_vjob.n_enabled;j++) {
            int loc=pgl_vjob.enabled[j]; attrs[loc]=get_v_attrib(&pgl_vjob.attr[loc],i);
        }
        float *out=pgl_vjob.outputs+vert*c->vs_output.size;
        pgl_vertex_exec(worker,out,attrs,&builtins,pgl_vertex_user);
        pgl_vjob.vertices[vert].vs_out=out;
        pgl_vjob.vertices[vert].clip_space=builtins.gl_Position;
        pgl_vjob.vertices[vert].clip_code=gl_clipcode(builtins.gl_Position);
        pgl_vjob.vertices[vert].rw_projected_valid=GL_FALSE;
        if (pgl_tri_clip_only && !c->cull_face) {
            pgl_vjob.vertices[vert].screen_space_valid=GL_FALSE;
            continue;
        }
        /* The viewport transform is read-only for the duration of a draw.
         * Do it while the persistent vertex workers already own this range;
         * the old path repeated the same matrix-vector multiply on the render
         * thread for every unclipped triangle, which dominated RW GPU
         * collection for non-indexed meshes.  Clipped temporary vertices are
         * still marked invalid below and take the exact post-clip path. */
        pgl_vjob.vertices[vert].screen_space =
            mult_m4_v4(c->vp_mat, pgl_vjob.vertices[vert].clip_space);
        pgl_vjob.vertices[vert].screen_space_valid=GL_TRUE;
        pgl_vjob.vertices[vert].rw_projected_valid=GL_FALSE;
        if (pgl_tri_callback) {
            vec4 s=pgl_vjob.vertices[vert].screen_space;
            float iw=1.0f/s.w;
            if (isfinite(s.x) && isfinite(s.y) && isfinite(s.z) &&
                isfinite(s.w) && s.w>1.0e-6f) {
                pgl_vjob.vertices[vert].rw_projected_x=s.x*iw;
                pgl_vjob.vertices[vert].rw_projected_y=s.y*iw;
                pgl_vjob.vertices[vert].rw_projected_z=s.z;
                pgl_vjob.vertices[vert].rw_projected_iw=iw;
                pgl_vjob.vertices[vert].rw_projected_valid=GL_TRUE;
            }
        }
    }
}
static void *pgl_vertex_worker(void *arg)
{
    int worker=(int)(intptr_t)arg;
    for(;;) {
        while(sem_wait(&pgl_vworkers[worker-1].start)!=0 && errno==EINTR) {}
        if(pgl_vpool_stop) break;
        pgl_vertex_range(worker);
        sem_post(&pgl_vworkers[worker-1].done);
    }
    return NULL;
}
PGLDEF void pglStopVertexWorkers(void)
{
    pgl_vpool_stop=1;
    for(int i=0;i<pgl_vworker_count;i++) sem_post(&pgl_vworkers[i].start);
    for(int i=0;i<pgl_vworker_count;i++) {
        pthread_join(pgl_vworkers[i].thread,NULL);
        sem_destroy(&pgl_vworkers[i].start);sem_destroy(&pgl_vworkers[i].done);
    }
    pgl_vworker_count=pgl_vpool_tried=pgl_vpool_stop=0;
    pgl_vertex_exec=NULL;pgl_vertex_user=NULL;
    free(pgl_vcache_words);free(pgl_vcache_vertices);free(pgl_vcache_outputs);
    pgl_vcache_words=NULL;pgl_vcache_vertices=NULL;pgl_vcache_outputs=NULL;
    pgl_vcache_wordcap=pgl_vcache_vertcap=pgl_vcache_floatcap=0;
}
static int pgl_vertex_cache_reserve(int count,size_t hashcap)
{
    size_t words=hashcap*2+(size_t)count*2;
    size_t floats=(size_t)count*c->vs_output.size;
    if(words>pgl_vcache_wordcap) {
        GLuint *p=realloc(pgl_vcache_words,words*sizeof *p);if(!p) return 0;
        pgl_vcache_words=p;pgl_vcache_wordcap=words;
    }
    if((size_t)count>pgl_vcache_vertcap) {
        glVertex *p=realloc(pgl_vcache_vertices,(size_t)count*sizeof *p);if(!p) return 0;
        pgl_vcache_vertices=p;pgl_vcache_vertcap=count;
    }
    if(floats>pgl_vcache_floatcap) {
        float *p=realloc(pgl_vcache_outputs,floats*sizeof *p);if(!p) return 0;
        pgl_vcache_outputs=p;pgl_vcache_floatcap=floats;
    }
    return 1;
}
static int pgl_parallel_vertices(glVertex_Attrib *attr,int *enabled,int n_enabled,
                                 const void *indices,int count,GLenum type)
{
    int parallel_min=pgl_vertex_parallel_min(type);
    if(!pgl_vertex_exec || count<parallel_min) return 0;
    if(!pgl_vpool_tried) {
        int requested=8;
        const char *env=getenv("PGL_VERTEX_THREADS");
        if(env) requested=atoi(env);
        if(requested<1) requested=1;
        if(requested>PGL_VERTEX_WORKERS) requested=PGL_VERTEX_WORKERS;
        pgl_vpool_tried=1;
        for(int i=0;i<requested-1;i++) {
            if(sem_init(&pgl_vworkers[i].start,0,0)!=0) break;
            if(sem_init(&pgl_vworkers[i].done,0,0)!=0) {sem_destroy(&pgl_vworkers[i].start);break;}
            if(pthread_create(&pgl_vworkers[i].thread,NULL,pgl_vertex_worker,(void*)(intptr_t)(i+1))!=0) {
                sem_destroy(&pgl_vworkers[i].start);sem_destroy(&pgl_vworkers[i].done);break;
            }
            ++pgl_vworker_count;
        }
    }
    if(type && c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER]) {
        GLuint elem=c->vertex_arrays.a[c->cur_vertex_array].element_buffer;
        indices=c->buffers.a[elem].data+(GLsizeiptr)indices;
    }
    pgl_vjob.attr=attr;pgl_vjob.enabled=enabled;pgl_vjob.n_enabled=n_enabled;
    pgl_vjob.indices=indices;pgl_vjob.count=count;pgl_vjob.type=type;
    pgl_vjob.vertices=c->glverts.a;pgl_vjob.outputs=c->vs_output.output_buf;
    GLuint *map=NULL;
    const char *cache=getenv("PGL_VERTEX_CACHE");
    if(type && (!cache || atoi(cache))) {
        /* GLSL ES vertex outputs depend on the fetched attributes, uniforms,
         * and instance, all fixed within this draw. Evaluate shared indices
         * once, then scatter to portablegl's original expanded vertex layout. */
        size_t hashcap=128;while(hashcap<(size_t)count*2) hashcap*=2;
        if(pgl_vertex_cache_reserve(count,hashcap)) {
            GLuint *keys=pgl_vcache_words,*slots=keys+hashcap;
            GLuint *unique=slots+hashcap;map=unique+count;
            memset(slots,255,hashcap*sizeof *slots);
            int n=0;
            for(int i=0;i<count;i++) {
                GLuint index=type==GL_UNSIGNED_BYTE?((const GLubyte*)indices)[i]:
                    type==GL_UNSIGNED_SHORT?((const GLushort*)indices)[i]:((const GLuint*)indices)[i];
                size_t h=(index*2654435761u)&(hashcap-1);
                while(slots[h]!=~0u && keys[h]!=index) h=(h+1)&(hashcap-1);
                if(slots[h]==~0u) {keys[h]=index;slots[h]=n;unique[n++]=index;}
                map[i]=slots[h];
            }
            pgl_vjob.indices=unique;pgl_vjob.type=GL_UNSIGNED_INT;pgl_vjob.count=n;
            pgl_vjob.vertices=pgl_vcache_vertices;pgl_vjob.outputs=pgl_vcache_outputs;
        }
    }
    pgl_vjob.tasks=pgl_vjob.count<parallel_min?1:pgl_vworker_count+1;
    for(int i=0;i<pgl_vjob.tasks-1;i++) sem_post(&pgl_vworkers[i].start);
    pgl_vertex_range(0);
    for(int i=0;i<pgl_vjob.tasks-1;i++)
        while(sem_wait(&pgl_vworkers[i].done)!=0 && errno==EINTR) {}
    /* Indexed and non-indexed ranges both received their viewport result in
     * pgl_vertex_range().  The scatter below only copies the cached result;
     * clipped triangles can invalidate it later and are recomputed after the
     * clip intersection, as before. */
    if(map) for(int i=0;i<count;i++) {
        glVertex *src=&pgl_vcache_vertices[map[i]],*dst=&c->glverts.a[i];
        float *out=c->vs_output.output_buf+(size_t)i*c->vs_output.size;
        memcpy(out,src->vs_out,(size_t)c->vs_output.size*sizeof(float));
        dst->vs_out=out;dst->clip_space=src->clip_space;dst->clip_code=src->clip_code;
        dst->screen_space=src->screen_space;
        dst->screen_space_valid=src->screen_space_valid;
        dst->rw_projected_x=src->rw_projected_x;
        dst->rw_projected_y=src->rw_projected_y;
        dst->rw_projected_z=src->rw_projected_z;
        dst->rw_projected_iw=src->rw_projected_iw;
        dst->rw_projected_valid=src->rw_projected_valid;
    }
    return 1;
}
