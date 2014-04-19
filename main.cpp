#include "pch.h"

#include "stdx" 
#include "mathx"

#include <iostream>
#include <string>
#include <vector>

#include <filex>

bool const stdx::is_debugger_present = IsDebuggerPresent() != FALSE;

static bool touch_dont_overwrite = false;

int scene_tool(char const* tool, char const* const* args, char const* const* args_end);
int help_tool(char const* tool, char const* const* args, char const* const* args_end);

char const* tools[] = {
	  "scene"
	, "help"
};

int main(int argc, const char* argv[])
{
	auto args = argv + 1;
	auto arg_end = argv + argc;

	// tool identifier
	auto tool = (args < arg_end) ? *args++ : "help";

	// batch mode
	if (args < arg_end && stdx::strieq(*args, "batch"))
	{
		touch_dont_overwrite = true;
		++args;
	}

	try
	{
		if (stdx::strieq(tool, "scene"))
			return scene_tool(tool, args, arg_end);
		else
			return help_tool(tool, args, arg_end);
	}
	catch (std::exception const &excpt)
	{
		std::cout << "Fatal error: " << excpt.what() << std::endl;
		return -1;
	}

	return 0;
}

// Stores the given command in a batch file to be played back later.
void record_command(const char *tool, const char *file, const char *const *args, size_t argCount)
{
	std::string batFilename;
	batFilename.reserve(strlen(file) + strlen(tool) + arraylen("..scenecvt.rc.bat"));
	batFilename.append(file);
	batFilename.append(".");
	batFilename.append(tool);
	batFilename.append(".rc.bat");

	bool need_update = true;

	if (touch_dont_overwrite)
		// Skip command update if touch successful
		need_update = !stdx::file_touch(batFilename.c_str());

	if (need_update)
	{
		auto batFile = stdx::write_file(batFilename.c_str(), std::ios_base::trunc);

		batFile << "scenecvt " << tool << " batch";
		
		for (size_t i = 0; i < argCount; ++i)
		{
			batFile << ' ';

			auto argCursor = args[i];
			auto argInsertRoot = strchr(argCursor, '@');

			bool need_quotes = (argInsertRoot || strchr(argCursor, ' '));
			if (need_quotes) batFile << '"';

			while (argInsertRoot)
			{
				batFile.write(argCursor, argInsertRoot - argCursor);
				batFile << "%~dp0";
				
				argCursor = argInsertRoot + 1;
				argInsertRoot = strchr(argCursor, '@');
			}
			batFile << argCursor;

			if (need_quotes) batFile << '"';
		}
	}
}

int help_tool(char const* tool, char const* const* args, char const* const* args_end)
{
	std::cout << "****************************************************************" << std::endl;
	std::cout << " scenecvt                                         lighter tools " << std::endl;
	std::cout << "****************************************************************" << std::endl << std::endl;

	std::cout << " Syntax: scenecvt <tool> <args ...>"  << std::endl << std::endl;

	std::cout << " Tools:"  << std::endl;

	for (auto tool : tools)
		std::cout << " -> " << tool << std::endl;

	std::cout << std::endl;
	
	std::cout << "Press ENTER to exit ...";
	std::cin.get();

	return 0;
}
