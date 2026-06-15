#include "App.h"

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

	App app;

	if (app.Initialize(hInstance, nCmdShow)) {

		return app.Run();

	}
	else
	{
		return 0;
	}

}