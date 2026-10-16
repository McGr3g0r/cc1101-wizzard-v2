//------------------------------------------------------------------------------------------------------------------
#include "settings.h"
#include "rprofile.h"
#include "radio_iface.h"
#include "CmdHandler.h"
//------------------------------------------------------------------------------------------------------------------
#if USE_FILE_SYSTEM == 1
#include "LittleFS.h"
#endif
//------------------------------------------------------------------------------------------------------------------
CmdHandler* getRootCommandHandler(void);
// Implemented in radio_handler.ino: tokenize a profile line and run it on 'ctx' (a radio_iface_t*).
void radio_profile_exec_line(void* ctx, const char* line);
//------------------------------------------------------------------------------------------------------------------
#define RPROFILE_PREFIX "/prof_"
//------------------------------------------------------------------------------------------------------------------
static void rprofile_path(char* dst, int dstlen, const char* name)
{
   snprintf(dst, dstlen, RPROFILE_PREFIX "%s", name);
}
//------------------------------------------------------------------------------------------------------------------
bool rprofile_valid_name(const char* name)
{
   if (name == 0)
      return false;
   int n = strlen(name);
   if (n < 1 || n > RPROFILE_NAME_MAXLEN)
      return false;
   for (int i = 0; i < n; i++)
   {
      char c = name[i];
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
      if (!ok)
         return false;
   }
   return true;
}
//------------------------------------------------------------------------------------------------------------------
#if USE_FILE_SYSTEM == 1
//------------------------------------------------------------------------------------------------------------------
bool rprofile_exists(const char* name)
{
   if (!rprofile_valid_name(name)) return false;
   char path[8 + RPROFILE_NAME_MAXLEN + 1];
   rprofile_path(path, sizeof(path), name);
   return LittleFS.exists(path);
}
//------------------------------------------------------------------------------------------------------------------
bool rprofile_new(const char* name)
{
   if (!rprofile_valid_name(name)) return false;
   char path[8 + RPROFILE_NAME_MAXLEN + 1];
   rprofile_path(path, sizeof(path), name);
   File f = LittleFS.open(path, "w");
   if (!f) return false;
   f.close();
   return true;
}
//------------------------------------------------------------------------------------------------------------------
bool rprofile_add_line(const char* name, const char* line)
{
   if (!rprofile_valid_name(name) || line == 0) return false;
   char path[8 + RPROFILE_NAME_MAXLEN + 1];
   rprofile_path(path, sizeof(path), name);
   File f = LittleFS.open(path, "a");
   if (!f) return false;
   f.print(line);
   f.print("\n");
   f.close();
   return true;
}
//------------------------------------------------------------------------------------------------------------------
bool rprofile_del(const char* name)
{
   if (!rprofile_valid_name(name)) return false;
   char path[8 + RPROFILE_NAME_MAXLEN + 1];
   rprofile_path(path, sizeof(path), name);
   if (!LittleFS.exists(path)) return false;
   return LittleFS.remove(path);
}
//------------------------------------------------------------------------------------------------------------------
int rprofile_list(Stream& out)
{
   int cnt = 0;
   int plen = strlen(RPROFILE_PREFIX);
   Dir dir = LittleFS.openDir("/");
   while (dir.next())
   {
      if (!dir.isFile()) continue;
      String fn = dir.fileName();
      // fileName() may come back with or without the leading '/'
      String full = fn.startsWith("/") ? fn : (String("/") + fn);
      if (full.startsWith(RPROFILE_PREFIX))
      {
         out.println(full.substring(plen));
         cnt++;
      }
   }
   return cnt;
}
//------------------------------------------------------------------------------------------------------------------
bool rprofile_show(const char* name, Stream& out)
{
   if (!rprofile_valid_name(name)) return false;
   char path[8 + RPROFILE_NAME_MAXLEN + 1];
   rprofile_path(path, sizeof(path), name);
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
bool rprofile_apply(const char* name, rprofile_apply_cb cb, void* ctx)
{
   if (!rprofile_valid_name(name) || cb == 0) return false;
   char path[8 + RPROFILE_NAME_MAXLEN + 1];
   rprofile_path(path, sizeof(path), name);
   if (!LittleFS.exists(path)) return false;
   File f = LittleFS.open(path, "r");
   if (!f) return false;

   char line[RPROFILE_LINE_MAXLEN];
   while (f.available())
   {
      String s = f.readStringUntil('\n');
      s.trim();
      if (s.length() == 0 || s[0] == '#')
         continue;
      memset(line, 0, sizeof(line));
      strncpy(line, s.c_str(), sizeof(line) - 1);
      cb(ctx, line);
   }
   f.close();
   return true;
}
//------------------------------------------------------------------------------------------------------------------
#else // no file system: profiles unavailable
bool rprofile_exists(const char*)            { return false; }
bool rprofile_new(const char*)               { return false; }
bool rprofile_add_line(const char*, const char*) { return false; }
bool rprofile_del(const char*)               { return false; }
int  rprofile_list(Stream&)                  { return 0; }
bool rprofile_show(const char*, Stream&)     { return false; }
bool rprofile_apply(const char*, rprofile_apply_cb, void*) { return false; }
#endif
//==================================================================================================================
// CLI command: profile
//==================================================================================================================
CmdStatus_e profile_main(void* parent, int argc, char* argv[]);
CmdStatus_e profile_list_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e profile_show_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e profile_new_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e profile_add_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e profile_del_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e profile_apply_cmd(void* parent, int argc, char* argv[]);
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t profile_handler = { &profile_main, NULL, NULL, "profile", "radio work profiles (LittleFS), use: 'profile help'", 0, "", "profile help" };
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t profile_sub_cmd[] = {
   { &profile_main,      &profile_handler, NULL, "help",  "this help", 0, "", "profile help" },
   { &profile_list_cmd,  &profile_handler, NULL, "list",  "list stored profiles", 0, "", "profile list" },
   { &profile_show_cmd,  &profile_handler, NULL, "show",  "show profile lines <name>", 1, "t", "profile show bikegate" },
   { &profile_new_cmd,   &profile_handler, NULL, "new",   "create/clear empty profile <name>", 1, "t", "profile new bikegate" },
   { &profile_add_cmd,   &profile_handler, NULL, "add",   "append a command line: <name> <radio-subcmd...>", 2, "tt", "profile add bikegate set freq 433920000" },
   { &profile_del_cmd,   &profile_handler, NULL, "del",   "delete profile <name>", 1, "t", "profile del bikegate" },
   { &profile_apply_cmd, &profile_handler, NULL, "apply", "apply profile <name> to interface <iface>", 2, "tt", "profile apply bikegate r0" },
   { 0, 0, NULL, "", "", 0, "", "" }
};
//------------------------------------------------------------------------------------------------------------------
void rprofile_cmd_init(void)
{
   getRootCommandHandler()->registerHandler(&profile_handler, profile_sub_cmd);
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_main(void* parent, int argc, char* argv[])
{
   STDOUT.println(profile_handler.desc);
   int idx = 0;
   cmd_handler_t* item = &profile_handler.sub_cmd[idx];
   while (item && item->handler != 0)
   {
      STDOUT.print("- "); STDOUT.print(item->cmd); STDOUT.print(" : "); STDOUT.println(item->desc);
      idx++;
      item = &profile_handler.sub_cmd[idx];
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_list_cmd(void* parent, int argc, char* argv[])
{
   STDOUT.println("profiles:");
   rprofile_list(STDOUT);
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_show_cmd(void* parent, int argc, char* argv[])
{
   if (!rprofile_show(argv[2], STDOUT))
   {
      getRootCommandHandler()->setHint("profile not found or bad name");
      return FILE_ERROR;
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_new_cmd(void* parent, int argc, char* argv[])
{
   if (!rprofile_valid_name(argv[2]))
   {
      getRootCommandHandler()->setHint("bad profile name (use a-z 0-9 _ - , max 16)");
      return WRONG_PARAMS;
   }
   if (!rprofile_new(argv[2]))
      return FILE_ERROR;
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_add_cmd(void* parent, int argc, char* argv[])
{
   if (!rprofile_valid_name(argv[2]))
   {
      getRootCommandHandler()->setHint("bad profile name");
      return WRONG_PARAMS;
   }
   if (!rprofile_exists(argv[2]) && !rprofile_new(argv[2]))
      return FILE_ERROR;

   // join argv[3..] into one command line
   char line[RPROFILE_LINE_MAXLEN];
   memset(line, 0, sizeof(line));
   for (int i = 3; i < argc; i++)
   {
      if (i > 3) strncat(line, " ", sizeof(line) - strlen(line) - 1);
      strncat(line, argv[i], sizeof(line) - strlen(line) - 1);
   }
   if (!rprofile_add_line(argv[2], line))
      return FILE_ERROR;
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_del_cmd(void* parent, int argc, char* argv[])
{
   if (!rprofile_del(argv[2]))
   {
      getRootCommandHandler()->setHint("profile not found or bad name");
      return FILE_ERROR;
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e profile_apply_cmd(void* parent, int argc, char* argv[])
{
   radio_iface_t* ifc = radio_iface_find(argv[3]);
   if (ifc == 0)
   {
      getRootCommandHandler()->setHint("unknown interface, use 'radio list'");
      return WRONG_PARAMS;
   }
   if (!rprofile_exists(argv[2]))
   {
      getRootCommandHandler()->setHint("profile not found");
      return WRONG_PARAMS;
   }
   if (!rprofile_apply(argv[2], radio_profile_exec_line, ifc))
      return FILE_ERROR;
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
