//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-17
//------------------------------------------------------------------------------------------------------------------
#include "Parser.h"
//------------------------------------------------------------------------------------------------------------------
char* env_get_direct(const char* name, char* defval);
//------------------------------------------------------------------------------------------------------------------
bool Parser::parse(String line)
{
    argc = 0;
    memset(buffer, 0, sizeof(buffer));
    memset(argv, 0, sizeof(char*) * PARSER_MAX_PARAMS);
    strcat(buffer, line.c_str());

    int idx = 0;
    while (idx < line.length())
    {
       int start = idx;
       while (idx < line.length() && buffer[idx] != ' ') idx++;
       if (start != idx)
       {
           if (argc >= PARSER_MAX_PARAMS) break;   // never write past argv[] (overflow = crash)
           argv[argc++] = &buffer[start];
           buffer[idx++] = 0;
       }
        while (idx < line.length() && buffer[idx] == ' ') idx++;
    }

    apply_env();

    if (argc >= 1 && argc <= PARSER_MAX_PARAMS)
       return true; 
    else    
       return false;
}
//------------------------------------------------------------------------------------------------------------------
void Parser::apply_env(void)
{
  char tmp[64];
  for (int idx = 0 ; idx < argc; idx++)
  {
      if (argv[idx][0] == '$')
      {
         char* arg = argv[idx];
         memset(tmp, 0, sizeof(tmp));
         strcat(tmp, &arg[1]);
         argv[idx] = env_get(tmp, arg);
      }
  }
}
//------------------------------------------------------------------------------------------------------------------
int Parser::getArgc(void)
{
    return argc;
}
//------------------------------------------------------------------------------------------------------------------
char** Parser::getArgv(void)
{
    return argv;
}
//------------------------------------------------------------------------------------------------------------------
