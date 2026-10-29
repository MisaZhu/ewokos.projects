// Host-side test harness for the LIBRW_EWOKOS mini shader preprocessor.
// Includes the real shader strings, runs the same preprocessing as
// gl3shader.cpp's ewokPreprocess(), and prints the result.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define rwMalloc(size, dur) malloc(size)
#define rwFree free
#define MEMDUR_FUNCTION 0

static const char *shaderDecl100es =
"#version 100\n"
"#define GL2\n"
"#define texture texture2D\n"
"#define VSIN(index) attribute\n"
"#define VSOUT varying\n"
"#define FSIN varying\n"
"#define FRAGCOLOR(c) (gl_FragColor = c)\n"
"precision highp float;\n"
"precision highp int;\n"
"#define CENTROID\n"
"#define STEPTEX() \n"
;

#include "shaders/header_vs.inc"

#include "shaders/default_vs_gl.inc"

#define LIBRW_EWOKOS 1

// ---- copy of ewokPreprocess from gl3shader.cpp ----
#define EWOK_PP_MAXDEFS 64
#define EWOK_PP_MAXLINE 4096

struct EwokDef { char name[64]; char body[256]; };

static void
ewokDefExpand(char *line, struct EwokDef *defs, int ndefs)
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
ewokDefined(struct EwokDef *defs, int ndefs, const char *nm)
{
	for(int d = 0; d < ndefs; d++)
		if(strcmp(defs[d].name, nm) == 0)
			return 1;
	return 0;
}

static char *
ewokPreprocess(char *src)
{
	struct EwokDef defs[EWOK_PP_MAXDEFS];
	int ndefs = 0;
	int depth = 0;
	int branchActive[16];
	int branchTaken[16];
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
			continue;
		}

		if(!branchActive[depth])
			continue;

		memcpy(expanded, line, EWOK_PP_MAXLINE);
		ewokDefExpand(expanded, defs, ndefs);
		size_t el = strlen(expanded);
		memcpy(o, expanded, el);
		o += el;
	}
	*o = '\0';
	return out;
}
// ---- end copy ----

int main(void)
{
	const char *srcs[] = { shaderDecl100es, header_vert_src, default_vert_src, NULL };
	size_t total = 0;
	for(int i = 0; srcs[i]; i++) total += strlen(srcs[i]);
	char *buf = (char*)malloc(total + 1);
	char *p = buf;
	for(int i = 0; srcs[i]; i++) p += sprintf(p, "%s", srcs[i]);

	char *out = ewokPreprocess(buf);
	printf("---- preprocessed (%zu bytes) ----\n%s\n---- end ----\n", strlen(out), out);
	if(strstr(out, "main"))
		printf("HAS main\n");
	else
		printf("NO MAIN FOUND\n");
	return 0;
}
