/*
** shader.h
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SHADER_H
#define SHADER_H

#include "etc-internal.h"
#include "gl-util.h"
#include "glstate.h"

class Shader
{
public:
	void bind();
	static void unbind();

	enum Attribute
	{
		Position = 0,
		TexCoord = 1,
		Color = 2
	};

protected:
	Shader();
	~Shader();

	void init(const unsigned char *vert, int vertSize,
	          const unsigned char *frag, int fragSize);
	void initFromFile(const char *vertFile, const char *fragFile);

	static void setVec4Uniform(GLint location, const Vec4 &vec);
	static void setTexUniform(GLint location, unsigned unitIndex, TEX::ID texture);

	GLuint vertShader, fragShader;
	GLuint program;
};

class ShaderBase : public Shader
{
public:

	struct GLProjMat : public GLProperty<Vec2i>
	{
	private:
		void apply(const Vec2i &value);
		GLint u_mat;

		friend class ShaderBase;
	};

	/* Stack is not used (only 'set()') */
	GLProjMat projMat;

	/* Retrieves the current glState.viewport size,
	 * calculates the corresponding ortho projection matrix
	 * and loads it into the shaders uniform */
	void applyViewportProj();

	void setTexSize(const Vec2i &value);
	void setTranslation(const Vec2i &value);

protected:
	void init();

	GLint u_texSizeInv, u_translation;
};

class SimpleShader : public ShaderBase
{
public:
	SimpleShader();

	void setTexOffsetX(int value);

private:
	GLint u_texOffsetX;
};

class SimpleColorShader : public ShaderBase
{
public:
	SimpleColorShader();
};

class SimpleAlphaShader : public ShaderBase
{
public:
	SimpleAlphaShader();
};

class SimpleSpriteShader : public ShaderBase
{
public:
	SimpleSpriteShader();

	void setSpriteMat(const float value[16]);

private:
	GLint u_spriteMat;
};

class TransShader : public ShaderBase
{
public:
	TransShader();

	void setCurrentScene(TEX::ID tex);
	void setFrozenScene(TEX::ID tex);
	void setTransMap(TEX::ID tex);
	void setProg(float value);
	void setVague(float value);

private:
	GLint u_currentScene, u_frozenScene, u_transMap, u_prog, u_vague;
};

class SimpleTransShader : public ShaderBase
{
public:
	SimpleTransShader();

	void setCurrentScene(TEX::ID tex);
	void setFrozenScene(TEX::ID tex);
	void setProg(float value);

private:
	GLint u_currentScene, u_frozenScene, u_prog;
};

class SpriteShader : public ShaderBase
{
public:
	SpriteShader();

	void setSpriteMat(const float value[16]);
	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setOpacity(float value);
	void setBushDepth(float value);
	void setBushOpacity(float value);

private:
	GLint u_spriteMat, u_tone, u_opacity, u_color, u_bushDepth, u_bushOpacity;
};

class PlaneShader : public ShaderBase
{
public:
	PlaneShader();

	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setFlash(const Vec4 &value);
	void setOpacity(float value);

private:
	GLint u_tone, u_color, u_flash, u_opacity;
};

class FlashMapShader : public ShaderBase
{
public:
	FlashMapShader();

	void setAlpha(float value);

private:
	GLint u_alpha;
};

class HueShader : public ShaderBase
{
public:
	HueShader();

	void setHueAdjust(float value);
	void setInputTexture(TEX::ID tex);

private:
	GLint u_hueAdjust, u_inputTexture;
};

#ifdef RGSS2
class SimpleMatrixShader : public ShaderBase
{
public:
	SimpleMatrixShader();

	void setMatrix(const float value[16]);

private:
	GLint u_matrix;
};

/* Gaussian blur */
struct BlurShader
{
	class HPass : public ShaderBase
	{
	public:
		HPass();
	};

	class VPass : public ShaderBase
	{
	public:
		VPass();
	};

	HPass pass1;
	VPass pass2;
};
#endif

/* Bitmap blit */
class BltShader : public ShaderBase
{
public:
	BltShader();

	void setSource();
	void setDestination(const TEX::ID value);
	void setDestCoorF(const Vec2 &value);
	void setSubRect(const FloatRect &value);
	void setOpacity(float value);

private:
	GLint u_source, u_destination, u_subRect, u_opacity;
};

#endif // SHADER_H
