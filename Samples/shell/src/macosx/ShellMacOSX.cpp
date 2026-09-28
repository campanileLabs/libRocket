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
 * Ported from the original Carbon + AGL implementation to Cocoa + NSOpenGL;
 * Carbon's WindowRef/EventHandler APIs and the AGL framework no longer exist
 * on current macOS.
 */

#include "Shell.h"
#include <Rocket/Core.h>
#include "ShellFileInterface.h"
#include "macosx/InputMacOSX.h"
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>

#import <Cocoa/Cocoa.h>

// Forwards declared below; used by the globals further down.
@class RocketShellView;
@class RocketShellWindowDelegate;

static NSWindow* window = nil;
static RocketShellView* shell_view = nil;
static RocketShellWindowDelegate* window_delegate = nil;
static timeval start_time;
static bool running = false;

ShellFileInterface* file_interface = NULL;
static ShellRenderInterfaceExtensions* shell_renderer = NULL;

// The sample's per-frame render callback, stashed here so a live window
// resize can redraw immediately instead of leaving a stale framebuffer.
// AppKit runs live resizing (dragging a window edge) in its own nested
// tracking run loop, which blocks Shell::EventLoop's own while loop for the
// whole drag -- without this, the window would just freeze until mouse-up.
static Shell::ShellIdleFunction redraw_function = NULL;

//===================================
// RocketShellView =================
//===================================
// Captures mouse/keyboard NSEvents and forwards them to InputMacOSX.

@interface RocketShellView : NSView
@end

@implementation RocketShellView

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)isFlipped
{
	// Puts (0,0) at the top-left of the view, matching Rocket's coordinate space.
	return YES;
}

- (void)handleMouseMove:(NSEvent*)event
{
	NSPoint location = [self convertPoint:[event locationInWindow] fromView:nil];
	InputMacOSX::ProcessMouseMove((int) location.x, (int) location.y, [event modifierFlags]);
}

- (void)mouseDown:(NSEvent*)event
{
	InputMacOSX::ProcessMouseButtonDown(0, [event modifierFlags]);
}

- (void)mouseUp:(NSEvent*)event
{
	InputMacOSX::ProcessMouseButtonUp(0, [event modifierFlags]);
}

- (void)rightMouseDown:(NSEvent*)event
{
	InputMacOSX::ProcessMouseButtonDown(1, [event modifierFlags]);
}

- (void)rightMouseUp:(NSEvent*)event
{
	InputMacOSX::ProcessMouseButtonUp(1, [event modifierFlags]);
}

- (void)otherMouseDown:(NSEvent*)event
{
	InputMacOSX::ProcessMouseButtonDown((int) [event buttonNumber], [event modifierFlags]);
}

- (void)otherMouseUp:(NSEvent*)event
{
	InputMacOSX::ProcessMouseButtonUp((int) [event buttonNumber], [event modifierFlags]);
}

- (void)mouseMoved:(NSEvent*)event
{
	[self handleMouseMove:event];
}

- (void)mouseDragged:(NSEvent*)event
{
	[self handleMouseMove:event];
}

- (void)rightMouseDragged:(NSEvent*)event
{
	[self handleMouseMove:event];
}

- (void)otherMouseDragged:(NSEvent*)event
{
	[self handleMouseMove:event];
}

- (void)scrollWheel:(NSEvent*)event
{
	InputMacOSX::ProcessMouseWheel((float) -[event deltaY], [event modifierFlags]);
}

- (void)keyDown:(NSEvent*)event
{
	InputMacOSX::ProcessKeyDown([event keyCode], [event modifierFlags]);
}

- (void)keyUp:(NSEvent*)event
{
	InputMacOSX::ProcessKeyUp([event keyCode], [event modifierFlags]);
}

@end

//===================================
// RocketShellWindowDelegate =======
//===================================

@interface RocketShellWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation RocketShellWindowDelegate

- (BOOL)windowShouldClose:(id)sender
{
	(void) sender;
	Shell::RequestExit();
	// The window is actually torn down by Shell::CloseWindow() once the
	// event loop exits, not here.
	return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
	NSWindow* resized_window = (NSWindow*) [notification object];
	NSRect content_bounds = [resized_window contentRectForFrameRect:[resized_window frame]];

	if (shell_renderer != NULL)
		shell_renderer->SetViewport((int) content_bounds.size.width, (int) content_bounds.size.height);

	// Redraw now, at the new size, rather than waiting for control to return
	// to the main loop (which won't happen until the drag ends).
	if (redraw_function != NULL)
		redraw_function();
}

@end

//===================================
// Shell ============================
//===================================

bool Shell::Initialise(const Rocket::Core::String& path)
{
	gettimeofday(&start_time, NULL);

	InputMacOSX::Initialise();

	// Find the location of the executable.
	CFBundleRef bundle = CFBundleGetMainBundle();
	CFURLRef executable_url = CFBundleCopyExecutableURL(bundle);
	CFStringRef executable_posix_file_name = CFURLCopyFileSystemPath(executable_url, kCFURLPOSIXPathStyle);
	CFIndex max_length = CFStringGetMaximumSizeOfFileSystemRepresentation(executable_posix_file_name);
	char* executable_file_name = new char[max_length];
	if (!CFStringGetFileSystemRepresentation(executable_posix_file_name, executable_file_name, max_length))
		executable_file_name[0] = 0;

	executable_path = Rocket::Core::String(executable_file_name);
	executable_path = executable_path.Substring(0, executable_path.RFind("/") + 1);

	delete[] executable_file_name;
	CFRelease(executable_posix_file_name);
	CFRelease(executable_url);

	file_interface = new ShellFileInterface(executable_path + "../../../" + path);
	Rocket::Core::SetFileInterface(file_interface);

	return true;
}

void Shell::Shutdown()
{
	delete file_interface;
	file_interface = NULL;
}

bool Shell::OpenWindow(const char* name, ShellRenderInterfaceExtensions* _shell_renderer, unsigned int width, unsigned int height, bool allow_resize)
{
	shell_renderer = _shell_renderer;

	[NSApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

	NSUInteger style_mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
	if (allow_resize)
		style_mask |= NSWindowStyleMaskResizable;

	NSRect content_rect = NSMakeRect(60, 20, width, height);

	window = [[NSWindow alloc] initWithContentRect:content_rect
										  styleMask:style_mask
											backing:NSBackingStoreBuffered
											  defer:NO];
	if (window == nil)
		return false;

	[window setTitle:[NSString stringWithUTF8String:name]];
	[window setReleasedWhenClosed:NO];
	[window setAcceptsMouseMovedEvents:YES];

	window_delegate = [[RocketShellWindowDelegate alloc] init];
	[window setDelegate:window_delegate];

	shell_view = [[RocketShellView alloc] initWithFrame:content_rect];
	[window setContentView:shell_view];
	[window makeFirstResponder:shell_view];

	[window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];

	if (shell_renderer != NULL)
	{
		if (!shell_renderer->AttachToNative((void*) window))
			return false;
	}

	return true;
}

void Shell::CloseWindow()
{
	if (shell_renderer != NULL)
		shell_renderer->DetachFromNative();

	if (window != nil)
	{
		[window setDelegate:nil];
		[window orderOut:nil];
		[window release];
		window = nil;
	}

	if (window_delegate != nil)
	{
		[window_delegate release];
		window_delegate = nil;
	}

	if (shell_view != nil)
	{
		[shell_view release];
		shell_view = nil;
	}
}

void* Shell::GetWindowHandle()
{
	return (void*) window;
}

void Shell::EventLoop(ShellIdleFunction idle_function)
{
	running = true;
	redraw_function = idle_function;

	[NSApp finishLaunching];

	while (running)
	{
		NSEvent* event;
		while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
											 untilDate:[NSDate distantPast]
												inMode:NSDefaultRunLoopMode
											   dequeue:YES]) != nil)
		{
			[NSApp sendEvent:event];
		}

		idle_function();

		// Roughly matches the original Carbon idle timer's 200Hz cadence.
		usleep(5000);
	}

	redraw_function = NULL;
}

void Shell::RequestExit()
{
	running = false;
}

void Shell::DisplayError(const char* fmt, ...)
{
	const int buffer_size = 1024;
	char buffer[buffer_size];
	va_list argument_list;

	// Print the message to the buffer.
	va_start(argument_list, fmt);
	int len = vsnprintf(buffer, buffer_size - 2, fmt, argument_list);
	if (len < 0 || len > buffer_size - 2)
	{
		len = buffer_size - 2;
	}
	buffer[len] = '\n';
	buffer[len + 1] = '\0';
	va_end(argument_list);

	fprintf(stderr, "%s", buffer);
}

void Shell::Log(const char* fmt, ...)
{
	const int buffer_size = 1024;
	char buffer[buffer_size];
	va_list argument_list;

	// Print the message to the buffer.
	va_start(argument_list, fmt);
	int len = vsnprintf(buffer, buffer_size - 2, fmt, argument_list);
	if (len < 0 || len > buffer_size - 2)
	{
		len = buffer_size - 2;
	}
	buffer[len] = '\n';
	buffer[len + 1] = '\0';
	va_end(argument_list);

	printf("%s", buffer);
}

float Shell::GetElapsedTime()
{
	struct timeval now;

	gettimeofday(&now, NULL);

	double sec = now.tv_sec - start_time.tv_sec;
	double usec = now.tv_usec;
	double result = sec + (usec / 1000000.0);

	return (float) result;
}
