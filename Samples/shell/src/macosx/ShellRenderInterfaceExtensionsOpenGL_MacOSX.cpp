/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Ported from AGL (Apple's legacy, now-removed GL context API) to
 * NSOpenGLContext. gl_context is stored as an opaque void* in the shared
 * ShellRenderInterfaceOpenGL header so non-Objective-C++ sample sources
 * don't need to know about NSOpenGLContext; it's cast back here.
 */

#include <ShellRenderInterfaceExtensions.h>
#include <ShellRenderInterfaceOpenGL.h>
#include <Rocket/Core/Context.h>
#include <Rocket/Core.h>
#include <Rocket/Core/Platform.h>

#import <Cocoa/Cocoa.h>

void ShellRenderInterfaceOpenGL::SetContext(void* context)
{
	m_rocket_context = context;
}

void ShellRenderInterfaceOpenGL::SetViewport(int width, int height)
{
	if (m_width != width || m_height != height)
	{
		m_width = width;
		m_height = height;

		glViewport(0, 0, width, height);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, width, height, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		if (gl_context != NULL)
			[(NSOpenGLContext*) gl_context update];
	}
	if (m_rocket_context != NULL)
	{
		((Rocket::Core::Context*) m_rocket_context)->SetDimensions(Rocket::Core::Vector2i(width, height));
	}
}

bool ShellRenderInterfaceOpenGL::AttachToNative(void* nativeWindow)
{
	NSWindow* window = (NSWindow*) nativeWindow;
	NSView* view = [window contentView];
	if (view == nil)
		return false;

	NSOpenGLPixelFormatAttribute attributes[] =
	{
		NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy,
		NSOpenGLPFADoubleBuffer,
		NSOpenGLPFAAlphaSize, 8,
		NSOpenGLPFADepthSize, 24,
		NSOpenGLPFAStencilSize, 8,
		NSOpenGLPFAAccelerated,
		0
	};

	NSOpenGLPixelFormat* pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
	if (pixel_format == nil)
		return false;

	NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixel_format shareContext:nil];
	[pixel_format release];
	if (context == nil)
		return false;

	[context setView:view];
	[context makeCurrentContext];

	this->gl_context = (void*) context;

	// Set up the GL state.
	glClearColor(0, 0, 0, 1);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	NSRect bounds = [view bounds];

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, bounds.size.width, bounds.size.height, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	return true;
}

void ShellRenderInterfaceOpenGL::DetachFromNative()
{
	if (gl_context != NULL)
	{
		NSOpenGLContext* context = (NSOpenGLContext*) gl_context;
		[NSOpenGLContext clearCurrentContext];
		[context clearDrawable];
		[context release];
		gl_context = NULL;
	}
}

void ShellRenderInterfaceOpenGL::PrepareRenderBuffer()
{
	glClear(GL_COLOR_BUFFER_BIT);
}

void ShellRenderInterfaceOpenGL::PresentRenderBuffer()
{
	if (gl_context != NULL)
		[(NSOpenGLContext*) gl_context flushBuffer];
}
