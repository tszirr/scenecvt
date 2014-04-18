#include "pch.h"

#include "stdx" 
#include "mathx"

#include <iostream>
#include <string>
#include <vector>

bool const stdx::is_debugger_present = IsDebuggerPresent() != FALSE;

int main()
{
	try
	{
	}
	catch (std::exception const &excpt)
	{
		std::cout << "Fatal error: " << excpt.what() << std::endl;
		return -1;
	}

	return 0;
}
