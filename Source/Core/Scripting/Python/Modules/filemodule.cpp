#include "filemodule.h"

#include <QObject>
#include <QString>
#include <QWidget>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "DolphinQt/QtUtils/DolphinFileDialog.h"
#include "Scripting/Python/Utils/module.h"

struct FileState
{
  // This is unused besides initializion of the module
};

static std::string scriptDir;

static PyObject* get_script_dir(PyObject* module, PyObject* args)
{
  return Py_BuildValue("s", scriptDir.c_str());
}

static PyObject* open_file(PyObject* module, PyObject* args)
{
  QString fileName =
      DolphinFileDialog::getOpenFileName(nullptr, QObject::tr("Open File"), scriptDir.c_str());

  return Py_BuildValue("s", fileName.toLocal8Bit().constData());
}

static void setup_file_module(PyObject* module, FileState* state)
{
  // I don't think we need anything here yet
}

PyMODINIT_FUNC PyInit_dol_file()
{
  scriptDir = File::GetUserPath(D_LOAD_IDX) + "Scripts";
  static PyMethodDef methods[] = {{"get_script_dir", get_script_dir, METH_NOARGS, ""},
  								  {"open_file", open_file, METH_NOARGS, ""},
                                  {nullptr, nullptr, 0, nullptr}};
  static PyModuleDef module_def =
      Py::MakeStatefulModuleDef<FileState, setup_file_module>("dolphin_file", methods);
  return PyModuleDef_Init(&module_def);
}
