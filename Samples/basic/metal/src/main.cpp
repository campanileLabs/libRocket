/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
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
 */

#include <Rocket/Core.h>
#include <Rocket/Debugger.h>
#include <Input.h>
#include <Shell.h>
#include "RenderInterfaceMetal.h"

Rocket::Core::Context* context = NULL;

ShellRenderInterfaceExtensions* shell_renderer;

Rocket::Core::ElementDocument* fps_document = NULL;
float fps_timer = 0;
int fps_frames = 0;

void UpdateFPS()
{
	fps_frames++;

	float now = Rocket::Core::GetSystemInterface()->GetElapsedTime();
	float elapsed = now - fps_timer;
	if (elapsed >= 0.5f)
	{
		float fps = fps_frames / elapsed;
		if (fps_document != NULL)
			fps_document->SetInnerRML(Rocket::Core::String(32, "FPS: %.1f", fps));

		fps_timer = now;
		fps_frames = 0;
	}
}

void GameLoop()
{
	context->Update();

	shell_renderer->PrepareRenderBuffer();
	context->Render();
	shell_renderer->PresentRenderBuffer();

	UpdateFPS();
}

int main(int ROCKET_UNUSED_PARAMETER(argc), char** ROCKET_UNUSED_PARAMETER(argv))
{
	ROCKET_UNUSED(argc);
	ROCKET_UNUSED(argv);

#define APP_PATH "../../Samples/basic/metal/"

	int window_width = 1024;
	int window_height = 768;

	RenderInterfaceMetal metal_renderer;
	shell_renderer = &metal_renderer;

	// Generic OS initialisation, creates a window and attaches a native Metal device.
	if (!Shell::Initialise(APP_PATH) ||
		!Shell::OpenWindow("Metal Sample", shell_renderer, window_width, window_height, true))
	{
		Shell::Shutdown();
		return -1;
	}

	// Rocket initialisation.
	Rocket::Core::SetRenderInterface(&metal_renderer);
	shell_renderer->SetViewport(window_width, window_height);

	ShellSystemInterface system_interface;
	Rocket::Core::SetSystemInterface(&system_interface);

	Rocket::Core::Initialise();

	// Create the main Rocket context and set it on the shell's input layer.
	context = Rocket::Core::CreateContext("main", Rocket::Core::Vector2i(window_width, window_height));
	if (context == NULL)
	{
		Rocket::Core::Shutdown();
		Shell::Shutdown();
		return -1;
	}

	Rocket::Debugger::Initialise(context);
	Input::SetContext(context);
	shell_renderer->SetContext(context);

	Shell::LoadFonts("../../assets/");

	// Load and show the demo document.
	Rocket::Core::ElementDocument* document = context->LoadDocument("../../assets/demo.rml");
	if (document != NULL)
	{
		document->Show();
		document->RemoveReference();
	}

	// Create a lightweight overlay document to display the frame rate.
	fps_document = context->CreateDocument();
	if (fps_document != NULL)
	{
		fps_document->SetProperty("position", "absolute");
		fps_document->SetProperty("top", "8px");
		fps_document->SetProperty("left", "8px");
		fps_document->SetProperty("width", "auto");
		fps_document->SetProperty("height", "auto");
		fps_document->SetProperty("padding", "4px 8px");
		fps_document->SetProperty("background-color", "rgba(0, 0, 0, 160)");
		fps_document->SetProperty("color", "white");
		fps_document->SetProperty("font-family", "Delicious");
		fps_document->SetProperty("font-size", "16px");
		fps_document->SetProperty("z-index", "10000");
		fps_document->SetInnerRML("FPS: 0.0");
		fps_document->Show(Rocket::Core::ElementDocument::NONE);
		fps_document->RemoveReference();
	}
	fps_timer = Rocket::Core::GetSystemInterface()->GetElapsedTime();

	Shell::EventLoop(GameLoop);

	// Shutdown Rocket.
	context->RemoveReference();
	Rocket::Core::Shutdown();

	Shell::CloseWindow();
	Shell::Shutdown();

	return 0;
}
