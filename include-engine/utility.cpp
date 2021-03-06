#include "utility.h"

/////////////////
// fail_fast() //
/////////////////

#include <Windows.h>
#include <iostream>

void fail_fast()
{
    if(IsDebuggerPresent()) DebugBreak();
    std::cerr << "fail_fast() called." << std::endl;
    std::exit(EXIT_FAILURE);
}