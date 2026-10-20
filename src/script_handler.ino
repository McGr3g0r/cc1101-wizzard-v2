//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-20
//------------------------------------------------------------------------------------------------------------------
// CLI 'script' command + startup init.scr execution.
//
// A script is a plain-text file '<name>.scr' on LittleFS. Each line is a CLI
// command; blank lines and lines starting with '#' are ignored. Scripts are
// managed from the CLI (list/show/new/add/del/rename/run) and, at boot,
// '/init.scr' (or the file named by the 'init' env var) is executed BEFORE the
// serial/USB loop starts - see script_run_init(), called from setup().
//------------------------------------------------------------------------------------------------------------------
#include "settings.h"
#include "CmdHandler.h"
#include "Script.h"
//------------------------------------------------------------------------------------------------------------------
#if USE_FILE_SYSTEM == 1
#include "LittleFS.h"
#include "env.h"
#endif
//------------------------------------------------------------------------------------------------------------------
CmdHandler* getRootCommandHandler(void);
Script*     getScript(void);                 // global Script object (cc1101-wizzard-v2.ino)
void        cli_exec_line(const char* line); // parse+run one CLI line (cc1101-wizzard-v2.ino)
//------------------------------------------------------------------------------------------------------------------
#define SCRIPT_SUFFIX      ".scr"
#define SCRIPT_NAME_MAXLEN 24
#define SCRIPT_LINE_MAXLEN 256
//------------------------------------------------------------------------------------------------------------------
static void script_path(char* dst, int dstlen, const char* name)
{
   snprintf(dst, dstlen, "/%s" SCRIPT_SUFFIX, name);
}
//------------------------------------------------------------------------------------------------------------------
static bool script_valid_name(const char* name)
{
   if (name == 0) return false;
   int n = strlen(name);
   if (n < 1 || n > SCRIPT_NAME_MAXLEN) return false;
   for (int i = 0; i < n; i++)
   {
      char c = name[i];
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
      if (!ok) return false;
   }
   return true;
}
//------------------------------------------------------------------------------------------------------------------
#if USE_FILE_SYSTEM == 1
//------------------------------------------------------------------------------------------------------------------
static bool script_exists(const char* name)
{
   if (!script_valid_name(name)) return false;
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(path, sizeof(path), name);
   return LittleFS.exists(path);
}
//------------------------------------------------------------------------------------------------------------------
static bool script_new(const char* name)
{
   if (!script_valid_name(name)) return false;
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(path, sizeof(path), name);
   File f = LittleFS.open(path, "w");   // truncate/create
   if (!f) return false;
   f.close();
   return true;
}
//------------------------------------------------------------------------------------------------------------------
static bool script_add_line(const char* name, const char* line)
{
   if (!script_valid_name(name) || line == 0) return false;
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(path, sizeof(path), name);
   File f = LittleFS.open(path, "a");
   if (!f) return false;
   f.print(line);
   f.print("\n");
   f.close();
   return true;
}
//------------------------------------------------------------------------------------------------------------------
static bool script_del(const char* name)
{
   if (!script_valid_name(name)) return false;
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(path, sizeof(path), name);
   if (!LittleFS.exists(path)) return false;
   return LittleFS.remove(path);
}
//------------------------------------------------------------------------------------------------------------------
static bool script_rename(const char* oldName, const char* newName)
{
   if (!script_valid_name(oldName) || !script_valid_name(newName)) return false;
   char oldPath[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   char newPath[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(oldPath, sizeof(oldPath), oldName);
   script_path(newPath, sizeof(newPath), newName);
   if (!LittleFS.exists(oldPath)) return false;
   if (LittleFS.exists(newPath)) return false;   // don't clobber an existing script
   return LittleFS.rename(oldPath, newPath);
}
//------------------------------------------------------------------------------------------------------------------
static int script_list(Stream& out)
{
   int cnt = 0;
   Dir dir = LittleFS.openDir("/");
   while (dir.next())
   {
      if (!dir.isFile()) continue;
      String fn = dir.fileName();
      if (fn.startsWith("/")) fn = fn.substring(1);
      if (fn.endsWith(SCRIPT_SUFFIX))
      {
         out.println(fn.substring(0, fn.length() - strlen(SCRIPT_SUFFIX)));   // name without .scr
         cnt++;
      }
   }
   return cnt;
}
//------------------------------------------------------------------------------------------------------------------
static bool script_show(const char* name, Stream& out)
{
   if (!script_valid_name(name)) return false;
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(path, sizeof(path), name);
   if (!LittleFS.exists(path)) return false;
   File f = LittleFS.open(path, "r");
   if (!f) return false;
   while (f.available())
   {
      String s = f.readStringUntil('\n');
      out.println(s);
   }
   f.close();
   return true;
}
//------------------------------------------------------------------------------------------------------------------
// Execute a script file synchronously, line by line, through the CLI. Used at
// boot for init.scr (no re-entrancy: the main loop has not started yet).
static void script_exec_file(const char* path)
{
   if (!LittleFS.exists(path)) return;
   File f = LittleFS.open(path, "r");
   if (!f) return;
   while (f.available())
   {
      String s = f.readStringUntil('\n');
      s.trim();
      if (s.length() == 0 || s[0] == '#') continue;
      STDOUT.print("init> "); STDOUT.println(s);
      cli_exec_line(s.c_str());
   }
   f.close();
}
//------------------------------------------------------------------------------------------------------------------
// Called from setup() BEFORE the serial/USB loop. Runs '/init.scr', or the file
// named by the 'init' env var if set.
void script_run_init(void)
{
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   char* initName = env_get("init", (char*)"none");
   if (initName == 0 || strcmp(initName, "none") == 0)
      strncpy(path, "/init" SCRIPT_SUFFIX, sizeof(path) - 1), path[sizeof(path) - 1] = 0;
   else
      snprintf(path, sizeof(path), "/%s", initName);   // env value is a full filename
   script_exec_file(path);
}
//------------------------------------------------------------------------------------------------------------------
#else // no file system
static bool script_exists(const char*)             { return false; }
static bool script_new(const char*)                { return false; }
static bool script_add_line(const char*, const char*) { return false; }
static bool script_del(const char*)                { return false; }
static bool script_rename(const char*, const char*){ return false; }
static int  script_list(Stream&)                   { return 0; }
static bool script_show(const char*, Stream&)      { return false; }
void        script_run_init(void)                  { }
#endif
//==================================================================================================================
// CLI command: script
//==================================================================================================================
CmdStatus_e script_main(void* parent, int argc, char* argv[]);
CmdStatus_e script_list_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e script_show_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e script_new_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e script_add_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e script_del_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e script_rename_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e script_run_cmd(void* parent, int argc, char* argv[]);
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t script_handler = { &script_main, NULL, NULL, "script", "LittleFS .scr scripts, use: 'script help'", 0, "", "script help" };
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t script_sub_cmd[] = {
   { &script_main,       &script_handler, NULL, "help",   "this help", 0, "", "script help" },
   { &script_list_cmd,   &script_handler, NULL, "list",   "list stored scripts", 0, "", "script list" },
   { &script_show_cmd,   &script_handler, NULL, "show",   "show a script's lines: <name>", 1, "t", "script show init" },
   { &script_new_cmd,    &script_handler, NULL, "new",    "create/clear an empty script: <name>", 1, "t", "script new init" },
   { &script_add_cmd,    &script_handler, NULL, "add",    "append a command line: <name> <cli-command...>", 2, "tt", "script add init radio r0 set freq 433920000" },
   { &script_del_cmd,    &script_handler, NULL, "del",    "delete a script: <name>", 1, "t", "script del init" },
   { &script_rename_cmd, &script_handler, NULL, "rename", "rename a script: <old> <new>", 2, "tt", "script rename init boot" },
   { &script_run_cmd,    &script_handler, NULL, "run",    "execute a script now: <name>", 1, "t", "script run init" },
   { 0, 0, NULL, "", "", 0, "", "" }
};
//------------------------------------------------------------------------------------------------------------------
void script_cmd_init(void)
{
   getRootCommandHandler()->registerHandler(&script_handler, script_sub_cmd);
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_main(void* parent, int argc, char* argv[])
{
   STDOUT.println(script_handler.desc);
   int idx = 0;
   cmd_handler_t* item = &script_handler.sub_cmd[idx];
   while (item && item->handler != 0)
   {
      STDOUT.print("- "); STDOUT.print(item->cmd); STDOUT.print(" : "); STDOUT.println(item->desc);
      idx++;
      item = &script_handler.sub_cmd[idx];
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_list_cmd(void* parent, int argc, char* argv[])
{
   STDOUT.println("scripts:");
   script_list(STDOUT);
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_show_cmd(void* parent, int argc, char* argv[])
{
   if (!script_show(argv[2], STDOUT))
   {
      getRootCommandHandler()->setHint("script not found or bad name");
      return FILE_ERROR;
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_new_cmd(void* parent, int argc, char* argv[])
{
   if (!script_valid_name(argv[2]))
   {
      getRootCommandHandler()->setHint("bad script name (use a-z A-Z 0-9 _ - , max 24)");
      return WRONG_PARAMS;
   }
   if (!script_new(argv[2]))
      return FILE_ERROR;
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_add_cmd(void* parent, int argc, char* argv[])
{
   if (!script_valid_name(argv[2]))
   {
      getRootCommandHandler()->setHint("bad script name");
      return WRONG_PARAMS;
   }
   if (!script_exists(argv[2]) && !script_new(argv[2]))
      return FILE_ERROR;

   // join argv[3..] into one command line
   char line[SCRIPT_LINE_MAXLEN];
   memset(line, 0, sizeof(line));
   for (int i = 3; i < argc; i++)
   {
      if (i > 3) strncat(line, " ", sizeof(line) - strlen(line) - 1);
      strncat(line, argv[i], sizeof(line) - strlen(line) - 1);
   }
   if (!script_add_line(argv[2], line))
      return FILE_ERROR;
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_del_cmd(void* parent, int argc, char* argv[])
{
   if (!script_del(argv[2]))
   {
      getRootCommandHandler()->setHint("script not found or bad name");
      return FILE_ERROR;
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_rename_cmd(void* parent, int argc, char* argv[])
{
   if (!script_valid_name(argv[2]) || !script_valid_name(argv[3]))
   {
      getRootCommandHandler()->setHint("bad script name");
      return WRONG_PARAMS;
   }
   if (!script_rename(argv[2], argv[3]))
   {
      getRootCommandHandler()->setHint("rename failed (source missing or target exists)");
      return FILE_ERROR;
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e script_run_cmd(void* parent, int argc, char* argv[])
{
#if USE_FILE_SYSTEM == 1
   if (!script_exists(argv[2]))
   {
      getRootCommandHandler()->setHint("script not found or bad name");
      return WRONG_PARAMS;
   }
   // Hand the file to the global Script object; the main loop drains it line by
   // line through the CLI (same path as a boot script). Done this way instead
   // of running inline to avoid re-entering the shared parser/handler.
   char path[2 + SCRIPT_NAME_MAXLEN + 4 + 1];
   script_path(path, sizeof(path), argv[2]);
   getScript()->load(path);
   return OK;
#else
   getRootCommandHandler()->setHint("file system disabled");
   return FILE_ERROR;
#endif
}
//------------------------------------------------------------------------------------------------------------------
