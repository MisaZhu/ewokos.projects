#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#ifdef RW_OPENGL
#include "rwgl3.h"
#include "rwgl3shader.h"

namespace rw {
namespace gl3 {

#include "shaders/header_vs.inc"
#include "shaders/header_fs.inc"

UniformRegistry uniformRegistry;
static char nameBuffer[(MAX_UNIFORMS + MAX_BLOCKS)*32];	// static because memory system isn't up yet when we register
static uint32 nameBufPtr;
static float uniformData[512*4];	// seems enough
static uint32 dataPtr;

static int uniformTypesize[] = {
	0, 4, 4, 16
};

static char*
shader_strdup(const char *name)
{
	size_t len = strlen(name)+1;
	char *s = &nameBuffer[nameBufPtr];
	nameBufPtr += len;
	assert(nameBufPtr <= nelem(nameBuffer));
	memcpy(s, name, len);
	return s;
}

int32
registerUniform(const char *name, UniformType type, int32 num)
{
	int i;
	i = findUniform(name);
	if(i >= 0){
		Uniform *u = &uniformRegistry.uniforms[i];
		assert(u->type == type);
		assert(u->num == num);
		return i;
	}
	// TODO: print error
	if(uniformRegistry.numUniforms+1 >= MAX_UNIFORMS){
		assert(0 && "no space for uniform");
		return -1;
	}
	Uniform *u = &uniformRegistry.uniforms[uniformRegistry.numUniforms];
	u->name = shader_strdup(name);
	u->type = type;
	u->serialNum = 0;
	if(type == UNIFORM_NA){
		u->num = 0;
		u->data = nil;
	}else{
		u->num = num;
		u->data = &uniformData[dataPtr];
		dataPtr += uniformTypesize[type]*num;
		assert(dataPtr <= nelem(uniformData));
	}
	
	return uniformRegistry.numUniforms++;
}

int32
findUniform(const char *name)
{
	int i;
	for(i = 0; i < uniformRegistry.numUniforms; i++)
		if(strcmp(name, uniformRegistry.uniforms[i].name) == 0)
			return i;
	return -1;
}

int32
registerBlock(const char *name)
{
	int i;
	i = findBlock(name);
	if(i >= 0) return i;
	// TODO: print error
	if(uniformRegistry.numBlocks+1 >= MAX_BLOCKS)
		return -1;
	uniformRegistry.blockNames[uniformRegistry.numBlocks] = shader_strdup(name);
	return uniformRegistry.numBlocks++;
}

int32
findBlock(const char *name)
{
	int i;
	for(i = 0; i < uniformRegistry.numBlocks; i++)
		if(strcmp(name, uniformRegistry.blockNames[i]) == 0)
			return i;
	return -1;
}

void
setUniform(int32 id, void *data)
{
	Uniform *u = &uniformRegistry.uniforms[id];
	assert(u->type != UNIFORM_NA);
	if(memcmp(u->data, data, uniformTypesize[u->type]*u->num * sizeof(float)) != 0){
		memcpy(u->data, data, uniformTypesize[u->type]*u->num * sizeof(float));
		//u->dirty = true;
		u->serialNum++;
	}
}

void
flushUniforms(void)
{
	for(int i = 0; i < uniformRegistry.numUniforms; i++){
		// this is bad!
		if(i >= currentShader->numUniforms){
			printf("trying to set uniform %d %s that doesn't exist!\n", i, uniformRegistry.uniforms[i].name);
			continue;
		}

		int32 loc = currentShader->uniformLocations[i];
		if(loc == -1)
			continue;

		Uniform *u = &uniformRegistry.uniforms[i];
		if(currentShader->serialNums[i] != u->serialNum)
			switch(u->type){
			case UNIFORM_NA:
				break;
			case UNIFORM_VEC4:
				glUniform4fv(loc, u->num, (GLfloat*)u->data);
				break;
			case UNIFORM_IVEC4:
				glUniform4iv(loc, u->num, (GLint*)u->data);
				break;
			case UNIFORM_MAT4:
				glUniformMatrix4fv(loc, u->num, GL_FALSE, (GLfloat*)u->data);
				break;
			}
		currentShader->serialNums[i] = u->serialNum;
	}
}

Shader *currentShader;

static void
printShaderSource(const char **src)
{
	int f;
	const char *file;
	bool printline;
	int line = 1;
	for(f = 0; src[f]; f++){
		int fileline = 1;
		char c;
		file = src[f];
		printline = true;
		while(c = *file++, c != '\0'){
			if(printline)
				printf("%.4d/%d:%.4d: ", line++, f, fileline++);
			putchar(c);
			printline = c == '\n';
		}
		putchar('\n');
	}
}

#ifdef LIBRW_EWOKOS
// The EwokOS GLES engine's shader lexer skips every '#' line without any
// preprocessor semantics: no macro expansion and both sides of #ifdef end up
// parsed as GLSL (the USE_UBOS branch's layout(...) can't compile). This is
// a small line preprocessor good enough for librw's shaders: object-like
// #define expansion, #ifdef/#ifndef/#else/#endif and dropping #version and
// friends. Function-like macros are already expanded in the .inc sources.
#define EWOK_PP_MAXDEFS 64
#define EWOK_PP_MAXLINE 4096

struct EwokDef { char name[64]; char body[256]; };

static void
ewokDefExpand(char *line, EwokDef *defs, int ndefs)
{
	static char tmp[EWOK_PP_MAXLINE];
	for(int pass = 0; pass < 4; pass++){
		char *out = tmp;
		const char *p = line;
		while(*p){
			if(isalpha((unsigned char)*p) || *p == '_'){
				char word[64];
				int k = 0;
				while((isalnum((unsigned char)*p) || *p == '_') && k < 63)
					word[k++] = *p++;
				word[k] = '\0';
				int d;
				for(d = 0; d < ndefs; d++)
					if(strcmp(defs[d].name, word) == 0)
						break;
				if(d < ndefs && defs[d].body[0]){
					size_t l = strlen(defs[d].body);
					memcpy(out, defs[d].body, l);
					out += l;
				}else{
					size_t l = strlen(word);
					memcpy(out, word, l);
					out += l;
				}
			}else{
				*out++ = *p++;
			}
		}
		*out = '\0';
		if(strcmp(tmp, line) == 0)
			break;
		memcpy(line, tmp, strlen(tmp) + 1);
	}
}

static int
ewokDefined(EwokDef *defs, int ndefs, const char *nm)
{
	for(int d = 0; d < ndefs; d++)
		if(strcmp(defs[d].name, nm) == 0)
			return 1;
	return 0;
}

static char*
ewokPreprocess(char *src)
{
	EwokDef defs[EWOK_PP_MAXDEFS];
	int ndefs = 0;
	int depth = 0;                  // #if nesting depth
	int branchActive[16];           // emitting lines at this level?
	int branchTaken[16];            // did any branch of this #if emit?
	branchActive[0] = 1;
	branchTaken[0] = 1;

	size_t srclen = strlen(src);
	char *out = (char*)rwMalloc(srclen + 4096, MEMDUR_FUNCTION);
	char *o = out;

	char line[EWOK_PP_MAXLINE];
	char expanded[EWOK_PP_MAXLINE];
	const char *p = src;
	while(*p){
		size_t l = 0;
		while(p[l] && p[l] != '\n' && l < EWOK_PP_MAXLINE - 2)
			l++;
		memcpy(line, p, l);
		line[l] = '\0';
		if(p[l] == '\n') l++;
		p += l;

		char *t = line;
		while(*t == ' ' || *t == '\t') t++;

		if(t[0] == '#'){
			char word[32];
			int k = 0;
			t++;
			while(*t == ' ' || *t == '\t') t++;
			while(isalpha((unsigned char)*t) && k < 31)
				word[k++] = *t++;
			word[k] = '\0';
			while(*t == ' ' || *t == '\t') t++;

			if(strcmp(word, "ifdef") == 0 || strcmp(word, "ifndef") == 0){
				char nm[64]; k = 0;
				while((isalnum((unsigned char)*t) || *t == '_') && k < 63)
					nm[k++] = *t++;
				nm[k] = '\0';
				int has = ewokDefined(defs, ndefs, nm);
				if(strcmp(word, "ifndef") == 0) has = !has;
				if(depth < 15){
					depth++;
					branchTaken[depth] = branchActive[depth-1] && has;
					branchActive[depth] = branchTaken[depth];
				}
			}else if(strcmp(word, "else") == 0){
				if(depth > 0){
					branchActive[depth] = branchActive[depth-1] && !branchTaken[depth];
					if(branchActive[depth]) branchTaken[depth] = 1;
				}
			}else if(strcmp(word, "endif") == 0){
				if(depth > 0)
					depth--;
			}else if(strcmp(word, "define") == 0 && branchActive[depth]){
				char nm[64]; k = 0;
				while((isalnum((unsigned char)*t) || *t == '_') && k < 63)
					nm[k++] = *t++;
				nm[k] = '\0';
				if(ndefs < EWOK_PP_MAXDEFS){
					strcpy(defs[ndefs].name, nm);
					char *b = defs[ndefs].body;
					// function-like macro (paren right after the name): not
					// supported by this mini pp; keep the name only
					if(*t == '('){
						*b = '\0';
					}else{
						while(*t == ' ' || *t == '\t') t++;
						size_t bl = strlen(t);
						if(bl > 255) bl = 255;
						memcpy(b, t, bl);
						b[bl] = '\0';
						char *nl = strchr(b, '\n');
						if(nl) *nl = '\0';
						char *cm = strstr(b, "//");
						if(cm) *cm = '\0';
					}
					ndefs++;
				}
			}
			// #version / #extension / anything else: dropped
			continue;
		}

		if(!branchActive[depth])
			continue;

		memcpy(expanded, line, EWOK_PP_MAXLINE);
		ewokDefExpand(expanded, defs, ndefs);
		size_t el = strlen(expanded);
		memcpy(o, expanded, el);
		o += el;
		// keep the newline: a swallowed one would merge // comments with all
		// following lines and comment out the rest of the shader
		*o++ = '\n';
	}
	*o = '\0';
	return out;
}
#endif

static int
compileshader(GLenum type, const char **src, GLuint *shader)
{
	GLint n;
	GLint shdr, success;
	GLint len;
	char *log;

	for(n = 0; src[n]; n++);

	shdr = glCreateShader(type);
#ifdef LIBRW_EWOKOS
	{
		int j;
		size_t total = 0;
		char *buf, *p, *pre;
		for(j = 0; j < n; j++)
			total += strlen(src[j]);
		buf = (char*)rwMalloc(total + 1, MEMDUR_FUNCTION);
		p = buf;
		for(j = 0; j < n; j++) {
			size_t l = strlen(src[j]);
			memcpy(p, src[j], l);
			p += l;
		}
		*p = '\0';
		pre = ewokPreprocess(buf);
		glShaderSource(shdr, 1, (const char**)&pre, nil);
		glCompileShader(shdr);
		rwFree(buf);
		rwFree(pre);
	}
#else
	glShaderSource(shdr, n, src, nil);
	glCompileShader(shdr);
#endif
	glGetShaderiv(shdr, GL_COMPILE_STATUS, &success);
	if(!success){
#ifdef LIBRW_EWOKOS
		{
			// reconstruct what was actually uploaded for this compile
			int j; size_t total = 0; char *dbg, *dp;
			for(j = 0; j < n; j++) total += strlen(src[j]);
			dbg = (char*)rwMalloc(total + 1, MEMDUR_FUNCTION);
			dp = dbg;
			for(j = 0; j < n; j++) {
				size_t l = strlen(src[j]);
				memcpy(dp, src[j], l); dp += l;
			}
			*dp = '\0';
			{
				char *d2 = ewokPreprocess(dbg);
				fprintf(stderr, "re3: pp len=%zu first200=%.200s\n", strlen(d2), d2);
				rwFree(d2);
			}
			rwFree(dbg);
		}
#endif
		printShaderSource(src);
		fprintf(stderr, "Error in %s shader\n",
		  type == GL_VERTEX_SHADER ? "vertex" : "fragment");
		glGetShaderiv(shdr, GL_INFO_LOG_LENGTH, &len);
		log = (char*)rwMalloc(len, MEMDUR_FUNCTION);
		glGetShaderInfoLog(shdr, len, nil, log);
		fprintf(stderr, "%s\n", log);
		rwFree(log);
		return 1;
	}
	*shader = shdr;
	return 0;
}

static int
linkprogram(GLint vs, GLint fs, GLuint *program)
{
	GLint prog, success;
	GLint len;
	char *log;

	prog = glCreateProgram();

	if(gl3Caps.glversion < 30){
		// TODO: perhaps just do this always and get rid of the layout stuff?
		glBindAttribLocation(prog, ATTRIB_POS, "in_pos");
		glBindAttribLocation(prog, ATTRIB_NORMAL, "in_normal");
		glBindAttribLocation(prog, ATTRIB_COLOR, "in_color");
		glBindAttribLocation(prog, ATTRIB_WEIGHTS, "in_weights");
		glBindAttribLocation(prog, ATTRIB_INDICES, "in_indices");
		glBindAttribLocation(prog, ATTRIB_TEXCOORDS0, "in_tex0");
		glBindAttribLocation(prog, ATTRIB_TEXCOORDS1, "in_tex1");
	}

	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if(!success){
		fprintf(stderr, "Error in program\n");
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
		log = (char*)rwMalloc(len, MEMDUR_FUNCTION);
		glGetProgramInfoLog(prog, len, nil, log);
		fprintf(stderr, "%s\n", log);
		rwFree(log);
		return 1;
	}
	*program = prog;
	return 0;
}

Shader*
Shader::create(const char **vsrc, const char **fsrc)
{
	GLuint vs, fs, program;
	int i;
	int fail;

	fail = compileshader(GL_VERTEX_SHADER, vsrc, &vs);
	if(fail)
		return nil;

	fail = compileshader(GL_FRAGMENT_SHADER, fsrc, &fs);
	if(fail){
		glDeleteShader(vs);
		return nil;
	}

	fail = linkprogram(vs, fs, &program);

	glDeleteShader(vs);
	glDeleteShader(fs);
	if(fail){
		return nil;
	}

	Shader *sh = rwNewT(Shader, 1, MEMDUR_EVENT | ID_DRIVER);	 // or global?

#ifdef xxxRW_GLES2
	int numUniforms;
	glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
	for(i = 0; i < numUniforms; i++){
		GLint size;
		GLenum type;
		char name[100];
		glGetActiveUniform(program, i, 100, nil, &size, &type, name);
		printf("%d %d %s\n", size, type, name);
	}
	printf("\n");
#endif

#ifdef xxxRW_GLES2
	int numAttribs;
	glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &numAttribs);
	for(i = 0; i < numAttribs; i++){
		GLint size;
		GLenum type;
		char name[100];
		glGetActiveAttrib(program, i, 100, nil, &size, &type, name);
		GLint bind = glGetAttribLocation(program, name);
		printf("%d %d %s. %d\n", size, type, name, bind);
	}
	printf("\n");
#endif

	// set uniform block binding
	for(i = 0; i < uniformRegistry.numBlocks; i++){
		int idx = glGetUniformBlockIndex(program,
		                                 uniformRegistry.blockNames[i]);
		if(idx >= 0)
			glUniformBlockBinding(program, idx, i);
	}

	// query uniform locations
	sh->program = program;
	sh->numUniforms = uniformRegistry.numUniforms;
	sh->uniformLocations = rwNewT(GLint, uniformRegistry.numUniforms, MEMDUR_EVENT | ID_DRIVER);
	sh->serialNums = rwNewT(uint32, uniformRegistry.numUniforms, MEMDUR_EVENT | ID_DRIVER);
	for(i = 0; i < uniformRegistry.numUniforms; i++){
		sh->uniformLocations[i] = glGetUniformLocation(program,
			uniformRegistry.uniforms[i].name);
		sh->serialNums[i] = ~0;	// let's hope this means dirty
	}

	// set samplers
	glUseProgram(program);
	char name[64];
	GLint loc;
	for(i = 0; i < 4; i++){
		sprintf(name, "tex%d", i);
		loc = glGetUniformLocation(program, name);
		glUniform1i(loc, i);
	}

	// reset program
	if(currentShader)
		glUseProgram(currentShader->program);

	return sh;
}

void
Shader::use(void)
{
	if(currentShader != this){
		glUseProgram(this->program);
		currentShader = this;
	}
}

void
Shader::destroy(void)
{
	glDeleteProgram(this->program);
	rwFree(this->uniformLocations);
	rwFree(this->serialNums);
	rwFree(this);
}

}
}

#endif
