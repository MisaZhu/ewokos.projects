/*
 * Copyright (C) 1997-2017 Id Software, Inc. / Daniel Gibson
 * (GPLv2 - see the upstream yquake2 tree for the full license text.)
 *
 * =======================================================================
 *
 * OpenGL3 refresher: Handling shaders
 *
 * EwokOS soft3d down-port (projects/soft3d/apps/yquake2):
 * The stock gl3 shaders are GLSL ES 3.00 and feed their matrices/params
 * through four std140 uniform buffer objects (uniCommon/uni2D/uni3D/
 * uniLights).  The libs/gles front end implements a GLES2 subset with a CPU
 * GLSL ES 1.00 interpreter that has neither UBOs nor the ES3 dialect, so this
 * file was down-ported:
 *   - dialect: in/out -> attribute/varying, out vec4 outColor -> gl_FragColor,
 *     texture() -> texture2D(), uint/flat dropped.
 *   - UBOs flattened: each block became plain `uniform` declarations that keep
 *     the exact member names, so shader bodies are unchanged.  The block data
 *     still lives in gl3state.uni{Common,2D,3D}Data; instead of glBufferData
 *     into a UBO, GL3_UpdateUBO*()/GL3_UseProgram() push those values into the
 *     bound program with glUniform*() (see the bottom of this file).
 *   - the lightmapped world shader's dynamic-light loop (a 32-entry struct
 *     array indexed by a loop variable, plus flat uint varyings) is beyond the
 *     interpreter, so it is reduced to a single static lightmap sample.  The
 *     lightmapped passes are only exercised by a real .bsp world anyway.
 *
 * =======================================================================
 */

#include "header/local.h"

// TODO: remove eprintf() usage
#define eprintf(...)  R_Printf(PRINT_ALL, __VA_ARGS__)


static GLuint
CompileShader(GLenum shaderType, const char* shaderSrc, const char* shaderSrc2)
{
	GLuint shader = glCreateShader(shaderType);

	// EwokOS down-port: the interpreter speaks GLSL ES 1.00.  The sources
	// below are already written in that dialect; fragment shaders only need a
	// default float precision (mirrors apps/imgui_demo, which the interpreter
	// is proven to accept).  Vertex shaders get no prefix.
	const char* version = (shaderType == GL_FRAGMENT_SHADER)
	                    ? "precision mediump float;\n" : "";
	const char* sources[3] = { version, shaderSrc, shaderSrc2 };
	int numSources = shaderSrc2 != NULL ? 3 : 2;

	glShaderSource(shader, numSources, sources, NULL);
	glCompileShader(shader);
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if(status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CompileShader(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetShaderInfoLog(shader, bufLen, NULL, bufPtr);

		const char* shaderTypeStr = "";
		switch(shaderType)
		{
			case GL_VERTEX_SHADER:   shaderTypeStr = "Vertex"; break;
			case GL_FRAGMENT_SHADER: shaderTypeStr = "Fragment"; break;
		}
		eprintf("ERROR: Compiling %s Shader failed: %s\n", shaderTypeStr, bufPtr);
		glDeleteShader(shader);

		if(bufPtr != buf)  free(bufPtr);

		return 0;
	}

	return shader;
}

static GLuint
CreateShaderProgram(int numShaders, const GLuint* shaders)
{
	int i=0;
	GLuint shaderProgram = glCreateProgram();
	if(shaderProgram == 0)
	{
		eprintf("ERROR: Couldn't create a new Shader Program!\n");
		return 0;
	}

	for(i=0; i<numShaders; ++i)
	{
		glAttachShader(shaderProgram, shaders[i]);
	}

	// make sure all shaders use the same attribute locations for common attributes
	// (so the same VAO can easily be used with different shaders)
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_TEXCOORD, "texCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LMTEXCOORD, "lmTexCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_COLOR, "vertColor");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_NORMAL, "normal");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LIGHTFLAGS, "lightFlags");

	glLinkProgram(shaderProgram);

	GLint status;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &status);
	if(status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CreateShaderProgram(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetProgramInfoLog(shaderProgram, bufLen, NULL, bufPtr);

		eprintf("ERROR: Linking shader program failed: %s\n", bufPtr);

		glDeleteProgram(shaderProgram);

		if(bufPtr != buf)  free(bufPtr);

		return 0;
	}

	for(i=0; i<numShaders; ++i)
	{
		// after linking, they don't need to be attached anymore.
		glDetachShader(shaderProgram, shaders[i]);
	}

	return shaderProgram;
}

#define MULTILINE_STRING(...) #__VA_ARGS__

// ############## shaders for 2D rendering (HUD, menus, console, videos, ..) #####################

static const char* vertexSrc2D = MULTILINE_STRING(

		attribute vec2 position; // GL3_ATTRIB_POSITION
		attribute vec2 texCoord; // GL3_ATTRIB_TEXCOORD

		uniform mat4 trans; // was uni2D UBO

		varying vec2 passTexCoord;

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
			passTexCoord = texCoord;
		}
);

static const char* fragmentSrc2D = MULTILINE_STRING(

		varying vec2 passTexCoord;

		// was uniCommon UBO
		uniform float gamma;
		uniform float intensity;
		uniform float intensity2D; // for HUD, menu etc
		uniform vec4 color;

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);
			// the gl1 renderer used glAlphaFunc(GL_GREATER, 0.666) + GL_ALPHA_TEST
			if(texel.a <= 0.666)
				discard;

			// apply gamma correction and intensity
			texel.rgb *= intensity2D;
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a; // alpha isn't modified by gamma and intensity
			gl_FragColor = c;
		}
);

// like fragmentSrc2D, but also multiplies by color uniform for tinting (e.g. crosshair color)
static const char* fragmentSrc2Dtinted = MULTILINE_STRING(

		varying vec2 passTexCoord;

		// was uniCommon UBO
		uniform float gamma;
		uniform float intensity;
		uniform float intensity2D; // for HUD, menu etc
		uniform vec4 color;

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);

			if(texel.a <= 0.666)
				discard;

			// apply color tint
			texel.rgb *= color.rgb;

			// apply gamma correction and intensity
			texel.rgb *= intensity2D;
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a;
			gl_FragColor = c;
		}
);

static const char* fragmentSrc2Dpostprocess = MULTILINE_STRING(
		varying vec2 passTexCoord;

		uniform sampler2D tex;
		uniform vec4 v_blend;

		void main()
		{
			// no gamma or intensity here, it has been applied before
			vec4 res = texture2D(tex, passTexCoord);
			res.rgb = v_blend.a * v_blend.rgb + (1.0 - v_blend.a)*res.rgb;
			gl_FragColor = res;
		}
);

static const char* fragmentSrc2DpostprocessWater = MULTILINE_STRING(
		varying vec2 passTexCoord;

		uniform sampler2D tex;

		uniform float time;
		uniform vec4 v_blend;

		void main()
		{
			vec2 uv = passTexCoord;

			// warping based on vkquake2 (uv is always between 0 and 1 here)
			float sx = 1.0 - abs(0.5-uv.x)*2.0;
			float sy = 1.0 - abs(0.5-uv.y)*2.0;
			float xShift = 2.0 * time + uv.y * 31.4159265;
			float yShift = 2.0 * time + uv.x * 31.4159265;
			vec2 distortion = vec2(sin(xShift) * sx, sin(yShift) * sy) * 0.00666;

			uv += distortion;
			uv = clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0));

			vec4 res = texture2D(tex, uv);
			res.rgb = v_blend.a * v_blend.rgb + (1.0 - v_blend.a)*res.rgb;
			gl_FragColor = res;
		}
);

// 2D color only rendering, GL3_Draw_Fill(), GL3_Draw_FadeScreen()
static const char* vertexSrc2Dcolor = MULTILINE_STRING(

		attribute vec2 position; // GL3_ATTRIB_POSITION

		uniform mat4 trans; // was uni2D UBO

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
		}
);

static const char* fragmentSrc2Dcolor = MULTILINE_STRING(

		// was uniCommon UBO
		uniform float gamma;
		uniform float intensity;
		uniform float intensity2D; // for HUD, menus etc
		uniform vec4 color;

		void main()
		{
			vec4 c;
			vec3 col = color.rgb * intensity2D;
			c.rgb = pow(col, vec3(gamma));
			c.a = color.a;
			gl_FragColor = c;
		}
);

// ############## shaders for 3D rendering #####################

// Common 3D vertex prelude: attributes + the projection/model matrices (both
// vertex-only) + the passTexCoord varying.  Per-shader scalars that are also
// read in a fragment stage (scroll, overbrightbits, lightScaleForTurb) are
// declared in the specific stage that uses them so no uniform is ever declared
// in both stages of one program.
static const char* vertexCommon3D = MULTILINE_STRING(

		attribute vec3 position;   // GL3_ATTRIB_POSITION
		attribute vec2 texCoord;   // GL3_ATTRIB_TEXCOORD
		attribute vec2 lmTexCoord; // GL3_ATTRIB_LMTEXCOORD
		attribute vec4 vertColor;  // GL3_ATTRIB_COLOR
		attribute vec3 normal;     // GL3_ATTRIB_NORMAL
		attribute float lightFlags; // GL3_ATTRIB_LIGHTFLAGS (unused: dyn lights dropped)

		varying vec2 passTexCoord;

		// was uni3D UBO (matrices are vertex-only)
		uniform mat4 transProjView;
		uniform mat4 transModel;
);

// Common 3D fragment prelude: the passTexCoord varying + all fragment-only
// scalars from uniCommon and uni3D.
static const char* fragmentCommon3D = MULTILINE_STRING(

		varying vec2 passTexCoord;

		// was uniCommon UBO
		uniform float gamma; // this is 1.0/vid_gamma
		uniform float intensity;
		uniform float intensity2D; // for HUD, menus etc
		uniform vec4 color;

		// was uni3D UBO (fragment-only members)
		uniform float time;
		uniform float alpha;
		uniform float particleFadeFactor;
);

static const char* vertexSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		void main()
		{
			passTexCoord = texCoord;
			gl_Position = transProjView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrc3Dflow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D
		uniform float scroll; // for SURF_FLOWING

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, 0.0);
			gl_Position = transProjView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		varying vec2 passLMcoord;

		void main()
		{
			passTexCoord = texCoord;
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			gl_Position = transProjView * worldCoord;
		}
);

static const char* vertexSrc3DlmFlow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D
		uniform float scroll; // for SURF_FLOWING

		varying vec2 passLMcoord;

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, 0.0);
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			gl_Position = transProjView * worldCoord;
		}
);

static const char* fragmentSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);

			// apply intensity and gamma
			texel.rgb *= intensity;
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a*alpha;
			gl_FragColor = c;
		}
);

static const char* fragmentSrc3Dwater = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D
		uniform float scroll;
		uniform float lightScaleForTurb;

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec2 tc = passTexCoord;
			tc.s += sin( passTexCoord.t*0.125 + time ) * 4.0;
			tc.s += scroll;
			tc.t += sin( passTexCoord.s*0.125 + time ) * 4.0;
			tc *= 1.0/64.0; // do this last

			vec4 texel = texture2D(tex, tc);

			// apply intensity and gamma
			texel.rgb *= intensity * lightScaleForTurb;
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a*alpha;
			gl_FragColor = c;
		}
);

// Down-ported lightmapped world shader: the ES3 original loops over a 32-entry
// dynamic-light struct array (indexed by a loop variable, with flat uint
// varyings and bitwise tests) which the interpreter cannot express.  Reduced to
// a single static lightmap sample modulating the base texture - correct for a
// pre-lit .bsp world without dynamic lights.
static const char* fragmentSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D
		uniform float overbrightbits;

		varying vec2 passLMcoord;

		uniform sampler2D tex;
		uniform sampler2D lightmap0;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);
			texel.rgb *= intensity;

			vec4 lmTex = texture2D(lightmap0, passLMcoord);
			lmTex.rgb *= overbrightbits;

			c = lmTex * texel;
			c.rgb = pow(c.rgb, vec3(gamma));
			c.a = 1.0; // lightmaps aren't used with translucent surfaces
			gl_FragColor = c;
		}
);

static const char* fragmentSrc3DlmNoColor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D
		uniform float overbrightbits;

		varying vec2 passLMcoord;

		uniform sampler2D tex;
		uniform sampler2D lightmap0;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);
			texel.rgb *= intensity;

			vec4 lmTex = texture2D(lightmap0, passLMcoord);

			// turn lightcolor into grey for gl3_colorlight 0
			lmTex.rgb = vec3(0.333 * (lmTex.r+lmTex.g+lmTex.b));
			lmTex.rgb *= overbrightbits;

			c = lmTex * texel;
			c.rgb = pow(c.rgb, vec3(gamma));
			c.a = 1.0;
			gl_FragColor = c;
		}
);

static const char* fragmentSrc3Dcolor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 c;
			vec4 texel = color;

			// apply gamma correction
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a*alpha;
			gl_FragColor = c;
		}
);

static const char* fragmentSrc3Dsky = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);

			// apply gamma correction
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a*alpha;
			gl_FragColor = c;
		}
);

static const char* fragmentSrc3Dsprite = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a*alpha;
			gl_FragColor = c;
		}
);

static const char* fragmentSrc3DspriteAlpha = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 c;
			vec4 texel = texture2D(tex, passTexCoord);

			if(texel.a <= 0.666)
				discard;

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a; // in this case alpha from uni3d shouldn't be used
			gl_FragColor = c;
		}
);

static const char* vertexSrc3Dwater = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D
		void main()
		{
			passTexCoord = texCoord;

			gl_Position = transProjView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrcAlias = MULTILINE_STRING(

// it gets attributes and uniforms from vertexCommon3D
// Standard portable GLES2: transform by the common proj*view and model
// matrices and pass the per-vertex light colour.  The platform (libs/gles)
// decides GPU-vs-CPU by matching this shape; the app carries no GPU token.
uniform float overbrightbits;
uniform mat4 uMVP;

varying vec4 passColor;

void main()
{
passColor = vertColor*overbrightbits;
passTexCoord = texCoord;
gl_Position = uMVP * vec4(position, 1.0);
}
);

static const char* fragmentSrcAlias = MULTILINE_STRING(

// it gets attributes and uniforms from fragmentCommon3D
// Portable GLES2: sample the skin and modulate by the interpolated per-vertex
// light colour, then gamma.  This "<colour varying> * texture2D(..)" shape is
// the platform's colour-modulated-texture dialect (the same one imgui uses):
// libs/gles runs it on the CPU interpreter or, under the platform's GPU build,
// maps it to the colour-modulated textured geometry kernel.  The app carries no
// GPU-specific token - the platform build selects the backend.
uniform sampler2D tex;

varying vec4 passColor;

void main()
{
	vec4 c = passColor * texture2D(tex, passTexCoord);
	c.rgb = pow(c.rgb, vec3(gamma));
	gl_FragColor = c;
}
);

static const char* fragmentSrcAliasColor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		varying vec4 passColor;

		void main()
		{
			vec4 c;
			vec4 texel = passColor;

			texel.a *= alpha; // is alpha even used here?
			c.rgb = pow(texel.rgb, vec3(gamma));
			c.a = texel.a;
			gl_FragColor = c;
		}
);

static const char* vertexSrcParticles = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		varying vec4 passColor;

		void main()
		{
			passColor = vertColor;
			gl_Position = transProjView * transModel * vec4(position, 1.0);

			// EwokOS down-port: the on-device mesa V3D backend aborts when a
			// linked program writes gl_PointSize (point sprites are not supported
			// by the raspi5 3D pipeline).  This standalone refresher never draws
			// particles (refdef.particles is unused), so the point-size output is
			// dropped here; the CPU-interpreter path is unaffected either way.
		}
);

static const char* fragmentSrcParticles = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		varying vec4 passColor;

		void main()
		{
			vec4 c;

			// EwokOS down-port: gl_PointCoord + discard (round point sprites)
			// likewise abort the mesa V3D backend at link time, so emit a flat
			// gamma-corrected colour instead.  Particles are not rendered here.
			c.rgb = pow(passColor.rgb, vec3(gamma));
			c.a = passColor.a * min(1.0, particleFadeFactor);
			gl_FragColor = c;
		}
);

static const char* fragmentSrcParticlesSquare = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		varying vec4 passColor;

		void main()
		{
			vec4 c;
			c.rgb = pow(passColor.rgb, vec3(gamma));
			c.a = passColor.a;
			gl_FragColor = c;
		}
);


#undef MULTILINE_STRING

/* ====================================================================== *
 * EwokOS down-port: flat-uniform driver functions
 *
 * Upstream binds four std140 UBOs (uniCommon/uni2D/uni3D/uniLights) and
 * uploads the whole block with glBufferData() whenever a value changes.
 * The GLES2-subset front end has no UBOs, so the block data still lives in
 * gl3state.uni{Common,2D,3D}Data but is pushed as individual glUniform*()
 * calls, keyed by the flattened member names used in the shaders above.
 * glGetUniformLocation() returns -1 for names a program doesn't declare, so
 * each push only touches the uniforms that actually exist in that program.
 * ====================================================================== */

static void
pushUniCommon(GLuint prog)
{
	GLint loc;
	if(prog == 0)  return;

	loc = glGetUniformLocation(prog, "gamma");
	if(loc != -1)  glUniform1f(loc, gl3state.uniCommonData.gamma);

	loc = glGetUniformLocation(prog, "intensity");
	if(loc != -1)  glUniform1f(loc, gl3state.uniCommonData.intensity);

	loc = glGetUniformLocation(prog, "intensity2D");
	if(loc != -1)  glUniform1f(loc, gl3state.uniCommonData.intensity2D);

	loc = glGetUniformLocation(prog, "color");
	if(loc != -1)  glUniform4fv(loc, 1, gl3state.uniCommonData.color.Elements);
}

static void
pushUni2D(GLuint prog)
{
	GLint loc;
	if(prog == 0)  return;

	loc = glGetUniformLocation(prog, "trans");
	if(loc != -1)
	{
		glUniformMatrix4fv(loc, 1, GL_FALSE, gl3state.uni2DData.transMat4.Elements[0]);
	}
}

static void
pushUni3D(GLuint prog)
{
	GLint loc;
	if(prog == 0)  return;

	loc = glGetUniformLocation(prog, "transProjView");
	if(loc != -1)
	{
		glUniformMatrix4fv(loc, 1, GL_FALSE, gl3state.uni3DData.transProjViewMat4.Elements[0]);
	}

	loc = glGetUniformLocation(prog, "transModel");
	if(loc != -1)
	{
		glUniformMatrix4fv(loc, 1, GL_FALSE, gl3state.uni3DData.transModelMat4.Elements[0]);
	}

	/* EwokOS GPU down-port: the V3D textured/colour geometry kernels transform
	 * by a single premultiplied model-view-projection matrix exposed as "uMVP".
	 * Push proj*view*model so the alias program's uMVP matches the kernel
	 * uniform by name (gpu_resolve_app_uniforms).  Programs without a uMVP
	 * uniform resolve to -1 and are skipped. */
	loc = glGetUniformLocation(prog, "uMVP");
	if(loc != -1)
	{
		hmm_mat4 mvp = HMM_MultiplyMat4(gl3state.uni3DData.transProjViewMat4,
		                                gl3state.uni3DData.transModelMat4);
		glUniformMatrix4fv(loc, 1, GL_FALSE, mvp.Elements[0]);
	}

	loc = glGetUniformLocation(prog, "scroll");
	if(loc != -1)  glUniform1f(loc, gl3state.uni3DData.scroll);

	loc = glGetUniformLocation(prog, "time");
	if(loc != -1)  glUniform1f(loc, gl3state.uni3DData.time);

	loc = glGetUniformLocation(prog, "alpha");
	if(loc != -1)  glUniform1f(loc, gl3state.uni3DData.alpha);

	loc = glGetUniformLocation(prog, "overbrightbits");
	if(loc != -1)  glUniform1f(loc, gl3state.uni3DData.overbrightbits);

	loc = glGetUniformLocation(prog, "particleFadeFactor");
	if(loc != -1)  glUniform1f(loc, gl3state.uni3DData.particleFadeFactor);

	loc = glGetUniformLocation(prog, "lightScaleForTurb");
	if(loc != -1)  glUniform1f(loc, gl3state.uni3DData.lightScaleForTurb);
}

/* Push every flattened uniform block into the given program.  Called from
 * GL3_UseProgram() right after glUseProgram() so a freshly bound program has
 * the current values even though nothing changed the UBO "blocks". */
void
GL3_ApplyAllUniforms(GLuint prog)
{
	pushUniCommon(prog);
	pushUni2D(prog);
	pushUni3D(prog);
}

static qboolean
initShader2D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc,
	qboolean uniCommonRequired)
{
	GLuint shaders2D[2] = {0};
	GLuint prog = 0;

	(void)uniCommonRequired; // no UBO blocks in the down-port

	if(shaderInfo->shaderProgram != 0)
	{
		Com_Printf("WARNING: calling %s for gl3ShaderInfo_t that already has a shaderProgram!\n",
			__func__);
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScalesOrTime = -1;
	shaderInfo->uniVblend = -1;

	shaders2D[0] = CompileShader(GL_VERTEX_SHADER, vertSrc, NULL);
	if(shaders2D[0] == 0)  return false;

	shaders2D[1] = CompileShader(GL_FRAGMENT_SHADER, fragSrc, NULL);
	if(shaders2D[1] == 0)
	{
		glDeleteShader(shaders2D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders2D);

	// the shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders2D[0]);
	glDeleteShader(shaders2D[1]);

	if(prog == 0)
	{
		return false;
	}

	shaderInfo->shaderProgram = prog;
	GL3_UseProgram(prog);

	shaderInfo->uniLmScalesOrTime = glGetUniformLocation(prog, "time");
	if(shaderInfo->uniLmScalesOrTime != -1)
	{
		glUniform1f(shaderInfo->uniLmScalesOrTime, 0.0f);
	}

	shaderInfo->uniVblend = glGetUniformLocation(prog, "v_blend");
	if(shaderInfo->uniVblend != -1)
	{
		glUniform4f(shaderInfo->uniVblend, 0, 0, 0, 0);
	}

	return true;
}

static qboolean
initShader3D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders3D[2] = {0};
	GLuint prog = 0;
	int i=0;

	if(shaderInfo->shaderProgram != 0)
	{
		Com_Printf("WARNING: calling initShader3D for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScalesOrTime = -1;
	shaderInfo->uniVblend = -1;

	shaders3D[0] = CompileShader(GL_VERTEX_SHADER, vertexCommon3D, vertSrc);
	if(shaders3D[0] == 0)  return false;

	shaders3D[1] = CompileShader(GL_FRAGMENT_SHADER, fragmentCommon3D, fragSrc);
	if(shaders3D[1] == 0)
	{
		glDeleteShader(shaders3D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders3D);

	if(prog == 0)
	{
		goto err_cleanup;
	}

	GL3_UseProgram(prog);

	// make sure texture is GL_TEXTURE0
	GLint texLoc = glGetUniformLocation(prog, "tex");
	if(texLoc != -1)
	{
		glUniform1i(texLoc, 0);
	}

	// ..  and the 4 lightmap textures use GL_TEXTURE1..4
	char lmName[10] = "lightmapX";
	for(i=0; i<4; ++i)
	{
		lmName[8] = '0'+i;
		GLint lmLoc = glGetUniformLocation(prog, lmName);
		if(lmLoc != -1)
		{
			glUniform1i(lmLoc, i+1); // lightmap0 belongs to GL_TEXTURE1, lightmap1 to GL_TEXTURE2 etc
		}
	}

	GLint lmScalesLoc = glGetUniformLocation(prog, "lmScales");
	shaderInfo->uniLmScalesOrTime = lmScalesLoc;
	if(lmScalesLoc != -1)
	{
		shaderInfo->lmScales[0] = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f);

		for(i=1; i<4; ++i)  shaderInfo->lmScales[i] = HMM_Vec4(0.0f, 0.0f, 0.0f, 0.0f);

		glUniform4fv(lmScalesLoc, 4, shaderInfo->lmScales[0].Elements);
	}

	shaderInfo->shaderProgram = prog;

	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	return true;

err_cleanup:

	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	if(prog != 0)  glDeleteProgram(prog);

	return false;
}

/* Down-port: no UBOs to create, just seed the block data that the flat-uniform
 * push helpers below read from.  Called before createShaders() so that the
 * GL3_UseProgram() inside each initShader*() pushes sane defaults. */
static void initUBOs(void)
{
	gl3state.uniCommonData.gamma = 1.0f/vid_gamma->value;
	gl3state.uniCommonData.intensity = gl3_intensity->value;
	gl3state.uniCommonData.intensity2D = gl3_intensity_2D->value;
	gl3state.uniCommonData.color = HMM_Vec4(1, 1, 1, 1);

	// the matrix will be set to something more useful later, before being used
	gl3state.uni2DData.transMat4 = HMM_Mat4();

	// the matrices will be set to something more useful later, before being used
	gl3state.uni3DData.transProjViewMat4 = HMM_Mat4();
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	gl3state.uni3DData.scroll = 0.0f;
	gl3state.uni3DData.time = 0.0f;
	gl3state.uni3DData.alpha = 1.0f;
	// gl3_overbrightbits 0 means "no scaling" which is equivalent to multiplying with 1
	gl3state.uni3DData.overbrightbits = (gl3_overbrightbits->value <= 0.0f) ? 1.0f : gl3_overbrightbits->value;
	gl3state.uni3DData.particleFadeFactor = gl3_particle_fade_factor->value;
	gl3state.uni3DData.lightScaleForTurb = 1.0f;

	gl3state.currentUBO = 0; // no UBO objects in the down-port
}

static qboolean createShaders(void)
{
	if (!initShader2D(&gl3state.si2D, vertexSrc2D, fragmentSrc2D, true))
	{
		Com_Printf("WARNING: Failed to create shader program for textured 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2Dtinted, vertexSrc2D, fragmentSrc2Dtinted, true))
	{
		Com_Printf("WARNING: Failed to create shader program for tinted 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2Dcolor, vertexSrc2Dcolor, fragmentSrc2Dcolor, true))
	{
		Com_Printf("WARNING: Failed to create shader program for color-only 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2DpostProcess, vertexSrc2D, fragmentSrc2Dpostprocess, false))
	{
		Com_Printf("WARNING: Failed to create shader program to render framebuffer object!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2DpostProcessWater, vertexSrc2D, fragmentSrc2DpostprocessWater, false))
	{
		Com_Printf("WARNING: Failed to create shader program to render framebuffer object under water!\n");
		return false;
	}

	const char* lightmappedFrag = (gl3_colorlight->value == 0.0f)
	                               ? fragmentSrc3DlmNoColor : fragmentSrc3Dlm;

	if(!initShader3D(&gl3state.si3Dlm, vertexSrc3Dlm, lightmappedFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for textured 3D rendering with lightmap!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dtrans, vertexSrc3D, fragmentSrc3D))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering translucent 3D things!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DcolorOnly, vertexSrc3D, fragmentSrc3Dcolor))
	{
		Com_Printf("WARNING: Failed to create shader program for flat-colored 3D rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dturb, vertexSrc3Dwater, fragmentSrc3Dwater))
	{
		Com_Printf("WARNING: Failed to create shader program for water rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DlmFlow, vertexSrc3DlmFlow, lightmappedFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for scrolling textured 3D rendering with lightmap!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DtransFlow, vertexSrc3Dflow, fragmentSrc3D))
	{
		Com_Printf("WARNING: Failed to create shader program for scrolling textured translucent 3D rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dsky, vertexSrc3D, fragmentSrc3Dsky))
	{
		Com_Printf("WARNING: Failed to create shader program for sky rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dsprite, vertexSrc3D, fragmentSrc3Dsprite))
	{
		Com_Printf("WARNING: Failed to create shader program for sprite rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DspriteAlpha, vertexSrc3D, fragmentSrc3DspriteAlpha))
	{
		Com_Printf("WARNING: Failed to create shader program for alpha-tested sprite rendering!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3Dalias, vertexSrcAlias, fragmentSrcAlias))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering textured models!\n");
		return false;
	}
	if(!initShader3D(&gl3state.si3DaliasColor, vertexSrcAlias, fragmentSrcAliasColor))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering flat-colored models!\n");
		return false;
	}

	const char* particleFrag = fragmentSrcParticles;
	if(gl3_particle_square->value != 0.0f)
	{
		particleFrag = fragmentSrcParticlesSquare;
	}

	if(!initShader3D(&gl3state.siParticle, vertexSrcParticles, particleFrag))
	{
		Com_Printf("WARNING: Failed to create shader program for rendering particles!\n");
		return false;
	}

	gl3state.currentShaderProgram = 0;

	return true;
}

qboolean GL3_InitShaders(void)
{
	initUBOs();

	return createShaders();
}

static void deleteShaders(void)
{
	const gl3ShaderInfo_t siZero = {0};
	for(gl3ShaderInfo_t* si = &gl3state.si2D; si <= &gl3state.siParticle; ++si)
	{
		if(si->shaderProgram != 0)  glDeleteProgram(si->shaderProgram);
		*si = siZero;
	}
}

void GL3_ShutdownShaders(void)
{
	deleteShaders();

	// no UBO objects were created in the down-port; just clear the handles
	gl3state.uniCommonUBO = gl3state.uni2DUBO = gl3state.uni3DUBO = gl3state.uniLightsUBO = 0;
	gl3state.currentUBO = 0;
}

qboolean GL3_RecreateShaders(void)
{
	// delete and recreate the existing shaders
	deleteShaders();
	return createShaders();
}

/* The upstream updateUBO() uploaded a whole block into a UBO with
 * glBufferData().  The down-port pushes the same data as flat uniforms into
 * the currently bound program instead. */

void GL3_UpdateUBOCommon(void)
{
	++gl3_numBufferUniforms;
	pushUniCommon(gl3state.currentShaderProgram);
}

void GL3_UpdateUBO2D(void)
{
	++gl3_numBufferUniforms;
	pushUni2D(gl3state.currentShaderProgram);
}

void GL3_UpdateUBO3D(void)
{
	++gl3_numBufferUniforms;
	pushUni3D(gl3state.currentShaderProgram);
}

void GL3_UpdateUBOLights(void)
{
	// dynamic lights were dropped from the down-ported lightmapped shader;
	// nothing to upload.
	++gl3_numBufferUniforms;
}
