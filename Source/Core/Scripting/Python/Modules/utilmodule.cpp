#include "utilmodule.h"

#include <nfd.h>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/ConfigManager.h"
#include "Core/Host.h"
#include "Core/Movie.h"
#include "Core/System.h"
#include "Scripting/Python/PyScriptingBackend.h"
#include "Scripting/Python/Utils/module.h"
#include "Scripting/Python/Utils/as_py_func.h"
#include "Scripting/ScriptList.h"

struct FileState
{
  // This is unused besides initializion of the module
};

static std::string scriptDir;
static std::string dumpDir;
static std::string userDir;

static PyObject* get_game_id(PyObject* module, PyObject* args)
{
  return Py_BuildValue("s", SConfig::GetInstance().GetGameID().c_str());
}

static PyObject* get_script_dir(PyObject* module, PyObject* args)
{
  return Py_BuildValue("s", scriptDir.c_str());
}

static PyObject* get_dump_dir(PyObject* module, PyObject* args)
{
  return Py_BuildValue("s", dumpDir.c_str());
}

static PyObject* get_user_dir(PyObject* module, PyObject* args)
{
  return Py_BuildValue("s", userDir.c_str());
}

static PyObject* start_framedump(PyObject* module, PyObject* args)
{
  Config::SetBaseOrCurrent(Config::MAIN_MOVIE_DUMP_FRAMES, true);
  Py_RETURN_NONE;
}

static PyObject* stop_framedump(PyObject* module, PyObject* args)
{
  Config::SetBaseOrCurrent(Config::MAIN_MOVIE_DUMP_FRAMES, false);
  Py_RETURN_NONE;
}

static PyObject* is_framedumping(PyObject* module, PyObject* args)
{
  if(Config::Get(Config::MAIN_MOVIE_DUMP_FRAMES))
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyObject* start_audiodump(PyObject* module, PyObject* args)
{
  Config::SetBaseOrCurrent(Config::MAIN_DUMP_AUDIO, true);
  Py_RETURN_NONE;
}

static PyObject* stop_audiodump(PyObject* module, PyObject* args)
{
  Config::SetBaseOrCurrent(Config::MAIN_DUMP_AUDIO, false);
  Py_RETURN_NONE;
}

static PyObject* is_audiodumping(PyObject* module, PyObject* args)
{
  if(Config::Get(Config::MAIN_DUMP_AUDIO))
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyObject* save_screenshot(PyObject* module, PyObject* args, PyObject* kwargs)
{
  char* filename = nullptr;
  char* temp = const_cast<char*>("filename");
  static char* kwlist[] = {temp, NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwargs,"|s", kwlist, &filename))
  	return nullptr;

  if(filename)
  	Core::SaveScreenShot(filename);
  else
  	Core::SaveScreenShot();
  
  Py_RETURN_NONE;
}

static PyObject* open_file(PyObject* module, PyObject* args)
{
  std::string filePath;

  NFD_Init();
  
  nfdchar_t *outPath;
  nfdfilteritem_t filterItem[1] = { { "All Files", "*" } };
  nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, NULL);

  if(result != NFD_OKAY)
  	Py_RETURN_NONE;

  filePath = outPath;
 
  return Py_BuildValue("s", filePath.c_str());
}

static PyObject* toggle_play(PyObject* module, PyObject* args)
{
  // Play/Pause can only be done on the host thread
  Core::QueueHostJob([](Core::System& system) {
    Core::State current_state = GetState(system);

    switch (current_state)
    {
    case Core::State::Paused:
      Core::SetState(system, Core::State::Running);
      break;
    case Core::State::Running:
      Core::SetState(system, Core::State::Paused);
      break;
    default:
      break;
    }
  });

  Py_RETURN_NONE;
}

static PyObject* is_paused(PyObject* module, PyObject* args)
{
  Core::State current_state = GetState(Core::System::GetInstance());

    switch (current_state)
    {
    case Core::State::Paused:
      Py_RETURN_TRUE;
      break;
    case Core::State::Running:
      Py_RETURN_FALSE;
      break;
    default:
      Py_RETURN_NONE;
      break;
    }
}

static PyObject* renderer_has_focus(PyObject* module, PyObject* args)
{
  bool focus = Host_RendererHasFocus();

  if (focus)
    Py_RETURN_TRUE;
  else
    Py_RETURN_FALSE;
}

static PyObject* renderer_geometry(PyObject* module, PyObject* args)
{
  int x = Host_GetRendererX();
  int y = Host_GetRendererY();
  int w = Host_GetRendererWidth();
  int h = Host_GetRendererHeight();
  return Py_BuildValue("iiii", x, y, w, h);
}

static PyObject* cancel_script(PyObject* module, PyObject* args, PyObject* kwargs)
{
  char* filename = nullptr;
  char* temp = const_cast<char*>("filename");
  static char* kwlist[] = {temp, NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", kwlist, &filename))
    return nullptr;

  if (filename)
  {
    // We queue the cancel so it doesn't happen before the script main() is over
    Core::QueueHostJob(
        [filename](Core::System& system) { Scripts::g_scripts_to_stop.push_front(filename); });
  }

  Py_RETURN_NONE;
}

static PyObject* activate_script(PyObject* module, PyObject* args, PyObject* kwargs)
{
  char* filename = nullptr;
  char* temp = const_cast<char*>("filename");
  static char* kwlist[] = {temp, NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", kwlist, &filename))
    return nullptr;

  if (filename)
  {
    // We queue the cancel so it doesn't happen before the script main() is over
     Core::QueueHostJob(
        [filename](Core::System& system) { Scripts::g_scripts_to_start.push_front(filename); });
  }

  Py_RETURN_NONE;
}

static PyObject* get_script_name(PyObject* module, PyObject* args)
{
  PyScripting::PyScriptingBackend* cur_instance = PyScripting::PyScriptingBackend::GetCurrent();

  return Py_BuildValue("s", (cur_instance->GetScriptPath()).c_str());
}

static PyObject* get_script_id(PyObject* module, PyObject* args)
{
  PyScripting::PyScriptingBackend* cur_instance = PyScripting::PyScriptingBackend::GetCurrent();

  return Py_BuildValue("i", (cur_instance->GetScriptId()));
}

static PyObject* get_frame_count(PyObject* module, PyObject* args)
{
  auto& movie = Core::System::GetInstance().GetMovie();
  return Py_BuildValue("i", (movie.GetCurrentFrame()));
}

static void setup_file_module(PyObject* module, FileState* state)
{
  // I don't think we need anything here yet
}

PyMODINIT_FUNC PyInit_dol_utils()
{
  scriptDir = File::GetUserPath(D_SCRIPTS_IDX);
  dumpDir = File::GetUserPath(D_DUMP_IDX);
  userDir = File::GetUserPath(D_USER_IDX);
  static PyMethodDef methods[] = {{"get_game_id", get_game_id, METH_NOARGS, ""},
                                  {"get_script_dir", get_script_dir, METH_NOARGS, ""},
                                  {"get_dump_dir", get_dump_dir, METH_NOARGS, ""},
                                  {"get_user_dir", get_user_dir, METH_NOARGS, ""},
                                  {"open_file", open_file, METH_NOARGS, ""},
                                  {"start_framedump", start_framedump, METH_NOARGS, ""},
                                  {"stop_framedump", stop_framedump, METH_NOARGS, ""},
                                  {"is_framedumping", is_framedumping, METH_NOARGS, ""},
                                  {"start_audiodump", start_audiodump, METH_NOARGS, ""},
                                  {"stop_audiodump", stop_audiodump, METH_NOARGS, ""},
                                  {"is_audiodumping", is_audiodumping, METH_NOARGS, ""},
                                  {"save_screenshot", (PyCFunction) save_screenshot, METH_VARARGS | METH_KEYWORDS, ""},
                                  {"toggle_play", toggle_play, METH_NOARGS, ""},
                                  {"is_paused", is_paused, METH_NOARGS, ""},
                                  {"renderer_has_focus", renderer_has_focus, METH_NOARGS, ""},
                                  {"renderer_geometry", renderer_geometry, METH_NOARGS, ""},
                                  {"cancel_script", (PyCFunction) cancel_script, METH_VARARGS | METH_KEYWORDS, ""},
                                  {"activate_script", (PyCFunction) activate_script, METH_VARARGS | METH_KEYWORDS, ""},
                                  {"get_script_name", get_script_name, METH_NOARGS, ""},
                                  {"get_script_id", get_script_id, METH_NOARGS, ""},
                                  {"get_frame_count", get_frame_count, METH_NOARGS, ""},
                                  {nullptr, nullptr, 0, nullptr}};
  static PyModuleDef module_def =
      Py::MakeStatefulModuleDef<FileState, setup_file_module>("dolphin_utils", methods);
  return PyModuleDef_Init(&module_def);
}
