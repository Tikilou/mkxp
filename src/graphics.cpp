/*
** graphics.cpp
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

#include "graphics.h"

#include "util.h"
#include "gl-util.h"
#include "sharedstate.h"
#include "glstate.h"
#include "shader.h"
#include "scene.h"
#include "quad.h"
#include "eventthread.h"
#include "texpool.h"
#include "bitmap.h"
#include "etc-internal.h"
#include "binding.h"
#include "perftimer.h"

#include "SDL_video.h"
#include "SDL_timer.h"

#include <time.h>

struct PingPong
{
	TEXFBO rt[2];
	unsigned srcInd, dstInd;
	int screenW, screenH;

	PingPong(int screenW, int screenH)
	    : srcInd(0), dstInd(1),
	      screenW(screenW), screenH(screenH)
	{
		for (int i = 0; i < 2; ++i)
		{
			TEXFBO::init(rt[i]);
			TEXFBO::allocEmpty(rt[i], screenW, screenH);
			TEXFBO::linkFBO(rt[i]);
			glClearColor(0, 0, 0, 1);
			FBO::clear();
		}
	}

	~PingPong()
	{
		for (int i = 0; i < 2; ++i)
			TEXFBO::fini(rt[i]);
	}

	/* Binds FBO of last good buffer for reading */
	void bindLastBuffer()
	{
		FBO::bind(rt[dstInd].fbo, FBO::Read);
	}

	/* Better not call this during render cycles */
	void resize(int width, int height)
	{
		screenW = width;
		screenH = height;
		for (int i = 0; i < 2; ++i)
		{
			TEX::bind(rt[i].tex);
			TEX::allocEmpty(width, height);
		}
	}

	void startRender()
	{
		bind();
	}

	void swapRender()
	{
		swapIndices();

		/* Discard dest buffer */
		TEX::bind(rt[dstInd].tex);
		TEX::allocEmpty(screenW, screenH);

		bind();
	}

	void blitFBOs()
	{
		FBO::blit(0, 0, 0, 0, screenW, screenH);
	}

	void finishRender()
	{
		FBO::unbind(FBO::Draw);
		FBO::bind(rt[dstInd].fbo, FBO::Read);
	}

private:
	void bind()
	{
		TEX::bind(rt[srcInd].tex);
		FBO::bind(rt[srcInd].fbo, FBO::Read);
		FBO::bind(rt[dstInd].fbo, FBO::Draw);
	}

	void swapIndices()
	{
		unsigned tmp = srcInd;
		srcInd = dstInd;
		dstInd = tmp;
	}
};

class ScreenScene : public Scene
{
public:
	ScreenScene(int width, int height)
	    : pp(width, height),
	      actW(width), actH(height)
	{
		updateReso(width, height);

#ifdef RGSS2
		brightEffect = false;
		brightnessQuad.setColor(Vec4());
#endif
	}

	void composite()
	{
		const int w = geometry.rect.w;
		const int h = geometry.rect.h;

		shState->prepareDraw();

		pp.startRender();

		glState.viewport.set(IntRect(0, 0, w, h));

		FBO::clear();

		Scene::composite();

#ifdef RGSS2
		if (brightEffect)
		{
			SimpleColorShader &shader = shState->simpleColorShader();
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());

			brightnessQuad.draw();
		}
#endif

		pp.finishRender();
	}

	void requestViewportRender(Vec4 &c, Vec4 &f, Vec4 &t)
	{
		pp.swapRender();
		pp.blitFBOs();

		PlaneShader &shader = shState->planeShader();
		shader.bind();
		shader.setColor(c);
		shader.setFlash(f);
		shader.setTone(t);
		shader.applyViewportProj();
		shader.setTexSize(geometry.rect.size());

		glState.blendMode.pushSet(BlendNone);

		screenQuad.draw();

		glState.blendMode.pop();
		shader.unbind();
	}

#ifdef RGSS2
	void setBrightness(float norm)
	{
		brightnessQuad.setColor(Vec4(0, 0, 0, 1.0 - norm));

		brightEffect = norm < 1.0;
	}
#endif

	void updateReso(int width, int height)
	{
		geometry.rect.w = width;
		geometry.rect.h = height;

		screenQuad.setTexPosRect(geometry.rect, geometry.rect);

#ifdef RGSS2
		brightnessQuad.setTexPosRect(geometry.rect, geometry.rect);
#endif

		notifyGeometryChange();
	}

	void setResolution(int width, int height)
	{
		pp.resize(width, height);
		updateReso(width, height);
	}

	void setScreenSize(int width, int height)
	{
		actW = width;
		actH = height;
	}

	PingPong &getPP()
	{
		return pp;
	}

private:
	PingPong pp;
	Quad screenQuad;
	int actW, actH;

#ifdef RGSS2
	Quad brightnessQuad;
	bool brightEffect;
#endif
};

struct FPSLimiter
{
	uint64_t lastTickCount;

	/* ticks per frame */
	int64_t tpf;

	/* Ticks per second */
	const uint64_t tickFreq;

	/* Ticks per milisecond */
	const uint64_t tickFreqMS;

	/* Ticks per nanosecond */
	const double tickFreqNS;

	/* Data for frame timing adjustment */
	struct
	{
		/* Last tick count */
		uint64_t last;

		/* How far behind/in front we are for ideal frame timing */
		int64_t idealDiff;

		bool resetFlag;
	} adj;

	FPSLimiter(uint16_t desiredFPS)
	    : lastTickCount(SDL_GetPerformanceCounter()),
	      tickFreq(SDL_GetPerformanceFrequency()),
	      tickFreqMS(tickFreq / 1000),
	      tickFreqNS(tickFreq / 1000000000)
	{
		setDesiredFPS(desiredFPS);

		adj.last = SDL_GetPerformanceCounter();
		adj.idealDiff = 0;
		adj.resetFlag = false;
	}

	void setDesiredFPS(uint16_t value)
	{
		tpf = tickFreq / value;
	}

	void delay()
	{
		int64_t tickDelta = SDL_GetPerformanceCounter() - lastTickCount;
		int64_t toDelay = tpf - tickDelta;

		/* Compensate for the last delta
		 * to the ideal timestep */
		toDelay -= adj.idealDiff;

		if (toDelay < 0)
			toDelay = 0;

		delayTicks(toDelay);

		uint64_t now = lastTickCount = SDL_GetPerformanceCounter();
		int64_t diff = now - adj.last;
		adj.last = now;

		/* Recalculate our temporal position
		 * relative to the ideal timestep */
		adj.idealDiff = diff - tpf + adj.idealDiff;

		if (adj.resetFlag)
		{
			adj.idealDiff = 0;
			adj.resetFlag = false;
		}
	}

	void resetFrameAdjust()
	{
		adj.resetFlag = true;
	}

	/* If we're more than a full frame's worth
	 * of ticks behind the ideal timestep,
	 * there's no choice but to skip frame(s)
	 * to catch up */
	bool frameSkipRequired() const
	{
		return adj.idealDiff > tpf;
	}

private:
	void delayTicks(uint64_t ticks)
	{
#ifdef HAVE_NANOSLEEP
		struct timespec req;
		req.tv_sec = 0;
		req.tv_nsec = ticks / tickFreqNS;
		while (nanosleep(&req, &req) == -1)
			;
#else
		SDL_Delay(ticks / tickFreqMS);
#endif
	}
};

struct GraphicsPrivate
{
	/* Screen resolution, ie. the resolution at which
	 * RGSS renders at (settable with Graphics.resize_screen).
	 * Can only be changed from within RGSS */
	Vec2i scRes;

	/* Screen size, to which the rendered frames are scaled up.
	 * This can be smaller than the window size when fixed aspect
	 * ratio is enforced */
	Vec2i scSize;

	/* Actual physical size of the game window */
	Vec2i winSize;

	/* Offset in the game window at which the scaled game screen
	 * is blitted inside the game window */
	Vec2i scOffset;

	ScreenScene screen;
	RGSSThreadData *threadData;

	int frameRate;
	int frameCount;

#ifdef RGSS2
	int brightness;
#endif

	FPSLimiter fpsLimiter;

	PerfTimer *gpuTimer;
	PerfTimer *cpuTimer;

	bool frozen;
	TEXFBO frozenScene;
	TEXFBO currentScene;
	Quad screenQuad;
	RBOFBO transBuffer;

	GraphicsPrivate()
	    : scRes(640, 480),
	      scSize(scRes),
	      winSize(scRes),
	      screen(scRes.x, scRes.y),
	      frameRate(40),
	      frameCount(0),
#ifdef RGSS2
	      brightness(255),
#endif
	      fpsLimiter(frameRate),
	      frozen(false)
	{
		gpuTimer = createGPUTimer(frameRate);
		cpuTimer = createCPUTimer(frameRate);

		TEXFBO::init(frozenScene);
		TEXFBO::allocEmpty(frozenScene, scRes.x, scRes.y);
		TEXFBO::linkFBO(frozenScene);

		TEXFBO::init(currentScene);
		TEXFBO::allocEmpty(currentScene, scRes.x, scRes.y);
		TEXFBO::linkFBO(currentScene);

		FloatRect screenRect(0, 0, scRes.x, scRes.y);
		screenQuad.setTexPosRect(screenRect, screenRect);

		RBOFBO::init(transBuffer);
		RBOFBO::allocEmpty(transBuffer, scRes.x, scRes.y);
		RBOFBO::linkFBO(transBuffer);
	}

	~GraphicsPrivate()
	{
		delete gpuTimer;
		delete cpuTimer;

		TEXFBO::fini(frozenScene);
		TEXFBO::fini(currentScene);

		RBOFBO::fini(transBuffer);
	}

	void updateScreenResoRatio()
	{
		Vec2 &ratio = shState->rtData().sizeResoRatio;
		ratio.x = (float) scRes.x / scSize.x;
		ratio.y = (float) scRes.y / scSize.y;

		shState->rtData().screenOffset = scOffset;
	}

	/* Enforces fixed aspect ratio, if desired */
	void recalculateScreenSize()
	{
		scSize = winSize;

		if (!threadData->config.fixedAspectRatio)
		{
			scOffset = Vec2i(0, 0);
			return;
		}

		float resRatio = (float) scRes.x / scRes.y;
		float winRatio = (float) winSize.x / winSize.y;

		if (resRatio > winRatio)
			scSize.y = scSize.x / resRatio;
		else if (resRatio < winRatio)
			scSize.x = scSize.y * resRatio;

		scOffset.x = (winSize.x - scSize.x) / 2.f;
		scOffset.y = (winSize.y - scSize.y) / 2.f;
	}

	void checkResize()
	{
		if (threadData->windowSizeMsg.pollChange(&winSize.x, &winSize.y))
		{
			recalculateScreenSize();
			screen.setScreenSize(scSize.x, scSize.y);
			updateScreenResoRatio();
		}
	}

	void shutdown()
	{
		threadData->rqTermAck = true;
		shState->texPool().disable();

		scriptBinding->terminate();
	}

	void swapGLBuffer()
	{
		fpsLimiter.delay();
		SDL_GL_SwapWindow(threadData->window);

		++frameCount;

		threadData->ethread->notifyFrame();
	}

	void compositeToBuffer(FBO::ID fbo)
	{
		screen.composite();
		FBO::bind(fbo, FBO::Draw);
		FBO::blit(0, 0, 0, 0, scRes.x, scRes.y);
	}

	void blitBufferFlippedScaled()
	{
		FBO::blit(0, 0, scRes.x, scRes.y,
		          scOffset.x, scSize.y+scOffset.y, scSize.x, -scSize.y,
		          threadData->config.smoothScaling ? FBO::Linear : FBO::Nearest);
	}

	/* Blits currently bound read FBO to screen (upside-down) */
	void blitToScreen()
	{
		FBO::unbind(FBO::Draw);
		FBO::clear();
		blitBufferFlippedScaled();
	}

	void redrawScreen()
	{
		screen.composite();
		blitToScreen();

		swapGLBuffer();
	}
};

Graphics::Graphics(RGSSThreadData *data)
{
	p = new GraphicsPrivate;
	p->threadData = data;

	if (data->config.fixedFramerate > 0)
		p->fpsLimiter.setDesiredFPS(data->config.fixedFramerate);
}

Graphics::~Graphics()
{
	delete p;
}

void Graphics::update()
{
	shState->checkShutdown();

//	p->cpuTimer->endTiming();
//	p->gpuTimer->startTiming();

	if (p->frozen)
		return;

	if (p->fpsLimiter.frameSkipRequired())
	{
		/* Skip frame */
		p->fpsLimiter.delay();
		++p->frameCount;
		p->threadData->ethread->notifyFrame();

		return;
	}

	p->checkResize();
	p->redrawScreen();

//	p->gpuTimer->endTiming();
//	p->cpuTimer->startTiming();
}

void Graphics::freeze()
{
	p->frozen = true;

	shState->checkShutdown();
	p->checkResize();

	/* Capture scene into frozen buffer */
	p->compositeToBuffer(p->frozenScene.fbo);
}

void Graphics::transition(int duration,
                          const char *filename,
                          int vague)
{
	vague = clamp(vague, 0, 512);
	Bitmap *transMap = filename ? new Bitmap(filename) : 0;

#ifdef RGSS2
	setBrightness(255);
#endif

	/* Capture new scene */
	p->compositeToBuffer(p->currentScene.fbo);

	/* If no transition bitmap is provided,
	 * we can use a simplified shader */
	if (transMap)
	{
		TransShader &shader = shState->transShader();
		shader.bind();
		shader.applyViewportProj();
		shader.setFrozenScene(p->frozenScene.tex);
		shader.setCurrentScene(p->currentScene.tex);
		shader.setTransMap(transMap->getGLTypes().tex);
		shader.setVague(vague / 512.0f);
		shader.setTexSize(p->scRes);
	}
	else
	{
		SimpleTransShader &shader = shState->sTransShader();
		shader.bind();
		shader.applyViewportProj();
		shader.setFrozenScene(p->frozenScene.tex);
		shader.setCurrentScene(p->currentScene.tex);
		shader.setTexSize(p->scRes);
	}

	glState.blendMode.pushSet(BlendNone);

	for (int i = 0; i < duration; ++i)
	{
		if (p->threadData->rqTerm)
		{
			delete transMap;
			p->shutdown();
		}

		const float prog = i * (1.0 / duration);

		if (transMap)
			shState->transShader().setProg(prog);
		else
			shState->sTransShader().setProg(prog);

		/* Draw the composed frame to a buffer first
		 * (we need this because we're skipping PingPong) */
		FBO::bind(p->transBuffer.fbo, FBO::Draw);
		FBO::clear();
		p->screenQuad.draw();

		p->checkResize();

		/* Then blit it flipped and scaled to the screen */
		FBO::bind(p->transBuffer.fbo, FBO::Read);
		p->blitToScreen();

		p->swapGLBuffer();
	}

	glState.blendMode.pop();

	delete transMap;

	p->frozen = false;
}

void Graphics::frameReset()
{
	p->fpsLimiter.resetFrameAdjust();
}

#undef RET_IF_DISP
#define RET_IF_DISP(x)

#undef CHK_DISP
#define CHK_DISP

DEF_ATTR_RD_SIMPLE(Graphics, FrameRate, int, p->frameRate)

DEF_ATTR_SIMPLE(Graphics, FrameCount, int, p->frameCount)

void Graphics::setFrameRate(int value)
{
	p->frameRate = clamp(value, 10, 120);

	if (p->threadData->config.fixedFramerate > 0)
		return;

	p->fpsLimiter.setDesiredFPS(p->frameRate);
}

#ifdef RGSS2

void Graphics::wait(int duration)
{
	for (int i = 0; i < duration; ++i)
	{
		shState->checkShutdown();
		p->checkResize();
		p->redrawScreen();
	}
}

void Graphics::fadeout(int duration)
{
	if (p->frozen)
		FBO::bind(p->frozenScene.fbo, FBO::Read);

	for (int i = duration-1; i > -1; --i)
	{
		setBrightness((255.0 / duration) * i);

		if (p->frozen)
		{
			p->blitToScreen();
			p->swapGLBuffer();
		}
		else
		{
			update();
		}
	}
}

void Graphics::fadein(int duration)
{
	if (p->frozen)
		FBO::bind(p->frozenScene.fbo, FBO::Read);

	for (int i = 0; i < duration; ++i)
	{
		setBrightness((255.0 / duration) * i);

		if (p->frozen)
		{
			p->blitToScreen();
			p->swapGLBuffer();
		}
		else
		{
			update();
		}
	}
}

Bitmap *Graphics::snapToBitmap()
{
	Bitmap *bitmap = new Bitmap(width(), height());

	p->compositeToBuffer(bitmap->getGLTypes().fbo);

	return bitmap;
}

int Graphics::width() const
{
	return p->scRes.x;
}

int Graphics::height() const
{
	return p->scRes.y;
}

void Graphics::resizeScreen(int width, int height)
{
	width = clamp(width, 1, 640);
	height = clamp(height, 1, 480);

	Vec2i size(width, height);

	if (p->scRes == size)
		return;

	shState->eThread().requestWindowResize(width, height);

	p->scRes = size;

	p->screen.setResolution(width, height);

	TEX::bind(p->frozenScene.tex);
	TEX::allocEmpty(width, height);
	TEX::bind(p->currentScene.tex);
	TEX::allocEmpty(width, height);

	FloatRect screenRect(0, 0, width, height);
	p->screenQuad.setTexPosRect(screenRect, screenRect);

	RBO::bind(p->transBuffer.rbo);
	RBO::allocEmpty(width, height);

	p->updateScreenResoRatio();
}

DEF_ATTR_RD_SIMPLE(Graphics, Brightness, int, p->brightness)

void Graphics::setBrightness(int value)
{
	value = clamp(value, 0, 255);

	if (p->brightness == value)
		return;

	p->brightness = value;
	p->screen.setBrightness(value / 255.0);
}

#endif

bool Graphics::getFullscreen() const
{
	return p->threadData->ethread->getFullscreen();
}

void Graphics::setFullscreen(bool value)
{
	p->threadData->ethread->requestFullscreenMode(value);
}

bool Graphics::getShowCursor() const
{
	return p->threadData->ethread->getShowCursor();
}

void Graphics::setShowCursor(bool value)
{
	p->threadData->ethread->requestShowCursor(value);
}

Scene *Graphics::getScreen() const
{
	return &p->screen;
}

void Graphics::repaintWait(volatile bool *exitCond)
{
	if (*exitCond)
		return;

	/* Repaint the screen with the last good frame we drew */
	p->screen.getPP().bindLastBuffer();
	FBO::unbind(FBO::Draw);

	while (!*exitCond)
	{
		shState->checkShutdown();

		FBO::clear();
		p->blitBufferFlippedScaled();
		SDL_GL_SwapWindow(p->threadData->window);
		p->fpsLimiter.delay();

		p->threadData->ethread->notifyFrame();
	}
}
