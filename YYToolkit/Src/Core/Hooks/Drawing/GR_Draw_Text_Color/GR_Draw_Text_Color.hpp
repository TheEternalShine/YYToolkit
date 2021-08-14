#pragma once

namespace Hooks
{
	namespace GR_Draw_Text_Color
	{
		void Function(float x, float y, const char* str, int linesep, int linewidth, unsigned int c1, unsigned int c2, unsigned int c3, float alpha, unsigned int c4);
		void* GetTargetAddress();

		inline decltype(&Function) pfnOriginal;
	}
}