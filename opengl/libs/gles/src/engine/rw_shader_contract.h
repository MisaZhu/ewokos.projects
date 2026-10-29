#ifndef RW_SHADER_CONTRACT_H
#define RW_SHADER_CONTRACT_H
static const char rw_simple_fs[] =
"precision highp float; precision highp int;"
"uniform vec4 u_alphaRef; uniform vec4 u_fogData; uniform vec4 u_fogColor;"
"void DoAlphaTest(float a) { if(a < u_alphaRef.x || a >= u_alphaRef.y) discard; }"
"uniform sampler2D tex0; varying vec4 v_color; varying vec2 v_tex0; varying float v_fog;"
"void main(void) { vec4 color = v_color*texture2D(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));"
"color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog); DoAlphaTest(color.a); (gl_FragColor = color); }";
static const char rw_matfx_fs[] =
"precision highp float; precision highp int;"
"uniform vec4 u_alphaRef; uniform vec4 u_fogData; uniform vec4 u_fogColor;"
"void DoAlphaTest(float a) { if(a < u_alphaRef.x || a >= u_alphaRef.y) discard; }"
"uniform sampler2D tex0; uniform sampler2D tex1; uniform vec4 u_fxparams;"
"varying vec4 v_color; varying vec4 v_envColor; varying vec2 v_tex0; varying vec2 v_tex1; varying float v_fog;"
"void main(void) { vec4 pass1 = v_color; pass1 *= texture2D(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));"
"vec4 pass2 = v_envColor*(u_fxparams.x)*texture2D(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));"
"pass1.rgb = mix(u_fogColor.rgb, pass1.rgb, v_fog);"
"pass2.rgb = mix(vec3(0.0, 0.0, 0.0), pass2.rgb, v_fog);"
"float fba = max(pass1.a, (u_fxparams.y)); vec4 color;"
"color.rgb = pass1.rgb*pass1.a + pass2.rgb*fba; color.a = pass1.a;"
"DoAlphaTest(color.a); (gl_FragColor = color); }";

#endif
