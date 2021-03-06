/*
** main.cpp
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

#include "glew.h"

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"

#include "sharedstate.h"
#include "eventthread.h"
#include "debuglogger.h"

#include "binding.h"

#include <QDebug>

static const char *reqExt[] =
{
	"GL_ARB_fragment_shader",
	"GL_ARB_shader_objects",
	"GL_ARB_vertex_shader",
	"GL_ARB_shading_language_100",
	"GL_ARB_texture_non_power_of_two",
	"GL_ARB_vertex_array_object",
	"GL_ARB_vertex_buffer_object",
	"GL_EXT_bgra",
	"GL_EXT_blend_func_separate",
	"GL_EXT_blend_subtract",
	"GL_EXT_framebuffer_object",
	"GL_EXT_framebuffer_blit",
	0
};

static void
rgssThreadError(RGSSThreadData *rtData, const QByteArray &msg)
{
	rtData->rgssErrorMsg = msg;
	rtData->ethread->requestTerminate();
	rtData->rqTermAck = true;
}

static inline const char*
glGetStringInt(GLenum name)
{
	return (const char*) glGetString(name);
}

static void
printGLInfo()
{
	qDebug() << "GL Vendor    :" << glGetStringInt(GL_VENDOR);
	qDebug() << "GL Renderer  :" << glGetStringInt(GL_RENDERER);
	qDebug() << "GL Version   :" << glGetStringInt(GL_VERSION);
	qDebug() << "GLSL Version :" << glGetStringInt(GL_SHADING_LANGUAGE_VERSION);
}

int rgssThreadFun(void *userdata)
{
	RGSSThreadData *threadData = static_cast<RGSSThreadData*>(userdata);
	SDL_Window *win = threadData->window;
	SDL_GLContext ctx;

	/* Setup GL context */
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	if (threadData->config.debugMode)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

	ctx = SDL_GL_CreateContext(win);

	if (!ctx)
	{
		rgssThreadError(threadData, QByteArray("Error creating context: ") + SDL_GetError());
		return 0;
	}

	if (glewInit() != GLEW_OK)
	{
		rgssThreadError(threadData, "Error initializing glew");
		SDL_GL_DeleteContext(ctx);
		return 0;
	}

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	SDL_GL_SwapWindow(win);

	printGLInfo();

	/* Check for required GL version */
	if (!GLEW_VERSION_2_0)
	{
		rgssThreadError(threadData, "At least OpenGL 2.0 is required");
		SDL_GL_DeleteContext(ctx);
		return 0;
	}

	/* Check for required GL extensions */
	for (int i = 0; reqExt[i]; ++i)
	{
		if (!glewIsSupported(reqExt[i]))
		{
			rgssThreadError(threadData, QByteArray("Required GL extension \"")
			                            + reqExt[i] + "\" not present");
			SDL_GL_DeleteContext(ctx);
			return 0;
		}
	}

	SDL_GL_SetSwapInterval(threadData->config.vsync ? 1 : 0);

	DebugLogger dLogger;

	SharedState::initInstance(threadData);

	/* Start script execution */
	scriptBinding->execute();

	threadData->rqTermAck = true;
	threadData->ethread->requestTerminate();

	SharedState::finiInstance();

	SDL_GL_DeleteContext(ctx);

	return 0;
}

int main(int, char *argv[])
{
	Config conf;

	conf.read();
	conf.readGameINI();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
	{
		qDebug() << "Error initializing SDL:" << SDL_GetError();
		return 0;
	}

	int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
	if (IMG_Init(imgFlags) != imgFlags)
	{
		qDebug() << "Error initializing SDL_image:" << SDL_GetError();
		SDL_Quit();
		return 0;
	}

	if (TTF_Init() < 0)
	{
		qDebug() << "Error initializing SDL_ttf:" << SDL_GetError();
		IMG_Quit();
		SDL_Quit();
		return 0;
	}

	SDL_SetHint("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");

	SDL_Window *win;
	Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;

	if (conf.winResizable)
		winFlags |= SDL_WINDOW_RESIZABLE;
	if (conf.fullscreen)
		winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	win = SDL_CreateWindow(conf.game.title.constData(),
	                       SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                       conf.defScreenW, conf.defScreenH, winFlags);

	if (!win)
	{
		qDebug() << "Error creating window";
		return 0;
	}

	EventThread eventThread;
	RGSSThreadData rtData(&eventThread, argv[0], win);
	rtData.config = conf;

	/* Start RGSS thread */
	SDL_Thread *rgssThread =
	        SDL_CreateThread(rgssThreadFun, "rgss", &rtData);

	/* Start event processing */
	eventThread.process(rtData);

	/* Request RGSS thread to stop */
	rtData.rqTerm = true;

	/* Wait for RGSS thread response */
	for (int i = 0; i < 1000; ++i)
	{
		/* We can stop waiting when the request was ack'd */
		if (rtData.rqTermAck)
		{
			qDebug() << "RGSS thread ack'd request after" << i*10 << "ms";
			break;
		}

		/* Give RGSS thread some time to respond */
		SDL_Delay(10);
	}

	/* If RGSS thread ack'd request, wait for it to shutdown,
	 * otherwise abandon hope and just end the process as is. */
	if (rtData.rqTermAck)
		SDL_WaitThread(rgssThread, 0);
	else
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.constData(),
		                         "The RGSS script seems to be stuck and mkxp will now force quit", win);

	if (!rtData.rgssErrorMsg.isEmpty())
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.constData(),
		                         rtData.rgssErrorMsg.constData(), win);

	/* Clean up any remainin events */
	eventThread.cleanup();

	qDebug() << "Shutting down.";

	SDL_DestroyWindow(win);

	TTF_Quit();
	IMG_Quit();
	SDL_Quit();

	return 0;
}
